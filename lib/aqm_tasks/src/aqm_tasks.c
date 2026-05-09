#include "aqm_datastore.h"
#include "aqm_modbus_reg.h"
#include "aqm_modbus.h"
#include "aqm_config.h"
#include "aqm_tasks.h"
#include "aqm_gpio.h"
#include "aqm_i2c.h"
#include "ads1115.h"
#include "sen5x_i2c.h"
#include "aqm_mics6814.h"
#include "sen55.h"

#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define OUTPUT_TASK_DELAY_MS 100 // Delay for output task loop
#define AQM_ADC_MAX_WAIT_TIME_MS 150 // Max time to wait for ADC conversion before timeout
#define SENSOR_TASK_DELAY_MS 1000 // Delay for sensor reading task loop (1 second)

   
static TaskHandle_t sensor_task_handle = NULL;
TaskHandle_t factory_reset_task_handle = NULL; // Global handle for factory reset task

static uint8_t sen55_initialized = 0;

static const char *TAG = "AQM_TASKS";

static uint32_t last_relay_switch_time = 0;
static uint32_t last_bad_air_time = 0;


static volatile bool is_warmup_finished = false;
static void warmup_timer_callback(void* arg) {
    is_warmup_finished = true;
    ESP_LOGI(TAG, "Warm-up done! Sensors should be stabilized now.");
}
static void start_sensor_warmup_timer(void) {
    const esp_timer_create_args_t warmup_timer_args = {
        .callback = &warmup_timer_callback,
        .name = "warmup_timer"
    };
    esp_timer_handle_t warmup_timer;
    ESP_ERROR_CHECK(esp_timer_create(&warmup_timer_args, &warmup_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(warmup_timer, START_UP_TIME_MS * 1000));
}

// Output task - handles hardware control and NVS saving
static void aqm_output_task(void *pvParameters) {
    ESP_LOGI(TAG, "Output Task Started");

   

    while(1){

        // Check if there was any change in the Control Word

            
            //ESP_LOGI(TAG, "Control Word change detected. Processing hardware updates...");

            // 1. Evaluate Relay State (Intent vs Reality)
            if (aqm_data.config.control_word.flags.relay_state != aqm_data.data.status.status_word.flags.relay_state) {
                if (aqm_data.config.control_word.flags.relay_state == 1) {
                    ESP_LOGI(TAG, "CW Command: Relay ON");
                    if (aqm_relay_turn_on() == ESP_OK) {
                        aqm_data.data.status.status_word.flags.relay_state = 1; // Update actual status
                    }
                } else {
                    ESP_LOGI(TAG, "CW Command: Relay OFF");
                    if (aqm_relay_turn_off() == ESP_OK) {
                        aqm_data.data.status.status_word.flags.relay_state = 0;
                    }
                }
            }

            // 2. Evaluate LED State (Intent vs Reality)
            if (aqm_data.config.control_word.flags.led_state != aqm_data.data.status.status_word.flags.led_state) {
                if (aqm_data.config.control_word.flags.led_state == 1) {
                    ESP_LOGI(TAG, "CW Command: LED ON");
                    if (aqm_led_turn_on() == ESP_OK) {
                        aqm_data.data.status.status_word.flags.led_state = 1;
                    }
                } else {
                    ESP_LOGI(TAG, "CW Command: LED OFF");
                    if (aqm_led_turn_off() == ESP_OK) {
                        aqm_data.data.status.status_word.flags.led_state = 0;
                    }
                }
            }

            // 3. Save the new configuration to NVS memory
            //ESP_LOGI(TAG, "Saving updated Control Word to NVS.");
            //aqm_control_word_save_nvs(); 
            
            // 4. Clear the flag so we don't process or save again until the next change
            aqm_data.config.control_word.flags.cw_changed = 0;

            // Check if reboot is needed
            uint8_t needs_reboot = 0;
            
            // Wi-Fi state change
            if (!aqm_data.config.control_word.flags.wifi_en && aqm_data.data.status.status_word.flags.wifi_en) {
                ESP_LOGW(TAG, "Wi-Fi disable requested.");
                needs_reboot = 1;
            }
            
            if (aqm_data.config.control_word.flags.wifi_en && !aqm_data.data.status.status_word.flags.wifi_en) {
                 ESP_LOGW(TAG, "Wi-Fi enable requested.");
                 needs_reboot = 1;
            }

            /*if (aqm_data.config.control_word.flags.mb_tcp_en && !aqm_data.data.status.status_word.flags.mb_tcp_en) {
                ESP_LOGW(TAG, "Modbus TCP enable requested.");
                needs_reboot = 1;
            }

            if (!aqm_data.config.control_word.flags.mb_tcp_en && aqm_data.data.status.status_word.flags.mb_tcp_en) {
                ESP_LOGW(TAG, "Modbus TCP disable requested.");
                needs_reboot = 1;
            }*/

            if (needs_reboot) {
                ESP_LOGW(TAG, "Critical configuration changed! Rebooting in 3 seconds...");
                
                // Wait before restart to allow Modbus to send response
                aqm_mics_config_save_nvs();
                aqm_control_word_save_nvs();
                aqm_wifi_config_save_nvs();

                vTaskDelay(pdMS_TO_TICKS(3000)); 
                esp_restart();
            }

        

        // Delay to yield to other RTOS tasks
        vTaskDelay(pdMS_TO_TICKS(OUTPUT_TASK_DELAY_MS));
    }
}

// -----------------------------------------------------------------------------
// SENSOR MEASUREMENT TASK (ADC & SEN55)
// -----------------------------------------------------------------------------

// ISR for ADC RDY pins
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

    if (err1 == ESP_OK) aqm_data.data.adc_raw.so2_raw_val = (uint16_t)val1;
    if (err2 == ESP_OK) aqm_data.data.adc_raw.mics_red_raw_val = (uint16_t)val2;

    // ==========================================
    // PAIR 2: 3.3V (Chip 1) and NH3 (Chip 2)
    // ==========================================
    ads1115_start_single_ended_conversion(ADS1115_ADDR_1, CHANNEL_3V3, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);
    ads1115_start_single_ended_conversion(ADS1115_ADDR_2, CHANNEL_NH3_VAL, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);

    err1 = aqm_adc_wait_and_read(ADS1115_ADDR_1, &val1);
    err2 = aqm_adc_wait_and_read(ADS1115_ADDR_2, &val2);

    if (err1 == ESP_OK) {
        aqm_data.data.adc_raw.v3v3_raw_val = (uint16_t)val1;
        aqm_data.data.status.v3v3_mv = (uint16_t)(ads1115_compute_mv(val1, ADS1115_PGA_4_096V));
    }
    if (err2 == ESP_OK) aqm_data.data.adc_raw.mics_nh3_raw_val = (uint16_t)val2;

    // ==========================================
    // PAIR 3: H2S (Chip 1) and NO2 (Chip 2)
    // ==========================================
    ads1115_start_single_ended_conversion(ADS1115_ADDR_1, CHANNEL_SGX_H2S, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);
    ads1115_start_single_ended_conversion(ADS1115_ADDR_2, CHANNEL_NO2_VAL, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);

    err1 = aqm_adc_wait_and_read(ADS1115_ADDR_1, &val1);
    err2 = aqm_adc_wait_and_read(ADS1115_ADDR_2, &val2);

    if (err1 == ESP_OK) aqm_data.data.adc_raw.h2s_raw_val = (uint16_t)val1;
    if (err2 == ESP_OK) aqm_data.data.adc_raw.mics_ox_raw_val = (uint16_t)val2;

    // ==========================================
    // REMAINING CHANNEL: 5.0V (Chip 1) 
    // ==========================================
    ads1115_start_single_ended_conversion(ADS1115_ADDR_1, CHANNEL_5V, ADS1115_PGA_4_096V, ADS1115_DR_8SPS);
    
    err1 = aqm_adc_wait_and_read(ADS1115_ADDR_1, &val1);
    
    if (err1 == ESP_OK) {
        aqm_data.data.adc_raw.v5v_raw_val = (uint16_t)val1;
        aqm_data.data.status.v5v_mv = (uint16_t)(ads1115_compute_mv(val1, ADS1115_PGA_4_096V) * 2); 
    }

    aqm_data.data.status.timestamp = pdTICKS_TO_MS(xTaskGetTickCount()); 
}

