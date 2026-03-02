#ifndef AQM_DATASTORE_H
#define AQM_DATASTORE_H

#include <stdint.h>

/**
 * @brief Raw ADC values from ADCs
 */
typedef struct {
    uint16_t so2_raw_val; 
    uint16_t v3v3_raw_val;
    uint16_t v5v_raw_val; 
    uint16_t h2s_raw_val; 
    
    uint16_t co_raw_val;  
    uint16_t nh3_raw_val; 
    uint16_t no2_raw_val; 
} aqm_adc_raw_t;

/**
 * @brief Calculated gas concentrations
 */
typedef struct {
    float so2_ppm;
    float h2s_ppm;

    float co_ppm;
    float nh3_ppm;
    float no2_ppm;
} aqm_gas_data_t;

/**
 * @brief Data from SEN55 Environmental Sensor
 */
typedef struct {
    float pm1_0;
    float pm2_5;
    float pm4_0;
    float pm10_0;
    float humidity;
    float temperature;
    float voc_index;
    float nox_index;
} aqm_sen55_data_t;

/**
 * @brief System status, connectivity info, Voltage rails, etc.
 */
typedef struct {
    uint8_t wifi_connected;
    uint32_t uptime_sec;
    uint8_t relay_active;
    uint8_t led_active;
    float v3v3_val;        // Voltage on 3.3V rail (V)
    float v5v_val;         // Voltage on 5.0V rail (V)

} aqm_system_status_t;

/**
 * @brief Wi-Fi configuration structure. (Credentials)
 */
typedef struct {
    char wifi_ssid[32];     // Target SSID
    char wifi_pass[64];     // Target Password
} aqm_wifi_config_t;

/**
 * @brief Main Data Storage Structure
 */
typedef struct {
    aqm_adc_raw_t adc_raw;      
    aqm_gas_data_t gases;       
    aqm_sen55_data_t sen55;     
    aqm_system_status_t status; 
    aqm_wifi_config_t wifi_config;
} aqm_data_t;

extern aqm_data_t aqm_data;

void aqm_datastore_init(void);

/**
 * @brief Save wifi credentials to NVS.
 */
void aqm_wifi_config_save_nvs(void);

/**
 * @brief Load wifi credentials from NVS.
 */
void aqm_wifi_config_load_nvs(void);

/**
 * @brief Fills the datastore with dummy/test data.
 * Useful for testing Modbus and UI without physical sensors.
 */
void aqm_datastore_fill_test_data(void);

#endif // AQM_DATASTORE_H