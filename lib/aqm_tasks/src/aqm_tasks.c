#include "aqm_datastore.h"
#include "aqm_modbus_reg.h"
#include "aqm_modbus.h"
#include "aqm_config.h"
#include "aqm_tasks.h"
#include "aqm_gpio.h"
#include "aqm_i2c.h"
#include "ads1115.h"
#include "sen5x_i2c.h"

#include <esp_log.h>
#include <esp_err.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define OUTPUT_TASK_DELAY_MS 100 // Delay for output task loop
#define AQM_ADC_MAX_WAIT_TIME_MS 150 // Max time to wait for ADC conversion before timeout
#define SENSOR_TASK_DELAY_MS 1000 // Delay for sensor reading task loop (1 second)

static TaskHandle_t sensor_task_handle = NULL;
static uint8_t sen55_initialized = 0;

static const char *TAG = "AQM_TASKS";

// -----------------------------------------------------------------------------
// OUTPUT TASK (Hardware Control & NVS Saving)
// -----------------------------------------------------------------------------

/**
 * @brief Processes intentions from the control_word and applies them to hardware.
 * Evaluates hardware states ONLY when the 'cw_changed' flag is set.
 */
static void aqm_output_task(void *pvParameters) {
    ESP_LOGI(TAG, "Output Task Started");

    while(1){
        // Check if there was any change in the Control Word
        if (aqm_data.control_word.flags.cw_changed == 1) {
            
            ESP_LOGI(TAG, "Control Word change detected. Processing hardware updates...");

            // 1. Evaluate Relay State (Intent vs Reality)
            if (aqm_data.control_word.flags.relay_state != aqm_data.status.status_word.flags.relay_state) {
                if (aqm_data.control_word.flags.relay_state == 1) {
                    ESP_LOGI(TAG, "CW Command: Relay ON");
                    if (aqm_relay_turn_on() == ESP_OK) {
                        aqm_data.status.status_word.flags.relay_state = 1; // Update actual status
                    }
                } else {
                    ESP_LOGI(TAG, "CW Command: Relay OFF");
                    if (aqm_relay_turn_off() == ESP_OK) {
                        aqm_data.status.status_word.flags.relay_state = 0;
                    }
                }
            }

            // 2. Evaluate LED State (Intent vs Reality)
            if (aqm_data.control_word.flags.led_state != aqm_data.status.status_word.flags.led_state) {
                if (aqm_data.control_word.flags.led_state == 1) {
                    ESP_LOGI(TAG, "CW Command: LED ON");
                    if (aqm_led_turn_on() == ESP_OK) {
                        aqm_data.status.status_word.flags.led_state = 1;
                    }
                } else {
                    ESP_LOGI(TAG, "CW Command: LED OFF");
                    if (aqm_led_turn_off() == ESP_OK) {
                        aqm_data.status.status_word.flags.led_state = 0;
                    }
                }
            }

            // 3. Save the new configuration to NVS memory
            ESP_LOGI(TAG, "Saving updated Control Word to NVS.");
            aqm_control_word_save_nvs(); 
            
            // 4. Clear the flag so we don't process or save again until the next change
            aqm_data.control_word.flags.cw_changed = 0;

            // Determine if the change requires a full system REBOOT.
            // Certain features (like Wi-Fi stack or Modbus instances) cannot be 
            // easily destroyed and recreated on-the-fly without memory leaks.
            uint8_t needs_reboot = 0;
            
            // Example 1: Wi-Fi was disabled by user, but we are currently connected
            if (!aqm_data.control_word.flags.wifi_en && aqm_data.status.status_word.flags.wifi_en) {
                ESP_LOGW(TAG, "Wi-Fi disable requested.");
                needs_reboot = 1;
            }
            
            // Example 2: Wi-Fi was enabled by user, but we are currently offline
            // (Assuming you track this, or just trigger reboot if wifi_en state toggled)
            if (aqm_data.control_word.flags.wifi_en && !aqm_data.status.status_word.flags.wifi_en) {
                 ESP_LOGW(TAG, "Wi-Fi enable requested.");
                 needs_reboot = 1;
            }

            /*if (aqm_data.control_word.flags.mb_tcp_en && !aqm_data.status.status_word.flags.mb_tcp_en) {
                ESP_LOGW(TAG, "Modbus TCP enable requested.");
                needs_reboot = 1;
            }

            if (!aqm_data.control_word.flags.mb_tcp_en && aqm_data.status.status_word.flags.mb_tcp_en) {
                ESP_LOGW(TAG, "Modbus TCP disable requested.");
                needs_reboot = 1;
            }*/
            


            // Note: You can add more checks here for mb_tcp_en or mb_rtu_en toggles

            if (needs_reboot) {
                ESP_LOGW(TAG, "Critical configuration changed! Rebooting in 3 seconds...");
                
                // Give Modbus TCP/RTU time to send the ACK response back to the Master 
                // before pulling the plug on the system.
                vTaskDelay(pdMS_TO_TICKS(3000)); 
                esp_restart();
            }

        }

        // Delay to yield to other RTOS tasks
        vTaskDelay(pdMS_TO_TICKS(OUTPUT_TASK_DELAY_MS));
    }
}

