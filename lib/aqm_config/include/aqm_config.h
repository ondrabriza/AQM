#ifndef AQM_CONFIG_H
#define AQM_CONFIG_H


#define LED_PIN 45

#define BOOT_BUTTON_PIN 46

#define RS485_D_PIN 9
#define RS485_DE_PIN 10
#define RS485_R_PIN 11

#define RELAY_PIN 12

#define I2C_SDA_PIN 47
#define I2C_SCL_PIN 21

#define ADC_1_RDY_PIN 14
#define ADC_2_RDY_PIN 13

#define SEN5X_I2C_ADDRESS 0x69 

#define ADS1115_ADDR_1 0x48
#define ADS1115_ADDR_2 0x49

#define CHANNEL_SGX_SO2 0   // AIN00
#define CHANNEL_3V3     1   // AIN01
#define CHANNEL_5V      2   // AIN02
#define CHANNEL_SGX_H2S 3   // AIN03

#define CHANNEL_CO_VAL  1   // AIN11
#define CHANNEL_NH3_VAL 2   // AIN12
#define CHANNEL_NO2_VAL 3   // AIN13

#define SENSITIVITY_H2S_MV_PER_PPM 26.4f
#define SENSITIVITY_SO2_MV_PER_PPM 120.0f


#define MICS_OX_LOAD  20000
#define MICS_NH3_LOAD 120000
#define MICS_RED_LOAD 390000

#define MICS_WARMUP_TIME_MS 60000 // 60 secs

#define BUFFER_SIZE 60

#define MICS_OX_BASE_R   40000 
#define MICS_NH3_BASE_R  100000
#define MICS_RED_BASE_R  500000

#define MICS_OX_THRESHOLD 90 // 0,9 ratio
#define MICS_NH3_THRESHOLD 90 // 0,9 ratio
#define MICS_RED_THRESHOLD 110 // 1,1 ratio


#define PM_AQI2                50.0f   // ug/m3
#define PM_AQI3                100.0f  // ug/m3
#define PM_AQI4                400.0f  // ug/m3

#define LIMIT_SO2               1.5f    // ppm
#define LIMIT_H2S               1.5f    // ppm

#define LIMIT_VOC_AQI2         150.0f  // Index
#define LIMIT_VOC_AQI3         300.0f  // Index
#define LIMIT_VOC_AQI4         400.0f  // Index

#define LIMIT_NOX_AQI2         25.0f  // Index
#define LIMIT_NOX_AQI3         50.0f // Index
#define LIMIT_NOX_AQI4         150.0f // Index



#define MICS_RED_NH3_AQI2     0.75f   // Below 75% for 60 seconds
#define MICS_RED_NH3_AQI3     0.65f   // Below 65% for 30 seconds
#define MICS_RED_NH3_AQI4     0.50f   // Below 50% for 5 seconds

// OX
#define MICS_OX_AQI2          3.00f   // Increase above 300% for 60 seconds
#define MICS_OX_AQI3          4.00f   // Increase above 400% for 30 seconds
#define MICS_OX_AQI4          5.00f   // Increase above 500% for 5 seconds


#define START_UP_TIME_MS   (2 * 60 * 1000)  // 
#define MIN_BLOCKED_TIME_MS     (10 * 60 * 1000) // Větrání musí zůstat vypnuté min. 10 minut
#define MIN_VENTING_TIME_MS     (5 * 60 * 1000)  // Větrání musí zůstat zapnuté min. 5 minut
#define CLEAN_AIR_HYSTERESIS_MS (5 * 60 * 1000)  // Vzduch musí být čistý 5 minut v kuse, než se vyvětrá

#define PERSISTENCE_THRESHOLD_SGX 15



// Fallback Access Point settings
#define AQM_AP_SSID          "AQM_Sensor_Setup"
#define AQM_AP_PASS          "12345678" // Min. 8 characters
#define AQM_CONFIG_PORT      80
#define AQM_MAX_RETRY        5
#define AQM_WIFI_SSID        "AQM_Sensor" // Default Wi-Fi SSID (used if NVS is empty)
#define AQM_WIFI_PASS        "123456789" // Default Wi-Fi Password (used if NVS is empty)


#include <stdint.h>

typedef struct {
    float values[BUFFER_SIZE];
    uint16_t head;
    uint16_t count;
} ring_buffer_t;

void push_to_buffer(ring_buffer_t *buf, float value);

float get_recent_average(ring_buffer_t *buf, uint16_t samples);

#endif // AQM_CONFIG_H