static void aqm_calculate_sgx_gas_concentrations(void) {
    aqm_data.data.gases.so2_ppm = (uint16_t)(((ads1115_compute_mv(aqm_data.data.adc_raw.so2_raw_val, ADS1115_PGA_4_096V) / SENSITIVITY_SO2_MV_PER_PPM) * 10.0f) + 0.5f); // 0.5f added for rounding to nearest integer when retyping to uint16_t
    aqm_data.data.gases.h2s_ppm = (uint16_t)(((ads1115_compute_mv(aqm_data.data.adc_raw.h2s_raw_val, ADS1115_PGA_4_096V) / SENSITIVITY_H2S_MV_PER_PPM) * 10.0f) + 0.5f);
}

static void aqm_calculate_sgx_aqi(void) {
    aqm_data.data.aqi_data.so2_aqi = 1;
    aqm_data.data.aqi_data.h2s_aqi = 1;
    if (aqm_data.data.gases.so2_ppm > LIMIT_SO2*10) aqm_data.data.aqi_data.so2_aqi = 4;
    if (aqm_data.data.gases.h2s_ppm > LIMIT_H2S*10) aqm_data.data.aqi_data.h2s_aqi = 4;
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
        aqm_data.data.sen55.pm1_0  = (pm1p0 == 0xFFFF) ? 0 : pm1p0;
        aqm_data.data.sen55.pm2_5  = (pm2p5 == 0xFFFF) ? 0 : pm2p5; 
        aqm_data.data.sen55.pm4_0  = (pm4p0 == 0xFFFF) ? 0 : pm4p0;
        aqm_data.data.sen55.pm10_0 = (pm10p0 == 0xFFFF) ? 0 : pm10p0;
        
        
        aqm_data.data.sen55.humidity    = (humidity == 0x7FFF) ? 0 : humidity; 
        aqm_data.data.sen55.temperature = (temperature == 0x7FFF) ? 0 : temperature; 
        aqm_data.data.sen55.voc_index   = (voc_index == 0x7FFF) ? 0 : voc_index; 
        aqm_data.data.sen55.nox_index   = (nox_index == 0x7FFF) ? 0 : nox_index; 
    }
}