// -----------------------------------------------------------------------------
// SENSOR MEASUREMENT TASK (ADC & SEN55)
// -----------------------------------------------------------------------------

/**
 * @brief Hardware ISR Handler for the RDY/ALERT pins.
 */
static void IRAM_ATTR aqm_adc_rdy_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (sensor_task_handle != NULL) {
        vTaskNotifyGiveFromISR(sensor_task_handle, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

static esp_err_t aqm_adc_wait_and_read(uint8_t i2c_addr, int16_t *out_val) {
    // Sleep the task until ISR notifies us (or timeout occurs).
    // pdFALSE decrements the notification count by 1 (does not clear it completely).
    uint32_t notified = ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(AQM_ADC_MAX_WAIT_TIME_MS));

    if (notified == 0) {
        ESP_LOGE(TAG, "Conversion timeout! ISR didn't trigger for I2C 0x%02X", i2c_addr);
        return ESP_ERR_TIMEOUT;
    }

    // Read result and release RDY pin
    return ads1115_read_adc(i2c_addr, out_val);
}

/**
 * @brief Reads all configured channels concurrently from both ADS1115 chips
 * and updates the global datastore.
 */
static void aqm_read_all_adc_channels(void) {
    int16_t val1 = 0, val2 = 0;
    esp_err_t err1, err2;

    // Clear any pending notifications before starting the measurement cycle
    ulTaskNotifyTake(pdTRUE, 0);

    // ==========================================
    // PAIR 1: SO2 (Chip 1) and CO (Chip 2)
    // ==========================================
    ads1115_start_single_ended_conversion(ADS1115_ADDR_1, CHANNEL_SGX_SO2, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);
    ads1115_start_single_ended_conversion(ADS1115_ADDR_2, CHANNEL_CO_VAL, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);

    err1 = aqm_adc_wait_and_read(ADS1115_ADDR_1, &val1);
    err2 = aqm_adc_wait_and_read(ADS1115_ADDR_2, &val2);

    if (err1 == ESP_OK) aqm_data.adc_raw.so2_raw_val = (uint16_t)val1;
    if (err2 == ESP_OK) aqm_data.adc_raw.co_raw_val = (uint16_t)val2;

    // ==========================================
    // PAIR 2: 3.3V (Chip 1) and NH3 (Chip 2)
    // ==========================================
    ads1115_start_single_ended_conversion(ADS1115_ADDR_1, CHANNEL_3V3, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);
    ads1115_start_single_ended_conversion(ADS1115_ADDR_2, CHANNEL_NH3_VAL, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);

    err1 = aqm_adc_wait_and_read(ADS1115_ADDR_1, &val1);
    err2 = aqm_adc_wait_and_read(ADS1115_ADDR_2, &val2);

    if (err1 == ESP_OK) {
        aqm_data.adc_raw.v3v3_raw_val = (uint16_t)val1;
        aqm_data.status.v3v3_val = (uint16_t)(ads1115_compute_mv(val1, ADS1115_PGA_4_096V));
    }
    if (err2 == ESP_OK) aqm_data.adc_raw.nh3_raw_val = (uint16_t)val2;

    // ==========================================
    // PAIR 3: H2S (Chip 1) and NO2 (Chip 2)
    // ==========================================
    ads1115_start_single_ended_conversion(ADS1115_ADDR_1, CHANNEL_SGX_H2S, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);
    ads1115_start_single_ended_conversion(ADS1115_ADDR_2, CHANNEL_NO2_VAL, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);

    err1 = aqm_adc_wait_and_read(ADS1115_ADDR_1, &val1);
    err2 = aqm_adc_wait_and_read(ADS1115_ADDR_2, &val2);

    if (err1 == ESP_OK) aqm_data.adc_raw.h2s_raw_val = (uint16_t)val1;
    if (err2 == ESP_OK) aqm_data.adc_raw.no2_raw_val = (uint16_t)val2;

    // ==========================================
    // REMAINING CHANNEL: 5.0V (Chip 1) 
    // ==========================================
    ads1115_start_single_ended_conversion(ADS1115_ADDR_1, CHANNEL_5V, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);
    
    err1 = aqm_adc_wait_and_read(ADS1115_ADDR_1, &val1);
    
    if (err1 == ESP_OK) {
        aqm_data.adc_raw.v5v_raw_val = (uint16_t)val1;
        aqm_data.status.v5v_val = (uint16_t)(ads1115_compute_mv(val1, ADS1115_PGA_4_096V) * 2); 
    }

    aqm_data.status.timestamp = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ); 
}

