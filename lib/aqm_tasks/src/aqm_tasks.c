#include "aqm_datastore.h"
#include "aqm_modbus_reg.h"
#include "aqm_modbus.h"

#include "aqm_config.h"
#include "aqm_tasks.h"
#include "aqm_gpio.h"
#include "aqm_i2c.h"
#include "ads1115.h"

#include <esp_log.h>

#include "aqm_tasks.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


#define OUTPUT_TASK_DELAY_MS 100 // Delay for output task loop
#define AQM_ADC_MAX_WAIT_TIME_MS 150 // Max time to wait for ADC conversion before timeout
#define SENSOR_TASK_DELAY_MS 1000 // Delay for sensor reading task loop (5 seconds)
static TaskHandle_t sensor_task_handle = NULL;

const char *TAG = "AQM_TASKS";

/**
 * @brief Processes commands incoming from the Modbus Master (Holding Registers)
 * Should be called periodically in the main Modbus or Tasks loop.
 */
static void aqm_output_task(void *pvParameters) {
    ESP_LOGI(TAG, "Output Task Started");

    while(1){
        // 1. Determine the REQUESTED relay state from the Modbus holding register (1 = ON, 0 = OFF)
        uint8_t requested_relay_state = (holding_reg_params.control_word & MASK_RELAY_CONTROL) ? 1 : 0;
        
        // Perform the action only if the requested state DIFFERS from the current state in the datastore
        if (requested_relay_state != aqm_data.status.relay_active) {
            if (requested_relay_state == 1) {
                ESP_LOGI(TAG, "Modbus command: Relay ON");
                if (aqm_relay_turn_on() == ESP_OK) {
                    aqm_data.status.relay_active = 1;
                }
            } else {
                ESP_LOGI(TAG, "Modbus command: Relay OFF");
                if (aqm_relay_turn_off() == ESP_OK) {
                    aqm_data.status.relay_active = 0;
                }
            }
        }

        // 2. Determine the REQUESTED LED state from the Modbus holding register
        uint8_t requested_led_state = (holding_reg_params.control_word & MASK_LED_CONTROL) ? 1 : 0;
        
        // Again, perform the action only upon a state change
        if (requested_led_state != aqm_data.status.led_active) {
            if (requested_led_state == 1) {
                ESP_LOGI(TAG, "Modbus command: LED ON");
                if (aqm_led_turn_on() == ESP_OK) {
                    aqm_data.status.led_active = 1;
                }
            } else {
                ESP_LOGI(TAG, "Modbus command: LED OFF");
                if (aqm_led_turn_off() == ESP_OK) {
                    aqm_data.status.led_active = 0;
                }
            }
        }

        // Update input registers to reflect any changes in status
        aqm_modbus_update_registers(); 

        vTaskDelay(pdMS_TO_TICKS(OUTPUT_TASK_DELAY_MS));
    }
}


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
    // Sleep the task until ISR notifies us (or 50ms timeout occurs).
    // pdFALSE decrements the notification count by 1 (does not clear it completely).
    
    uint32_t notified = ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(AQM_ADC_MAX_WAIT_TIME_MS));

    if (notified == 0) {
        ESP_LOGE(TAG, "Conversion timeout! ISR didn't trigger for I2C 0x%02X", i2c_addr);
        return ESP_ERR_TIMEOUT;
    }
   /*while( gpio_get_level( (i2c_addr == ADS1115_ADDR_1) ? ADC_1_RDY_PIN : ADC_2_RDY_PIN ) == 1 ) {
        // Wait for the pin to go LOW (Active Low)
        vTaskDelay(pdMS_TO_TICKS(10)); // Poll every 10ms
    }
    ESP_LOGI(TAG, "RDY pin triggered for I2C 0x%02X", i2c_addr);*/

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
    //ESP_LOGI(TAG, "Starting concurrent ADC conversions for SO2 and CO channels");
    ads1115_start_single_ended_conversion(ADS1115_ADDR_1, CHANNEL_SGX_SO2, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);
    ads1115_start_single_ended_conversion(ADS1115_ADDR_2, CHANNEL_CO_VAL, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);

    //ESP_LOGI(TAG, "Waiting for ADC conversions to complete...");
    // Wait for both chips to trigger the ISR and then read the results
    err1 = aqm_adc_wait_and_read(ADS1115_ADDR_1, &val1);
    err2 = aqm_adc_wait_and_read(ADS1115_ADDR_2, &val2);

    //ESP_LOGI(TAG, "ADC conversions completed. Processing results...");
    if (err1 == ESP_OK) aqm_data.adc_raw.so2_raw_val = (uint16_t)val1;
    if (err2 == ESP_OK) aqm_data.adc_raw.co_raw_val = (uint16_t)val2;
    //ESP_LOGI(TAG, "SO2 raw value: %d, CO raw value: %d", val1, val2);


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
        aqm_data.status.v5v_val = (uint16_t)(ads1115_compute_mv(val1, ADS1115_PGA_4_096V) * 2); // 5V rail is measured via a voltage divider, so we multiply by 2 to get the actual voltage
    }

    aqm_data.status.uptime_sec = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ); // Update uptime in seconds

    ESP_LOGI(TAG, "ADC reading cycle completed.");
    ESP_LOGI(TAG, "SO2 raw: %d, H2S raw: %d, CO raw: %d, NH3 raw: %d, NO2 raw: %d, 3.3V raw: %d, 5V raw: %d", 
             aqm_data.adc_raw.so2_raw_val,
             aqm_data.adc_raw.h2s_raw_val,
             aqm_data.adc_raw.co_raw_val,
             aqm_data.adc_raw.nh3_raw_val,
             aqm_data.adc_raw.no2_raw_val,
             aqm_data.adc_raw.v3v3_raw_val,
             aqm_data.adc_raw.v5v_raw_val);
}

