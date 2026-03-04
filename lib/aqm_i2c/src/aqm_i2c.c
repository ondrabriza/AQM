#include "aqm_i2c.h"
#include "aqm_config.h"
#include "ads1115.h"
#include "aqm_datastore.h"

#include <esp_err.h>
#include <esp_log.h>


static const char *TAG = "AQM_I2C";


esp_err_t i2c_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed");
        return err;
    }

    err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed");
    } else {
        ESP_LOGI(TAG, "I2C initialized successfully (100kHz)");
    }
    return err;
}

esp_err_t i2c_free(void) {
    return i2c_driver_delete(I2C_MASTER_NUM);
}

esp_err_t i2c_read(uint8_t address, uint8_t* data, uint16_t count) {
    return i2c_master_read_from_device(I2C_MASTER_NUM, address, data, count, 
                                                pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}

esp_err_t i2c_write(uint8_t address, const uint8_t* data, uint16_t count) {
    return i2c_master_write_to_device(I2C_MASTER_NUM, address, data, count, 
                                               pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}
