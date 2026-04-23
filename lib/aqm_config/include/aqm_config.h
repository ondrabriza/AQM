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

#define MICS_OX_BASE_R   40000 
#define MICS_NH3_BASE_R  100000
#define MICS_RED_BASE_R  500000

#define MICS_OX_THRESHOLD 90 // 0,9 ratio
#define MICS_NH3_THRESHOLD 90 // 0,9 ratio
#define MICS_RED_THRESHOLD 110 // 1,1 ratio



// Fallback Access Point settings
#define AQM_AP_SSID          "AQM_Sensor_Setup"
#define AQM_AP_PASS          "12345678" // Min. 8 characters
#define AQM_CONFIG_PORT      80
#define AQM_MAX_RETRY        5
#define AQM_WIFI_SSID        "AQM_Sensor" // Default Wi-Fi SSID (used if NVS is empty)
#define AQM_WIFI_PASS        "123456789" // Default Wi-Fi Password (used if NVS is empty)


#endif // AQM_CONFIG_H