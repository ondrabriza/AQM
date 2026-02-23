#include "aqm_config.h"
#include "modbus_reg.h"
#include "aqm_modbus.h"
#include "aqm_i2c.h"

#include "ads1115.h"
#include "sen55.h"
#include "aqm_wifi.h"
#include "aqm_gpio.h"

#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

static const char *TAG = "MAIN";

void app_main() {

    esp_err_t err = aqm_gpio_intialize();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPIOs");
        return;
    }
    
    if (i2c_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C");
        return;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);


    // Initialize Wi-Fi and connect to the Access Point
    aqm_wifi_connect();
    aqm_init_modbus();

    // Initialize sensors and Modbus communication here
}