static void aqm_calculate_gas_concentrations(void) {
    aqm_data.gases.so2_ppm = ads1115_compute_mv(aqm_data.adc_raw.so2_raw_val, ADS1115_PGA_4_096V) / SENSITIVITY_SO2_MV_PER_PPM;
    aqm_data.gases.h2s_ppm = ads1115_compute_mv(aqm_data.adc_raw.h2s_raw_val, ADS1115_PGA_4_096V) / SENSITIVITY_H2S_MV_PER_PPM;

    aqm_data.gases.co_mv  = ads1115_compute_mv(aqm_data.adc_raw.co_raw_val, ADS1115_PGA_4_096V); 
    aqm_data.gases.nh3_mv = ads1115_compute_mv(aqm_data.adc_raw.nh3_raw_val, ADS1115_PGA_4_096V);
    aqm_data.gases.no2_mv = ads1115_compute_mv(aqm_data.adc_raw.no2_raw_val, ADS1115_PGA_4_096V);
}

static void aqm_sen55_read_measurements(void) {
    int16_t error;

    // 1. INITIALIZATION
    if (!sen55_initialized) {
        ESP_LOGI(TAG, "Initializing SEN55...");
        
        error = sen5x_device_reset();
        if (error) {
            ESP_LOGE(TAG, "SEN55 Reset failed: %i", error);
            return; 
        }
        vTaskDelay(pdMS_TO_TICKS(500)); 

        error = sen5x_start_measurement();
        if (error) {
            ESP_LOGE(TAG, "SEN55 Start Measurement failed: %i", error);
            return;
        }

        ESP_LOGI(TAG, "SEN55 Initialized and measurement started.");
        sen55_initialized = 1;
    }

    // 2. CHECK DATA READY
    bool data_ready = false;
    error = sen5x_read_data_ready(&data_ready);
    if (error || !data_ready) return;

    // 3. READ VALUES
    uint16_t pm1p0, pm2p5, pm4p0, pm10p0;
    int16_t humidity, temperature, voc_index, nox_index;

    error = sen5x_read_measured_values(
        &pm1p0, &pm2p5, &pm4p0, &pm10p0, 
        &humidity, &temperature, &voc_index, &nox_index
    );

    if (error) {
        ESP_LOGE(TAG, "Error reading SEN55 values: %i", error);
    } else {
        // 4. STORE TO DATASTORE
        aqm_data.sen55.pm1_0 = pm1p0;
        aqm_data.sen55.pm2_5 = pm2p5; 
        aqm_data.sen55.pm4_0 = pm4p0;
        aqm_data.sen55.pm10_0 = pm10p0;
        aqm_data.sen55.temperature = temperature; 
        aqm_data.sen55.humidity = humidity; 
        aqm_data.sen55.voc_index = voc_index; 
        aqm_data.sen55.nox_index = nox_index; 
    }
}


