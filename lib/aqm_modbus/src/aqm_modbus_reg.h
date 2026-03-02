#ifndef MODBUS_REG_H
#define MODBUS_REG_H

#include <stdint.h>

// Bit masks for Control Word (Holding Reg)
#define MASK_LED_CONTROL    (1 << 0)  // Bit 0
#define MASK_RELAY_ENABLE   (1 << 1)  // Bit 1

// Bit masks for Status Word (Input Reg)
#define MASK_STATUS_WIFI    (1 << 0)  // Bit 0
#define MASK_STATUS_RELAY   (1 << 1)  // Bit 1


/**
 * @brief HOLDING REGISTERS (Read/Write) - Function 03, 06, 16
 */
typedef struct {
    uint16_t control_word; /**< Reg 0: Bit 0=LED, Bit 1=Relay */
    uint16_t test_val;     /**< Reg 1: Test register */
} holding_reg_params_t;

/**
 * @brief INPUT REGISTERS (Read Only) - Function 04
 * @note All float values are scaled to uint16_t. Divide by the scale factor on the Master side.
 * For negative temperatures, the Master must interpret the uint16_t as a signed int16_t (2's complement).
 */
typedef struct {
    // --- System Status ---
    uint16_t status_word;  /**< Reg 0: Bit 0=WiFi, Bit 1=Relay */
    
    // --- RAW ADC counts (Scale: 1) ---
    uint16_t so2_raw_val;  /**< Reg 1: SO2 Raw ADC count */
    uint16_t v3v3_raw_val; /**< Reg 2: 3.3V Raw ADC count */
    uint16_t v5v_raw_val;  /**< Reg 3: 5V Raw ADC count */
    uint16_t h2s_raw_val;  /**< Reg 4: H2S Raw ADC count */
    uint16_t co_raw_val;   /**< Reg 5: CO Raw ADC count */
    uint16_t nh3_raw_val;  /**< Reg 6: NH3 Raw ADC count */
    uint16_t no2_raw_val;  /**< Reg 7: NO2 Raw ADC count */

    // --- Calculated Gas PPM (Scale: 100 -> 2 decimal place) ---
    // Example: Master reads 125 -> 1.25 ppm
    uint16_t so2_ppm;      /**< Reg 8: SO2 concentration (PPM * 100) */
    uint16_t h2s_ppm;      /**< Reg 9: H2S concentration (PPM * 100) */
    uint16_t co_ppm;       /**< Reg 10: CO concentration (PPM * 100) */
    uint16_t nh3_ppm;      /**< Reg 11: NH3 concentration (PPM * 100) */
    uint16_t no2_ppm;      /**< Reg 12: NO2 concentration (PPM * 100) */

    // --- Voltage Rails (Scale: 1000 -> 3 decimal places) ---
    // Example: Master reads 3315 -> 3.315 V
    uint16_t v3v3_val;     /**< Reg 13: 3.3V Rail Voltage (V * 1000 = mV) */
    uint16_t v5v_val;      /**< Reg 14: 5.0V Rail Voltage (V * 1000 = mV) */

    // --- SEN55 Climate Data (Scale: 10 -> 1 decimal place) ---
    uint16_t temperature;  /**< Reg 15: Temperature (C * 10). E.g. 245 = 24.5 C */
    uint16_t humidity;     /**< Reg 16: Relative Humidity (% * 10) */
    
    // --- SEN55 PM & Index Data (Scale: 1) ---
    uint16_t pm1_0;        /**< Reg 17: PM 1.0 (ug/m3) */
    uint16_t pm2_5;        /**< Reg 18: PM 2.5 (ug/m3) */
    uint16_t pm4_0;        /**< Reg 19: PM 4.0 (ug/m3) */
    uint16_t pm10_0;       /**< Reg 20: PM 10.0 (ug/m3) */
    uint16_t voc_index;    /**< Reg 21: VOC Index (0-500) */
    uint16_t nox_index;    /**< Reg 22: NOx Index (0-500) */

    uint16_t uptime_sec_hi;   /**< Reg 23: System uptime in seconds */
    uint16_t uptime_sec_lo;   /**< Reg 24: System uptime in seconds (lower 16 bits) */

} input_reg_params_t;


extern holding_reg_params_t holding_reg_params;
extern input_reg_params_t input_reg_params;

#endif // MODBUS_REG_H