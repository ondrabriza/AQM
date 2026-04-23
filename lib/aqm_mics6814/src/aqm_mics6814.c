#include "aqm_mics6814.h"
#include "aqm_datastore.h"
#include "aqm_config.h"
#include "ads1115.h"

#include <esp_log.h>

#define RATIO_MIN 90
#define RATIO_MAX 110
#define ALPHA_R0 0.001f
#define ALPHA_RS 0.2f

static const char *TAG = "AQM_MICS6814";

void aqm_calculate_mics_r(uint16_t mics_adc_raw, ads1115_pga_t pga, uint16_t voltage_in_mv, uint32_t r_load, uint32_t *out_r) {
    
    float v_out_mv = ads1115_compute_mv(mics_adc_raw, pga);

    
    if (v_out_mv >= (float)voltage_in_mv) {
        *out_r = 0xFFFFFFFF;
        return;
    }

    //  Rs = R_load * (V_out / (V_in - V_out))
    float rs_calc = (float)r_load * (v_out_mv / ((float)voltage_in_mv - v_out_mv));

    *out_r = (uint32_t)(rs_calc + 0.5f);
}



void aqm_calculate_mics_ratio(uint32_t r_value, uint32_t r0_value, uint16_t *out_ratio) {
    if (r0_value == 0) {
        *out_ratio = 0;
        return;
    }

    float ratio = ((float)r_value / (float)r0_value) * 100.0f; // Ratio in percentage

    *out_ratio = (uint16_t)(ratio + 0.5f); // Round to nearest integer
}

void aqm_calculate_r0_baseline(const uint32_t r_value, const uint32_t current_r0, const float alpha, uint32_t *out_r0) {
    float new_r0 = (alpha * (float)r_value) + ((1.0f - alpha) * (float)current_r0);
    *out_r0 = (uint32_t)(new_r0 + 0.5f); // Round to nearest integer
}

void aqm_update_mics_r0_baselines(void) {
    
    // RED: Clean air = HIGH Resistance. Gas detection = DECREASE in resistance.
    if(aqm_data.data.gases.mics_red >= RATIO_MIN && aqm_data.data.gases.mics_red <= RATIO_MAX) {
        aqm_calculate_r0_baseline(aqm_data.data.gases.mics_red_r, aqm_data.config.mics_r0.red_r0, ALPHA_R0, &aqm_data.config.mics_r0.red_r0);
    }
    
    // NH3: Clean air = HIGH Resistance. Gas detection = DECREASE in resistance.
    if(aqm_data.data.gases.mics_nh3 >= RATIO_MIN && aqm_data.data.gases.mics_nh3 <= RATIO_MAX){
        aqm_calculate_r0_baseline(aqm_data.data.gases.mics_nh3_r, aqm_data.config.mics_r0.nh3_r0, ALPHA_R0, &aqm_data.config.mics_r0.nh3_r0);
    }

    // OX: Clean air = LOW Resistance. Gas detection = INCREASE in resistance.
    if(aqm_data.data.gases.mics_ox >= RATIO_MIN && aqm_data.data.gases.mics_ox <= RATIO_MAX) {
        aqm_calculate_r0_baseline(aqm_data.data.gases.mics_ox_r, aqm_data.config.mics_r0.ox_r0, ALPHA_R0, &aqm_data.config.mics_r0.ox_r0);
    }

}

void aqm_filter_mics_r(uint32_t last_r, uint32_t r_value, float alpha, uint32_t *out_filtered_r) {
    float new_r = (alpha * (float)r_value) + ((1.0f - alpha) * (float)last_r);
    *out_filtered_r = (uint32_t)(new_r + 0.5f); // Round to nearest integer
}



void aqm_calculate_data_from_mics(void){

    uint32_t red_value, ox_value, nh3_value;
    aqm_calculate_mics_r(aqm_data.data.adc_raw.mics_red_raw_val, ADS1115_PGA_4_096V, aqm_data.data.status.v3v3_mv, MICS_RED_LOAD, &red_value);
    aqm_calculate_mics_r(aqm_data.data.adc_raw.mics_ox_raw_val, ADS1115_PGA_4_096V, aqm_data.data.status.v3v3_mv, MICS_OX_LOAD, &ox_value);
    aqm_calculate_mics_r(aqm_data.data.adc_raw.mics_nh3_raw_val, ADS1115_PGA_4_096V, aqm_data.data.status.v3v3_mv, MICS_NH3_LOAD, &nh3_value);

    
    aqm_data.data.gases.mics_red_r = red_value;
    aqm_data.data.gases.mics_ox_r = ox_value;
    aqm_data.data.gases.mics_nh3_r = nh3_value;

    /*
    aqm_filter_mics_r(aqm_data.data.gases.mics_red_r, red_value, ALPHA_RS, &aqm_data.data.gases.mics_red_r);
    aqm_filter_mics_r(aqm_data.data.gases.mics_ox_r, ox_value, ALPHA_RS, &aqm_data.data.gases.mics_ox_r);
    aqm_filter_mics_r(aqm_data.data.gases.mics_nh3_r, nh3_value, ALPHA_RS, &aqm_data.data.gases.mics_nh3_r);
    */
    

    aqm_calculate_mics_ratio(aqm_data.data.gases.mics_red_r, aqm_data.config.mics_r0.red_r0, &aqm_data.data.gases.mics_red);
    aqm_calculate_mics_ratio(aqm_data.data.gases.mics_ox_r, aqm_data.config.mics_r0.ox_r0, &aqm_data.data.gases.mics_ox);
    aqm_calculate_mics_ratio(aqm_data.data.gases.mics_nh3_r, aqm_data.config.mics_r0.nh3_r0, &aqm_data.data.gases.mics_nh3);
    
    aqm_update_mics_r0_baselines();

}
