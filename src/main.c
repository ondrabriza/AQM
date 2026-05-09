#include "aqm_config.h"
#include "aqm_i2c.h"
#include "aqm_datastore.h"
#include "ads1115.h"
#include "sen55.h"
#include "aqm_wifi.h"
#include "aqm_modbus_reg.h"
#include "aqm_modbus.h"
#include "aqm_gpio.h"
#include "aqm_tasks.h"

#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "MAIN";

void app_main(void) {
    // 1. Initialize hardware (GPIOs, I2C)
    esp_err_t err = aqm_gpio_intialize();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPIOs");
        return;
    }
    
    if (i2c_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C");
        return;
    }

    // 2. Initialize NVS (Required to load configurations like Wi-Fi and Control Word)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 3. Initialize datastore
    aqm_datastore_init();
    
    // Copy the boot state of the Control Word into the Modbus holding registers
        // Try to load saved data from Flash memory
    aqm_wifi_config_load_nvs();
    aqm_mics_config_load_nvs();
    aqm_control_word_load_nvs();
    aqm_data.config.control_word.flags.relay_state = 0; // Force relay off on boot for safety, regardless of saved state

    //aqm_data.control_word.word = 0xFFFF;
    //aqm_data.control_word.flags.cw_changed = 0;

    //aqm_modbus_sync_from_nvs();

    ESP_LOGI(TAG, "Boot Control Word: 0x%04X", aqm_data.config.control_word.word);

    // 4. Start Modbus RTU (Independent of network status)
    
    if(aqm_init_modbus_rtu() == ESP_OK) {
        aqm_data.data.status.status_word.flags.mb_rtu_en = 1;
    } else {
        ESP_LOGE(TAG, "Failed to start Modbus RTU.");
        
    }

    // 5. Start Network (Wi-Fi)
    if (aqm_data.config.control_word.flags.wifi_en) {
        ESP_LOGI(TAG, "Wi-Fi is ENABLED in CW. Connecting...");
        
        // This runs asynchronously in the background
        aqm_wifi_connect(); 
        
        // Wait until Wi-Fi either connects (STA) or falls back to AP portal
        // (Assumes s_wifi_event_group is globally accessible from aqm_wifi.h)
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_AP_BIT,
                pdFALSE, pdFALSE, portMAX_DELAY);

        if ((bits & WIFI_CONNECTED_BIT) | (bits & WIFI_AP_BIT)) {
            ESP_LOGI(TAG, "Wi-Fi setup complete. Proceeding with application.");
            aqm_data.data.status.status_word.flags.wifi_en = 1;


        
        }

        // 6. Start Modbus TCP (Only if network is up and enabled in CW)
        if (aqm_data.config.control_word.flags.mb_tcp_en && aqm_data.data.status.status_word.flags.wifi_en) {
            ESP_LOGI(TAG, "Modbus TCP is ENABLED in CW. Starting...");
            err = aqm_init_modbus_tcp(); // Uncomment once TCP init is fully implemented
            if (err == ESP_OK)
            {
                aqm_data.data.status.status_word.flags.mb_tcp_en = 1;
            }
        
        // Must be after ModBus TCP, otherwise does not work
        if (aqm_data.data.status.status_word.flags.wifi_en){
            // Start HTTP server for provisioning
            start_web_server();
            start_mdns_service(); // aqm.local
        }

            

        } else {
            ESP_LOGI(TAG, "Modbus TCP is DISABLED.");
            aqm_data.config.control_word.flags.mb_tcp_en = 0;
        }

    } else {
        ESP_LOGI(TAG, "Wi-Fi is DISABLED in CW. Running completely offline.");
    }

    // 7. Start background tasks (Sensors and Output control)
    aqm_tasks_start();

    // 8. Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); 

    }
}