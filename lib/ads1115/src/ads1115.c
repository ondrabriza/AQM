#include "ads1115.h"
#include "aqm_i2c.h"   // Include your I2C read/write wrapper functions
#include "aqm_config.h" // Include your global config
#include "driver/gpio.h"

#include <esp_log.h>

static const char *TAG = "ADS1115_DRV";


// --- Low-level I2C helpers ---

static esp_err_t ads1115_write_reg(uint8_t i2c_addr, uint8_t reg, uint16_t value) {
    uint8_t buf[3];
    buf[0] = reg;                    // Address Pointer Register
    buf[1] = (uint8_t)(value >> 8);  // MSB of data
    buf[2] = (uint8_t)(value & 0xFF); // LSB of data
    return i2c_write(i2c_addr, buf, 3);
}

static esp_err_t ads1115_read_reg(uint8_t i2c_addr, uint8_t reg, uint16_t *value) {
    // 1. Write to Address Pointer Register
    esp_err_t err = i2c_write(i2c_addr, &reg, 1); 
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write pointer reg for 0x%02X", i2c_addr);
        return err;
    }

    // 2. Read 2 bytes of data
    uint8_t buf[2] = {0};
    err = i2c_read(i2c_addr, buf, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data for 0x%02X", i2c_addr);
        return err;
    }

    // 3. Combine MSB and LSB
    *value = (buf[0] << 8) | buf[1];
    return ESP_OK;
}


// --- Public Application API ---

esp_err_t ads1115_enable_rdy_pin(uint8_t i2c_addr) {
    esp_err_t err;
    // MSB of High Threshold must be 1
    err = ads1115_write_reg(i2c_addr, ADS1115_REG_HI_THRESH, 0x8000);
    if (err != ESP_OK) return err;
    // MSB of Low Threshold must be 0
    err = ads1115_write_reg(i2c_addr, ADS1115_REG_LO_THRESH, 0x0000);
    return err;
}

esp_err_t ads1115_request_measurement(uint8_t i2c_addr, ads1115_mux_t mux, ads1115_pga_t pga, ads1115_data_rate_t rate) {
    uint16_t config = 0x0000;
    config |= (1 << 15);                  // OS = 1 (Start single-shot conversion)
    config |= (mux & 0x07) << 12;         // MUX
    config |= (pga & 0x07) << 9;          // PGA
    config |= (1 << 8);                   // MODE = 1 (Single-shot)
    config |= (rate & 0x07) << 5;         // DR
    // COMP_MODE, COMP_POL, COMP_LAT = 0
    // COMP_QUE = '00' (Assert ALERT/RDY pin after one conversion)
    return ads1115_write_reg(i2c_addr, ADS1115_REG_CONFIG, config);
}

esp_err_t ads1115_read_adc(uint8_t i2c_addr, int16_t *adc_value) {
    uint16_t raw_value = 0;
    // Reading the conversion register automatically clears the RDY pin back to HIGH
    esp_err_t err = ads1115_read_reg(i2c_addr, ADS1115_REG_CONVERSION, &raw_value);
    if (err == ESP_OK) {
        *adc_value = (int16_t)raw_value;
    }
    return err;
}

// --- Concurrent Measurement API ---

esp_err_t ads1115_start_single_ended_conversion(uint8_t i2c_addr, uint8_t channel, ads1115_pga_t pga, ads1115_data_rate_t rate) {
    if (channel > 3) return ESP_ERR_INVALID_ARG;
    
    // Calculate single-ended MUX based on the requested channel
    ads1115_mux_t mux = (ads1115_mux_t)(ADS1115_MUX_SINGLE_0 + channel);
    
    // Transmit request via I2C and return immediately (non-blocking)
    return ads1115_request_measurement(i2c_addr, mux, pga, rate);
}


float ads1115_compute_mv(int16_t adc_value, ads1115_pga_t pga) {
    float multiplier = 0.0f;
    switch (pga) {
        case ADS1115_PGA_6_144V: multiplier = 0.1875f; break;
        case ADS1115_PGA_4_096V: multiplier = 0.125f;  break;
        case ADS1115_PGA_2_048V: multiplier = 0.0625f; break;
        case ADS1115_PGA_1_024V: multiplier = 0.03125f; break;
        case ADS1115_PGA_0_512V: multiplier = 0.015625f; break;
        case ADS1115_PGA_0_256V: multiplier = 0.0078125f; break;
        default: multiplier = 0.0f; break;
    }
    return adc_value * multiplier;
}