static void aqm_print_measured_values(void) {

    ESP_LOGI(TAG, "=== Sensor Readings at %llu ms ===", aqm_data.data.status.timestamp);

    ESP_LOGI(TAG, "SEN55: PM1.0: %.1f, PM2.5: %.1f, PM4.0: %.1f, PM10.0: %.1f [ug/m3]", 
            aqm_data.data.sen55.pm1_0/10.0f, aqm_data.data.sen55.pm2_5/10.0f, aqm_data.data.sen55.pm4_0/10.0f, aqm_data.data.sen55.pm10_0/10.0f);

    ESP_LOGI(TAG, "Env: Temp: %.1f°C, Hum: %.1f%%, VOC: %.1f, NOx: %.1f", 
            aqm_data.data.sen55.temperature / 200.0f, aqm_data.data.sen55.humidity / 100.0f, aqm_data.data.sen55.voc_index/10.0f, aqm_data.data.sen55.nox_index/10.0f);

    ESP_LOGI(TAG, "Gases: SO2: %.1f ppm, H2S: %.1f ppm, MICS_RED: %.2f Idx, MICS_NH3: %.2f Idx, MICS_OX: %.2f Idx", 
            aqm_data.data.gases.so2_ppm/10.0f, aqm_data.data.gases.h2s_ppm/10.0f, aqm_data.data.gases.mics_red/100.0f, aqm_data.data.gases.mics_nh3/100.0f, aqm_data.data.gases.mics_ox/100.0f);

    ESP_LOGI(TAG, "MICS R0: RED: %lu, NH3: %lu, OX: %lu", 
            aqm_data.config.mics_r0.red_r0, aqm_data.config.mics_r0.nh3_r0, aqm_data.config.mics_r0.ox_r0);

    ESP_LOGI(TAG, " MICS Thresholds: RED: %.2f, NH3: %.2f, OX: %.2f", 
            aqm_data.config.mics_thresholds.red_threshold/100.0f, aqm_data.config.mics_thresholds.nh3_threshold/100.0f, aqm_data.config.mics_thresholds.ox_threshold/100.0f);

    ESP_LOGI(TAG, "Status: 3.3V: %u mV, 5.0V: %u mV", aqm_data.data.status.v3v3_mv, aqm_data.data.status.v5v_mv);

    ESP_LOGI(TAG, "AQI: SO2: %u, H2S: %u, MICS_RED: %u, MICS_NH3: %u, MICS_OX: %u, PM: %u, VOC: %u, NOx: %u, Global AQI: %u", 
            aqm_data.data.aqi_data.so2_aqi, aqm_data.data.aqi_data.h2s_aqi, aqm_data.data.aqi_data.mics_red_aqi, aqm_data.data.aqi_data.mics_nh3_aqi, aqm_data.data.aqi_data.mics_ox_aqi,
            aqm_data.data.aqi_data.pm_aqi, aqm_data.data.aqi_data.voc_aqi, aqm_data.data.aqi_data.nox_aqi, aqm_data.data.aqi_data.global_aqi);

}

