#include "aqm_mics6814.h"
#include "aqm_datastore.h"
#include "aqm_config.h"
#include "ads1115.h"


#define ALPHA_CLEANING   0.005f   
#define ALPHA_POLLUTING  0.0005f

void calculate_mics_r(uint16_t mics_adc_raw, ads1115_pga_t pga, uint16_t voltage_in_mv, uint32_t r_load, uint32_t *out_r) {
    
    float v_out_mv = ads1115_compute_mv(mics_adc_raw, pga);

    
    if (v_out_mv >= (float)voltage_in_mv) {
        *out_r = 0xFFFFFFFF;
        return;
    }

    //  Rs = R_load * (V_out / (V_in - V_out))
    float rs_calc = (float)r_load * (v_out_mv / ((float)voltage_in_mv - v_out_mv));

    *out_r = (uint32_t)(rs_calc + 0.5f);
}

void aqm_calculate_data_from_mics(void){
    
    calculate_mics_r(aqm_data.data.adc_raw.mics_red_raw_val, ADS1115_PGA_4_096V, aqm_data.data.status.v3v3_mv, MICS_RED_LOAD, &aqm_data.data.gases.mics_red_r);
    calculate_mics_r(aqm_data.data.adc_raw.mics_ox_raw_val, ADS1115_PGA_4_096V, aqm_data.data.status.v3v3_mv, MICS_OX_LOAD, &aqm_data.data.gases.mics_ox_r);
    calculate_mics_r(aqm_data.data.adc_raw.mics_nh3_raw_val, ADS1115_PGA_4_096V, aqm_data.data.status.v3v3_mv, MICS_NH3_LOAD, &aqm_data.data.gases.mics_nh3_r);

    
    // division by 0 safety check
    if (aqm_data.config.mics_r0.red_r0 > 0 && aqm_data.config.mics_r0.ox_r0 > 0 && aqm_data.config.mics_r0.nh3_r0 > 0) {
        
        aqm_data.data.gases.mics_red = (uint16_t)(((float)aqm_data.data.gases.mics_red_r / aqm_data.config.mics_r0.red_r0)*100.0f);
        aqm_data.data.gases.mics_ox = (uint16_t)(((float)aqm_data.data.gases.mics_ox_r / aqm_data.config.mics_r0.ox_r0)*100.0f);
        aqm_data.data.gases.mics_nh3 = (uint16_t)(((float)aqm_data.data.gases.mics_nh3_r / aqm_data.config.mics_r0.nh3_r0)*100.0f);
    }
    
    aqm_update_mics_r0_baselines();

}

void aqm_update_mics_r0_baselines(void) {

    // RED: Clean air = HIGH Resistance. Gas detection = DECREASE in resistance.
    if(aqm_data.data.gases.mics_red > aqm_data.config.mics_thresholds.red_threshold) {
        float alpha_red = (aqm_data.data.gases.mics_red_r > aqm_data.config.mics_r0.red_r0) ? ALPHA_CLEANING : ALPHA_POLLUTING;
        aqm_data.config.mics_r0.red_r0 = (uint32_t)((aqm_data.data.gases.mics_red_r * alpha_red) + (aqm_data.config.mics_r0.red_r0 * (1.0f - alpha_red)) + 0.5f); // +0.5f for rounding
    }
    
    // NH3: Clean air = HIGH Resistance. Gas detection = DECREASE in resistance.
    if(aqm_data.data.gases.mics_nh3 > aqm_data.config.mics_thresholds.nh3_threshold) {
        float alpha_nh3 = (aqm_data.data.gases.mics_nh3_r > aqm_data.config.mics_r0.nh3_r0) ? ALPHA_CLEANING : ALPHA_POLLUTING;
        aqm_data.config.mics_r0.nh3_r0 = (uint32_t)((aqm_data.data.gases.mics_nh3_r * alpha_nh3) + (aqm_data.config.mics_r0.nh3_r0 * (1.0f - alpha_nh3)) + 0.5f);
    }

    // OX: Clean air = LOW Resistance. Gas detection = INCREASE in resistance.
    if(aqm_data.data.gases.mics_ox < aqm_data.config.mics_thresholds.ox_threshold) {
    
        float alpha_ox = (aqm_data.data.gases.mics_ox_r < aqm_data.config.mics_r0.ox_r0) ? ALPHA_CLEANING : ALPHA_POLLUTING;
        aqm_data.config.mics_r0.ox_r0 = (uint32_t)((aqm_data.data.gases.mics_ox_r * alpha_ox) + (aqm_data.config.mics_r0.ox_r0 * (1.0f - alpha_ox)) + 0.5f);
    }

}