static void aqm_print_measured_values(void) {

    ESP_LOGI(TAG, "SEN55: PM1.0: %.1f, PM2.5: %.1f, PM4.0: %.1f, PM10.0: %.1f [ug/m3]", 
            aqm_data.sen55.pm1_0/10.0f, aqm_data.sen55.pm2_5/10.0f, aqm_data.sen55.pm4_0/10.0f, aqm_data.sen55.pm10_0/10.0f);

    ESP_LOGI(TAG, "Env: Temp: %.1f°C, Hum: %.1f%%, VOC: %.1f, NOx: %.1f", 
            aqm_data.sen55.temperature / 200.0f, aqm_data.sen55.humidity / 100.0f, aqm_data.sen55.voc_index/10.0f, aqm_data.sen55.nox_index/10.0f);

    ESP_LOGI(TAG, "Gases: SO2: %.2fppm, H2S: %.2fppm, CO: %.2fmV, NH3: %.2fmV, NO2: %.2fmV", 
            aqm_data.gases.so2_ppm, aqm_data.gases.h2s_ppm, aqm_data.gases.co_mv, aqm_data.gases.nh3_mv, aqm_data.gases.no2_mv);

    ESP_LOGI(TAG, "Status: 3.3V: %u mV, 5.0V: %u mV", aqm_data.status.v3v3_val, aqm_data.status.v5v_val);

}

/**
 * @brief FreeRTOS Task responsible for reading all sensors periodically.
 * Obeys the 'measure_en' flag from the Control Word.
 */
static void aqm_sensor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Sensor Task Started");

    // 1. Register this task's handle so the ISR knows who to wake up
    sensor_task_handle = xTaskGetCurrentTaskHandle();

    ads1115_enable_rdy_pin(ADS1115_ADDR_1);
    ads1115_enable_rdy_pin(ADS1115_ADDR_2);

    // 2. Configure GPIOs for ADC RDY pins and attach the ISR handler
    gpio_isr_handler_add(ADC_1_RDY_PIN, aqm_adc_rdy_isr_handler, NULL);
    gpio_isr_handler_add(ADC_2_RDY_PIN, aqm_adc_rdy_isr_handler, NULL);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SENSOR_TASK_DELAY_MS);
    
    // 3. Main loop: Read sensors and update Modbus registers
    while(1) {
        
        // --- Conditionally Read Sensors based on Control Word ---
        if (aqm_data.control_word.flags.measure_en == 1) {
            aqm_read_all_adc_channels(); 
            aqm_calculate_gas_concentrations(); 
            aqm_sen55_read_measurements();

            aqm_print_measured_values();
        }
        
        // --- Always Update Modbus Registers ---
        // Processes incoming CW changes from Modbus Master and updates status
        aqm_modbus_update_registers();
        
        // Sleep until exactly the defined frequency has passed
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// -----------------------------------------------------------------------------
// TASK INITIALIZATION
// -----------------------------------------------------------------------------

/**
 * @brief Spawns all system background tasks
 */
void aqm_tasks_start(void) {
    // Create the output task. 
    // Stack size: 2048 bytes, Priority: 5 (Standard priority)
    xTaskCreate(aqm_output_task, "output_task", 2048, NULL, 5, NULL);
    
    // Create the sensor task.
    // Stack size: 4096 bytes (due to I2C and sensor reading), Priority: 6 (Higher than output task)
    xTaskCreate(aqm_sensor_task, "sensor_task", 4096, NULL, 6, NULL);
}