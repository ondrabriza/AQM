#ifndef ADS1115_H
#define ADS1115_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

// --- I2C Addresses ---
// These addresses correspond to ADS1115_ADDR_1 (0x48) and ADS1115_ADDR_2 (0x49) from aqm_config.h
#define ADS1115_ADDR_GND 0x48 // ADDR pin connected to GND
#define ADS1115_ADDR_VDD 0x49 // ADDR pin connected to VDD
#define ADS1115_ADDR_SDA 0x4A // ADDR pin connected to SDA
#define ADS1115_ADDR_SCL 0x4B // ADDR pin connected to SCL

// --- ADS1115 Registers ---
#define ADS1115_REG_CONVERSION 0x00
#define ADS1115_REG_CONFIG     0x01
#define ADS1115_REG_LO_THRESH  0x02
#define ADS1115_REG_HI_THRESH  0x03

// --- Configuration Enums ---

// Input Multiplexer (MUX) configuration
typedef enum {
    ADS1115_MUX_DIFF_0_1 = 0x00, // Differential AIN0 - AIN1
    ADS1115_MUX_DIFF_0_3 = 0x01, // Differential AIN0 - AIN3
    ADS1115_MUX_DIFF_1_3 = 0x02, // Differential AIN1 - AIN3
    ADS1115_MUX_DIFF_2_3 = 0x03, // Differential AIN2 - AIN3
    ADS1115_MUX_SINGLE_0 = 0x04, // Single-ended AIN0 - GND
    ADS1115_MUX_SINGLE_1 = 0x05, // Single-ended AIN1 - GND
    ADS1115_MUX_SINGLE_2 = 0x06, // Single-ended AIN2 - GND
    ADS1115_MUX_SINGLE_3 = 0x07  // Single-ended AIN3 - GND
} ads1115_mux_t;

// Programmable Gain Amplifier (PGA) configuration
typedef enum {
    ADS1115_PGA_6_144V   = 0x00, // +/- 6.144V 
    ADS1115_PGA_4_096V   = 0x01, // +/- 4.096V 
    ADS1115_PGA_2_048V   = 0x02, // +/- 2.048V (Default)
    ADS1115_PGA_1_024V   = 0x03, // +/- 1.024V
    ADS1115_PGA_0_512V   = 0x04, // +/- 0.512V
    ADS1115_PGA_0_256V   = 0x05  // +/- 0.256V
} ads1115_pga_t;

// Data Rate configuration
typedef enum {
    ADS1115_DR_8SPS      = 0x00,
    ADS1115_DR_16SPS     = 0x01,
    ADS1115_DR_32SPS     = 0x02,
    ADS1115_DR_64SPS     = 0x03,
    ADS1115_DR_128SPS    = 0x04, // Default
    ADS1115_DR_250SPS    = 0x05,
    ADS1115_DR_475SPS    = 0x06,
    ADS1115_DR_860SPS    = 0x07
} ads1115_data_rate_t;


// --- API Functions ---

/**
 * @brief Configures the ALERT/RDY pin to act as a conversion ready indicator.
 * @note The ALERT/RDY pin requires an external pull-up resistor (e.g., 10k to 3.3V).
 * @param i2c_addr I2C address of the ADS1115 device
 * @return ESP_OK on success
 */
esp_err_t ads1115_enable_rdy_pin(uint8_t i2c_addr);

/**
 * @brief Sets the configuration and starts a single measurement (Single-shot mode).
 * @param i2c_addr I2C address of the ADS1115 device
 * @param mux Channel or differential pair selection
 * @param pga Voltage range selection
 * @param rate Sampling rate
 * @return ESP_OK on success
 */
esp_err_t ads1115_request_measurement(uint8_t i2c_addr, ads1115_mux_t mux, ads1115_pga_t pga, ads1115_data_rate_t rate);

/**
 * @brief Reads the measurement result. Reading the data clears the RDY pin (returns to HIGH).
 * @param i2c_addr I2C address of the ADS1115 device
 * @param adc_value Pointer to store the raw 16-bit signed ADC value
 * @return ESP_OK on success
 */
esp_err_t ads1115_read_adc(uint8_t i2c_addr, int16_t *adc_value);

/**
 * @brief Converts the raw ADC value to voltage in millivolts based on the PGA setting.
 * @param adc_value Raw value obtained from ads1115_read_adc
 * @param pga The PGA range used during the measurement
 * @return Voltage in millivolts [mV]
 */
float ads1115_compute_mv(int16_t adc_value, ads1115_pga_t pga);


/**
 * @brief Synchronously reads a single-ended channel from the ADS1115
 * @param i2c_addr Address of the specific ADS1115 chip
 * @param channel Channel number (0 to 3)
 * @param pga Gain setting
 * @param rate Data rate
 * @param out_val Pointer to store the raw ADC result
 */
esp_err_t ads1115_read_single_channel(uint8_t i2c_addr, uint8_t channel, ads1115_pga_t pga, ads1115_data_rate_t rate, int16_t *out_val);



/**
 * @brief Non-blocking function. Requests a single measurement and returns immediately.
 */
esp_err_t ads1115_start_single_ended_conversion(uint8_t i2c_addr, uint8_t channel, ads1115_pga_t pga, ads1115_data_rate_t rate);


#endif // ADS1115_H