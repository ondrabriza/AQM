#ifndef AQM_MICS6814_H
#define AQM_MICS6814_H

#include <stdint.h>
#include "ads1115.h"

typedef enum{
    MICS_RED = 0,
    MICS_OX = 1,
    MICS_NH3 = 2
}aqm_mics_element_t;

void aqm_calculate_mics_r(uint16_t mics_adc_raw, ads1115_pga_t pga, uint16_t voltage_in_mv, uint32_t r_load, uint32_t *out_r);

void aqm_calculate_data_from_mics(void);

/**
 * @brief Calculates the ratio. The ratio is calculated as (r_value / r0_value) * 100. And stored as an integer with 2 decimal places (e.g. 1.05 is stored as 105).
 * 
 * @param r_value 
 * @param r0_value 
 * @param out_ratio 
 */
void aqm_calculate_mics_ratio(uint32_t r_value, uint32_t r0_value, uint16_t *out_ratio);

/**
 * @brief Calculates the R0 baseline. The new R0 is calculated as (alpha * r_value) + ((1.0f - alpha) * current_r0).
 * 
 * @param r_value 
 * @param current_r0 
 * @param alpha 
 * @param out_r0 
 */
void aqm_calculate_r0_baseline(const uint32_t r_value, const uint32_t current_r0, const float alpha, uint32_t *out_r0);

/**
 * @brief Updates the R0 baselines for each MICS element.
 * 
 */
void aqm_update_mics_r0_baselines(void);


void aqm_filter_mics_data(void);

/**
 * @brief Performs temperature compensation on the MICS resistance value.
 * 
 * @param r_value - The resistance value to be compensated
 * @param temperature_c - The current temperature in Celsius
 * @param element - The MICS element for which the compensation is being calculated (RED, OX, NH3)
 * @param out_compensated_r - The output pointer where the compensated resistance value will be stored
 */
void aqm_mics_temp_compensate_r(uint32_t r_value, float temperature_c, aqm_mics_element_t element, uint32_t *out_compensated_r);

/**
 * @brief Calculates the ratios for each MICS element.
 * 
 */
void aqm_calculate_mics_ratios(void);

/**
 * @brief Calculates the AQI based on MICS sensor data.
 * 
 */
void aqm_calculate_aqi_mics(void);

#endif // AQM_MICS6814_H