static void aqm_evaluate_aqi(void) {

        uint16_t max_gas_aqi = 0;
        if (is_warmup_finished)
        {
            if (aqm_data.data.aqi_data.so2_aqi > max_gas_aqi) max_gas_aqi = aqm_data.data.aqi_data.so2_aqi;
            if (aqm_data.data.aqi_data.h2s_aqi > max_gas_aqi) max_gas_aqi = aqm_data.data.aqi_data.h2s_aqi;
            if (aqm_data.data.aqi_data.mics_red_aqi > max_gas_aqi) max_gas_aqi = aqm_data.data.aqi_data.mics_red_aqi;
            if (aqm_data.data.aqi_data.mics_nh3_aqi > max_gas_aqi) max_gas_aqi = aqm_data.data.aqi_data.mics_nh3_aqi;
            if (aqm_data.data.aqi_data.pm_aqi > max_gas_aqi) max_gas_aqi = aqm_data.data.aqi_data.pm_aqi;
            if (aqm_data.data.aqi_data.voc_aqi > max_gas_aqi) max_gas_aqi = aqm_data.data.aqi_data.voc_aqi;
            if (aqm_data.data.aqi_data.nox_aqi > max_gas_aqi) max_gas_aqi = aqm_data.data.aqi_data.nox_aqi;
            
            //if (aqm_data.data.aqi_data.mics_ox_aqi > max_gas_aqi) max_gas_aqi = aqm_data.data.aqi_data.mics_ox_aqi;
            
        }

        // Overall AQI is the maximum of all individual AQIs
        aqm_data.data.aqi_data.global_aqi = max_gas_aqi;

}



