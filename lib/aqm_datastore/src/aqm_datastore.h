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
    
    uint16_t mics_red_raw_val;  
    uint16_t mics_ox_raw_val; 
    uint16_t mics_nh3_raw_val; 
    uint16_t reserved; // Padding to align to 16-bit boundary
} __attribute__((packed)) aqm_adc_raw_t;

/**
 * @brief Calculated gas concentrations
 */
typedef struct {
    uint16_t so2_ppm;
    uint16_t h2s_ppm;

    uint16_t mics_ox;
    uint16_t mics_red;
    uint16_t mics_nh3;
    uint16_t reserved; // Padding to align to 16-bit boundary

    uint32_t mics_ox_r;
    uint32_t mics_red_r;
    uint32_t mics_nh3_r;

} __attribute__((packed)) aqm_gas_data_t;

typedef struct {
    uint32_t mics_ox_r0;
    uint32_t mics_red_r0;
    uint32_t mics_nh3_r0;
} __attribute__((packed)) aqm_mics_r0_t;

/**
 * @brief Data from SEN55 Environmental Sensor
 */
typedef struct {
    uint16_t pm1_0;
    uint16_t pm2_5;
    uint16_t pm4_0;
    uint16_t pm10_0;
    int16_t humidity;
    int16_t temperature;
    int16_t voc_index;
    int16_t nox_index;
} __attribute__((packed)) aqm_sen55_data_t;

typedef struct {
    uint16_t measure_en    : 1; // Bit  0: Measuring enabled
    uint16_t wifi_en       : 1; // Bit  1: Wi-Fi enabled
    uint16_t relay_state   : 1; // Bit  2: Control of relay state (0 = off, 1 = on)
    uint16_t led_state     : 1; // Bit  3: Control of LED state
    uint16_t web_server_en : 1; // Bit  4: Web server in STA mode enabled
    uint16_t mb_tcp_en     : 1; // Bit  5: Modbus TCP enabled
    uint16_t mb_rtu_en     : 1; // Bit  6: Modbus RTU enabled
    uint16_t cw_changed    : 1; // Bit  7: Internal flag to indicate that the control word was changed
    uint16_t reserved      : 8; // Bits 8-15: Reserved for future features
} __attribute__((packed)) aqm_flags_t; 

/**
 * @brief Control Word (16-bit) for setting the behavior of the device.
 * It is stored in NVS and accessible via Modbus and Web.
 */
typedef union {
    aqm_flags_t flags; // Access individual bits
    uint16_t word; // 16bit word for easy storage and Modbus mapping
} aqm_control_word_t;

/**
 * @brief Wi-Fi configuration structure. (Credentials)
 */
typedef struct {
    char wifi_ssid[32];     // Target SSID
    char wifi_pass[64];     // Target Password
} aqm_wifi_config_t;

/**
 * @brief System status, connectivity info, Voltage rails, etc.
 */
typedef struct {
    aqm_control_word_t status_word; // Bit 0=WiFi, Bit 1=Relay, Bit 2=LED
    uint16_t reserved; // Padding to align to 16-bit boundary
    uint64_t timestamp;
    uint16_t v3v3_mv;        /**< Voltage on 3V3 rail in mV */
    uint16_t v5v_mv;         /**< Voltage on 5V rail in mV */

} __attribute__((packed)) aqm_system_status_t;

typedef struct {

} aqm_mics_ppm_t;

typedef struct {
    aqm_control_word_t control_word; // Control bits for device behavior
    uint16_t reserved; // Padding to align to 16-bit boundary
    aqm_mics_r0_t mics_r0; // Baseline resistances for MICS sensors
} __attribute__((packed)) aqm_holding_reg_t;

typedef struct {
    aqm_system_status_t status; 
    aqm_sen55_data_t sen55;     
    aqm_gas_data_t gases;       
    aqm_adc_raw_t adc_raw;    
} __attribute__((packed)) aqm_input_reg_t;

/**
 * @brief Main Data Storage Structure
 */
typedef struct {
    aqm_holding_reg_t config; // For Modbus Holding Registers (e.g. Control Word)
    aqm_input_reg_t data;   // For Modbus Input Registers (Status, Gas Data, etc.)
    aqm_wifi_config_t wifi_config;

    //aqm_adc_raw_t adc_raw;      
    //aqm_gas_data_t gases;       
    //aqm_sen55_data_t sen55;     
    //aqm_system_status_t status; 
    //aqm_control_word_t control_word; // Control bits for device behavior
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
 * @brief Save control word to NVS.
 */
void aqm_control_word_save_nvs(void);

/**
 * @brief Load control word from NVS.
 */
void aqm_control_word_load_nvs(void);

/**
 * @brief Fills the datastore with dummy/test data.
 * Useful for testing Modbus and UI without physical sensors.
 */
void aqm_datastore_fill_test_data(void);

/**
 * @brief Fills the NVS with default values.
 * 
 */
void aqm_datastore_fill_nvs_with_defaults(void);


void aqm_datastore_set_flag_cw_changed(void);

#endif // AQM_DATASTORE_H