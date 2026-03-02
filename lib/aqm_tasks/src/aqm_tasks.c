#include "aqm_datastore.h"
#include "aqm_modbus_reg.h"
#include "aqm_modbus.h"

#include "aqm_gpio.h"
#include "aqm_config.h"
#include <esp_log.h>

#include "aqm_tasks.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


#define OUTPUT_TASK_DELAY_MS 100 // Delay for output task loop

const char *TAG = "AQM_TASKS";



/**
 * @brief Processes commands incoming from the Modbus Master (Holding Registers)
 * Should be called periodically in the main Modbus or Tasks loop.
 */
static void aqm_output_task(void *pvParameters) {
    // We check the control bits to determine actions.

    while(1){

        // Check Relay Control Bit
        if (holding_reg_params.control_word & MASK_RELAY_CONTROL) {
            ESP_LOGI(TAG, "Modbus command: Relay ON");
            if(aqm_relay_turn_on() == ESP_OK) {
                aqm_data.status.relay_active = 1;
            }
        } else {
            ESP_LOGI(TAG, "Modbus command: Relay OFF");
            if(aqm_relay_turn_off() == ESP_OK) {
                aqm_data.status.relay_active = 0;
            }
        }

        // Check LED Control Bit
        if (holding_reg_params.control_word & MASK_LED_CONTROL) {
            ESP_LOGI(TAG, "Modbus command: LED ON");
            if(aqm_led_turn_on() == ESP_OK) {
                aqm_data.status.led_active = 1;
            }
        } else {
            ESP_LOGI(TAG, "Modbus command: LED OFF");
            if(aqm_led_turn_off() == ESP_OK) {
                aqm_data.status.led_active = 0;
            }
        }

        aqm_modbus_update_registers(); // Update input registers to reflect any changes in status

        vTaskDelay(pdMS_TO_TICKS(OUTPUT_TASK_DELAY_MS));
    }
}



/**
 * @brief Spawns all system background tasks
 */
void aqm_tasks_start(void) {
    // Create the output task. 
    // Stack size: 2048 bytes, Priority: 5 (Standard priority)
    xTaskCreate(aqm_output_task, "output_task", 2048, NULL, 5, NULL);
    
    // Zde později přidáme: xTaskCreate(aqm_sensor_task, ...);
}
