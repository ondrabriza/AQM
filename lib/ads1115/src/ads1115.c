#include "ads1115.h"
#include "aqm_i2c.h"   // Include your I2C read/write wrapper functions
#include "aqm_config.h" // Include your global config
#include "esp_log.h"

static const char *TAG = "ADS1115_DRV";

// --- Low-level I2C helpers ---

static esp_err_t ads1115_write_reg(uint8_t i2c_addr, uint8_t reg, uint16_t value) {
    uint8_t buf[3];
    buf[0] = reg;                     // Address Pointer Register
    buf[1] = (uint8_t)(value >> 8);   // MSB of data
    buf[2] = (uint8_t)(value & 0xFF); // LSB of data
    return i2c_write(i2c_addr, buf, 3);
}

static esp_err_t ads1115_read_reg(uint8_t i2c_addr, uint8_t reg, uint16_t *value) {
    // 1. Write to Address Pointer Register to specify which register we want to read
    esp_err_t err = i2c_write(i2c_addr, &reg, 1); 
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write pointer reg for 0x%02X", i2c_addr);
        return err;
    }

    // 2. Read 2 bytes of data from the specified register
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

// --- Public API Implementation ---

esp_err_t ads1115_enable_rdy_pin(uint8_t i2c_addr) {
    esp_err_t err;
    
    // To configure the ALERT/RDY pin as a conversion ready indicator,
    // the MSB of the High Threshold register must be 1.
    err = ads1115_write_reg(i2c_addr, ADS1115_REG_HI_THRESH, 0x8000);
    if (err != ESP_OK) return err;

    // And the MSB of the Low Threshold register must be 0.
    err = ads1115_write_reg(i2c_addr, ADS1115_REG_LO_THRESH, 0x0000);
    return err;
}

esp_err_t ads1115_request_measurement(uint8_t i2c_addr, ads1115_mux_t mux, ads1115_pga_t pga, ads1115_data_rate_t rate) {
    uint16_t config = 0x0000;

    // Bit 15: OS (Operational status/single-shot conversion start) = 1 to start
    config |= (1 << 15);
    
    // Bits 14-12: MUX (Input multiplexer configuration)
    config |= (mux & 0x07) << 12;
    
    // Bits 11-9: PGA (Programmable gain amplifier configuration)
    config |= (pga & 0x07) << 9;
    
    // Bit 8: MODE (Device operating mode) = 1 for Single-shot mode
    config |= (1 << 8);
    
    // Bits 7-5: DR (Data rate)
    config |= (rate & 0x07) << 5;
    
    // COMP_MODE (Bit 4) = 0 (Traditional comparator)
    // COMP_POL (Bit 3)  = 0 (Active low - ALERT/RDY pin goes low when ready)
    // COMP_LAT (Bit 2)  = 0 (Non-latching comparator)
    
    // Bits 1-0: COMP_QUE (Comparator queue and disable)
    // Set to '00' to assert the ALERT/RDY pin after one conversion
    // This is mandatory for the RDY pin to function!
    // (Other bits remain 0 by default initialization)
    
    return ads1115_write_reg(i2c_addr, ADS1115_REG_CONFIG, config);
}

esp_err_t ads1115_read_adc(uint8_t i2c_addr, int16_t *adc_value) {
    uint16_t raw_value = 0;
    
    // Reading the conversion register will automatically clear the ALERT/RDY pin
    esp_err_t err = ads1115_read_reg(i2c_addr, ADS1115_REG_CONVERSION, &raw_value);
    if (err == ESP_OK) {
        *adc_value = (int16_t)raw_value;
    }
    return err;
}

float ads1115_compute_mv(int16_t adc_value, ads1115_pga_t pga) {
    float multiplier = 0.0f;
    
    // Determine the voltage per bit (LSB size) based on the PGA setting
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