static void aqm_calculate_gas_concentrations(void) {
    // Placeholder for actual conversion logic based on sensor calibration curves
    // For demonstration, we will just apply a dummy linear conversion

    aqm_data.gases.so2_ppm = ads1115_compute_mv(aqm_data.adc_raw.so2_raw_val, ADS1115_PGA_4_096V) / SENSITIVITY_SO2_MV_PER_PPM;
    aqm_data.gases.h2s_ppm = ads1115_compute_mv(aqm_data.adc_raw.h2s_raw_val, ADS1115_PGA_4_096V) / SENSITIVITY_H2S_MV_PER_PPM;

    aqm_data.gases.co_mv  = ads1115_compute_mv(aqm_data.adc_raw.co_raw_val, ADS1115_PGA_4_096V); // Replace with actual conversion
    aqm_data.gases.nh3_mv = ads1115_compute_mv(aqm_data.adc_raw.nh3_raw_val, ADS1115_PGA_4_096V);
    aqm_data.gases.no2_mv = ads1115_compute_mv(aqm_data.adc_raw.no2_raw_val, ADS1115_PGA_4_096V);
}

/**
 * @brief FreeRTOS Task responsible for reading all sensors periodically.
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

    // 3. 
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SENSOR_TASK_DELAY_MS);
    
    // 4. Main loop: Read sensors and update Modbus registers every second
    while(1) {
        
        // --- Read Data from ADC ---
        aqm_read_all_adc_channels(); 

        aqm_calculate_gas_concentrations(); // Calculate gas concentrations based on the latest ADC readings
        
        // --- Read Data from SEN55 (To be added) ---
        // aqm_sen55_read_measurements();
        
        // --- Update Modbus Registers ---
        // Push the freshly read data from the datastore to Modbus input registers
        aqm_modbus_update_registers();
        
        // --- Sleep until exactly 1 second (xFrequency) has passed since the last loop start ---
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}






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
    
    // We will add the sensor task here later: xTaskCreate(aqm_sensor_task, ...);
}