static void aqm_control_relay_by_aqi(void) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    if (last_relay_switch_time == 0) {
        last_relay_switch_time = current_time;
    }

    uint32_t time_since_switch = current_time - last_relay_switch_time;
    
    bool should_be_blocked = (aqm_data.data.aqi_data.global_aqi >= 3);


    if (should_be_blocked) {
        last_bad_air_time = current_time; 
    }

    bool is_ventilation_blocked = (aqm_data.config.control_word.flags.relay_state == 1);


    if (!is_ventilation_blocked) {
        // --- ZAVÍRÁME VĚTRÁNÍ ---
        // Podmínka: Vzduch je špatný A ZÁROVEŇ jsme větrali dostatečně dlouho
        if (should_be_blocked && (time_since_switch > MIN_VENTING_TIME_MS)) {
            
            // Nastavíme záměr do Control Wordu
            aqm_data.config.control_word.flags.relay_state = 1; 
            last_relay_switch_time = current_time;
            
            if (aqm_data.data.aqi_data.global_aqi == 4) {
                ESP_LOGI(TAG, "AQI=4; (Extreme pollution). Ventilation blocked!");
            } else {
                ESP_LOGI(TAG, "AQI=3; Ventilation blocked!");
            }
        }
    } else {
        uint32_t clean_air_duration = current_time - last_bad_air_time;
        
        // Podmínka: Vzduch je čistý (AQI 1 nebo 2) A ZÁROVEŇ je čistý už 5 minut 
        // A ZÁROVEŇ jsme byli zablokováni alespoň minimální požadovanou dobu
        if (!should_be_blocked && 
            (clean_air_duration > CLEAN_AIR_HYSTERESIS_MS) && 
            (time_since_switch > MIN_BLOCKED_TIME_MS)) {
            

            aqm_data.config.control_word.flags.relay_state = 0; 
            last_relay_switch_time = current_time;
            
            ESP_LOGI(TAG, "AQI < 3; Clean air detected for 5 minutes. Ventilation unblocked.");
        }
    }
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

    start_sensor_warmup_timer();
    const TickType_t xFrequency = pdMS_TO_TICKS(SENSOR_TASK_DELAY_MS);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    // 3. Main loop: Read sensors and update Modbus registers
    while(1) {
        // Sleep until exactly the defined frequency has passed
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        
        // --- Conditionally Read Sensors based on Control Word ---
        if (aqm_data.config.control_word.flags.measure_en == 1) {
            aqm_read_all_adc_channels(); 
            aqm_sen55_read_measurements();
            aqm_calculate_sgx_gas_concentrations(); 
            aqm_calculate_data_from_mics();

            aqm_calculate_sgx_aqi();
            aqm_calculate_aqi_mics();
            aqm_calculate_sen55_aqi();

            aqm_evaluate_aqi();

            aqm_control_relay_by_aqi();



            aqm_print_measured_values();
        }
        
        // --- Always Update Modbus Registers ---
        // Processes incoming CW changes from Modbus Master and updates status
        //aqm_modbus_update_registers();
        
    }
}


// Task for safe factory reset execution outside ISR
static void factory_reset_task(void *pvParameters) {
    while (1) {
        // Task sleeps and waits until it receives a signal from ISR
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGW(TAG, "Long press detected. Performing factory reset...");

        aqm_datastore_fill_nvs_with_defaults(); // Fills NVS with default values for control word and Wi-Fi config

        ESP_LOGW(TAG, "Factory reset applied. Restarting device in 1 second...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
}

/**
 * @brief Task that wakes up every 12 hours to commit data to NVS.
 */
static void aqm_periodic_nvs_save_task(void *pvParameters) {

    ESP_LOGI(TAG, "Periodic NVS Save Task Started (12h interval)");
    const TickType_t save_interval = pdMS_TO_TICKS(12 * 60 * 60 * 1000);

    while(1) {
        
        ESP_LOGI(TAG, "12 hours passed. Executing routine NVS save...");

        aqm_mics_config_save_nvs();
        aqm_control_word_save_nvs();
        aqm_wifi_config_save_nvs();
        
        //ESP_LOGW(TAG, "Remaining unused task memory: %d bytes", uxTaskGetStackHighWaterMark(NULL));

        vTaskDelay(save_interval);
    }
}


// Task initialization
void aqm_tasks_start(void) {
    // Create output task
    xTaskCreate(aqm_output_task, "output_task", 2048, NULL, 5, NULL);

    // Create factory reset task
    xTaskCreate(factory_reset_task, "factory_reset_task", 2048, NULL, 7, &factory_reset_task_handle);

    xTaskCreate(aqm_periodic_nvs_save_task, "nvs_save_task", 2560, NULL, 4, NULL);

    
    // Create the sensor task.
    // Stack size: 6144 bytes (due to I2C and sensor reading), Priority: 6 (Higher than output task)
    xTaskCreate(aqm_sensor_task, "sensor_task", 6144, NULL, 6, NULL);

}