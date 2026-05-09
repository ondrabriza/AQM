#include "aqm_mics6814.h"
#include "aqm_datastore.h"
#include "aqm_config.h"
#include "ads1115.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define RATIO_MIN 90
#define RATIO_MAX 110
#define ALPHA_R0 0.001f
#define ALPHA_RS 0.2f

static const char *TAG = "MICS6814";




static ring_buffer_t nh3_buf = {0};
static ring_buffer_t ox_buf = {0};
static ring_buffer_t red_buf = {0};



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

    uint32_t red_r0_value = aqm_data.config.mics_r0.red_r0;
    uint32_t nh3_r0_value = aqm_data.config.mics_r0.nh3_r0;
    uint32_t ox_r0_value = aqm_data.config.mics_r0.ox_r0;

    
    // RED: Clean air = HIGH Resistance. Gas detection = DECREASE in resistance.
    if(aqm_data.data.gases.mics_red >= RATIO_MIN && aqm_data.data.gases.mics_red <= RATIO_MAX) {
        aqm_calculate_r0_baseline(aqm_data.data.gases.mics_red_r, aqm_data.config.mics_r0.red_r0, ALPHA_R0, &red_r0_value);
    }
    
    // NH3: Clean air = HIGH Resistance. Gas detection = DECREASE in resistance.
    if(aqm_data.data.gases.mics_nh3 >= RATIO_MIN && aqm_data.data.gases.mics_nh3 <= RATIO_MAX){
        aqm_calculate_r0_baseline(aqm_data.data.gases.mics_nh3_r, aqm_data.config.mics_r0.nh3_r0, ALPHA_R0, &nh3_r0_value);
    }

    // OX: Clean air = LOW Resistance. Gas detection = INCREASE in resistance.
    if(aqm_data.data.gases.mics_ox >= RATIO_MIN && aqm_data.data.gases.mics_ox <= RATIO_MAX) {
        aqm_calculate_r0_baseline(aqm_data.data.gases.mics_ox_r, aqm_data.config.mics_r0.ox_r0, ALPHA_R0, &ox_r0_value);
    }

    aqm_data.config.mics_r0.red_r0 = red_r0_value;
    aqm_data.config.mics_r0.ox_r0 = ox_r0_value;
    aqm_data.config.mics_r0.nh3_r0 = nh3_r0_value;

}

void aqm_filter_mics_r(uint32_t last_r, uint32_t r_value, float alpha, uint32_t *out_filtered_r) {




    float new_r = (alpha * (float)r_value) + ((1.0f - alpha) * (float)last_r);
    *out_filtered_r = (uint32_t)(new_r + 0.5f); // Round to nearest integer
}

void aqm_mics_temp_compensate_r(uint32_t r_value, float temperature_c, aqm_mics_element_t element, uint32_t *out_compensated_r) {

    // 1. Ochrana proti nesmyslným teplotám
    if (temperature_c < -20.0f || temperature_c > 70.0f) {
        *out_compensated_r = r_value;
        return;
    }
    /*
    Coeffs
    RED: a = 1.737197e+00, b = -1.609703e+02, c = -3.503991e+03, d = 7.446628e+05
    OX:  a = -2.480841e+00, b = 2.485417e+02, c = -8.704072e+03, d = 1.514469e+05
    NH3: a = -2.855777e+01, b = 3.345094e+03, c = -1.403178e+05, d = 2.504895e+06
    */
    float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f, R_exp_20 = 0.0f;

    /*float t_ref  = 20.0f;
    float t_ref2 = 400.0f;  // 20^2
    float t_ref3 = 8000.0f; // 20^3
    
    float R_exp_20 = (a * t_ref3) + (b * t_ref2) + (c * t_ref) + d;*/

    switch (element) {
        case MICS_RED:
            a = 1.737197; b = -160.9703; c = -3503.991; d = 744662.8;
            R_exp_20 = 624092.446;
            break;
        case MICS_OX:
            a = -2.480841; b = 248.5417; c = -8704.072; d = 151446.9;
            R_exp_20 = 56935.396;
            break;
        case MICS_NH3:
            a = -28.55777; b = 3345.094; c = -140317.8; d = 2504895;
            R_exp_20 = 808114.225;
            break;
        default:
            *out_compensated_r = r_value;
            return;
    }

    float t_act2 = temperature_c * temperature_c;
    float t_act3 = t_act2 * temperature_c;
    
    float R_exp_T = (a * t_act3) + (b * t_act2) + (c * temperature_c) + d;



    if (R_exp_T <= 1.0f || R_exp_20 <= 1.0f) {
        *out_compensated_r = r_value;
        return;
    }

    float compensation_factor = R_exp_T / R_exp_20;
    *out_compensated_r = (uint32_t)( ((float)r_value / compensation_factor) + 0.5f );
}

void aqm_calculate_data_from_mics(void){

    uint32_t red_r_value=0, ox_r_value=0, nh3_r_value=0;
    uint16_t red_ratio = 0, ox_ratio = 0, nh3_ratio = 0;

    aqm_calculate_mics_r(aqm_data.data.adc_raw.mics_red_raw_val, ADS1115_PGA_4_096V, aqm_data.data.status.v3v3_mv, MICS_RED_LOAD, &red_r_value);
    aqm_calculate_mics_r(aqm_data.data.adc_raw.mics_ox_raw_val, ADS1115_PGA_4_096V, aqm_data.data.status.v3v3_mv, MICS_OX_LOAD, &ox_r_value);
    aqm_calculate_mics_r(aqm_data.data.adc_raw.mics_nh3_raw_val, ADS1115_PGA_4_096V, aqm_data.data.status.v3v3_mv, MICS_NH3_LOAD, &nh3_r_value);

    //ESP_LOGI(TAG, "Calculated Rs values - RED: %u, OX: %u, NH3: %u", red_r_value, ox_r_value, nh3_r_value);


    aqm_mics_temp_compensate_r(red_r_value, aqm_data.data.sen55.temperature/200, MICS_RED, &red_r_value);
    aqm_mics_temp_compensate_r(ox_r_value, aqm_data.data.sen55.temperature/200, MICS_OX, &ox_r_value);
    aqm_mics_temp_compensate_r(nh3_r_value, aqm_data.data.sen55.temperature/200, MICS_NH3, &nh3_r_value);

    //ESP_LOGI(TAG, "Temperature compensated Rs values - RED: %u, OX: %u, NH3: %u", red_r_value, ox_r_value, nh3_r_value);


    aqm_data.data.gases.mics_red_r = red_r_value;
    aqm_data.data.gases.mics_ox_r = ox_r_value;
    aqm_data.data.gases.mics_nh3_r = nh3_r_value;

    /*
    aqm_filter_mics_r(aqm_data.data.gases.mics_red_r, red_r_value, ALPHA_RS, &aqm_data.data.gases.mics_red_r);
    aqm_filter_mics_r(aqm_data.data.gases.mics_ox_r, ox_r_value, ALPHA_RS, &aqm_data.data.gases.mics_ox_r);
    aqm_filter_mics_r(aqm_data.data.gases.mics_nh3_r, nh3_r_value, ALPHA_RS, &aqm_data.data.gases.mics_nh3_r);

    */
    

    aqm_calculate_mics_ratio(aqm_data.data.gases.mics_red_r, aqm_data.config.mics_r0.red_r0, &red_ratio);
    aqm_calculate_mics_ratio(aqm_data.data.gases.mics_ox_r, aqm_data.config.mics_r0.ox_r0, &ox_ratio);
    aqm_calculate_mics_ratio(aqm_data.data.gases.mics_nh3_r, aqm_data.config.mics_r0.nh3_r0, &nh3_ratio);

    aqm_data.data.gases.mics_red = red_ratio;
    aqm_data.data.gases.mics_ox = ox_ratio;
    aqm_data.data.gases.mics_nh3 = nh3_ratio;

    aqm_update_mics_r0_baselines();

}


void aqm_calculate_aqi_mics(void) {

    uint16_t aqi_nh3 = 1, aqi_ox = 1, aqi_red = 1;

    // Save to buffer
    push_to_buffer(&nh3_buf, (float)aqm_data.data.gases.mics_nh3_r);
    push_to_buffer(&ox_buf,  (float)aqm_data.data.gases.mics_ox_r);
    push_to_buffer(&red_buf,  (float)aqm_data.data.gases.mics_red_r);


    float nh3_5s = get_recent_average(&nh3_buf, 5);
    float nh3_30s = get_recent_average(&nh3_buf, 30);
    float nh3_60s = get_recent_average(&nh3_buf, 60);

    if (nh3_5s > 0 && (nh3_5s / (float)aqm_data.config.mics_r0.nh3_r0) < MICS_RED_NH3_AQI4) aqi_nh3 = 4;
    else if (nh3_30s > 0 && (nh3_30s / (float)aqm_data.config.mics_r0.nh3_r0) < MICS_RED_NH3_AQI3) aqi_nh3 = 3;
    else if (nh3_60s > 0 && (nh3_60s / (float)aqm_data.config.mics_r0.nh3_r0) < MICS_RED_NH3_AQI2) aqi_nh3 = 2;


    // OX
    float ox_5s = get_recent_average(&ox_buf, 5);
    float ox_30s = get_recent_average(&ox_buf, 30);
    float ox_60s = get_recent_average(&ox_buf, 60);

    if (ox_5s > 0 && (ox_5s / (float)aqm_data.config.mics_r0.ox_r0) > MICS_OX_AQI4) aqi_ox = 4;
    else if (ox_30s > 0 && (ox_30s / (float)aqm_data.config.mics_r0.ox_r0) > MICS_OX_AQI3) aqi_ox = 3;
    else if (ox_60s > 0 && (ox_60s / (float)aqm_data.config.mics_r0.ox_r0) > MICS_OX_AQI2) aqi_ox = 2;

    // RED
    float red_5s = get_recent_average(&red_buf, 5);
    float red_30s = get_recent_average(&red_buf, 30);
    float red_60s = get_recent_average(&red_buf, 60);

    if (red_5s > 0 && (red_5s / (float)aqm_data.config.mics_r0.red_r0) > MICS_RED_NH3_AQI4) aqi_red = 4;
    else if (red_30s > 0 && (red_30s / (float)aqm_data.config.mics_r0.red_r0) > MICS_RED_NH3_AQI3) aqi_red = 3;
    else if (red_60s > 0 && (red_60s / (float)aqm_data.config.mics_r0.red_r0) > MICS_RED_NH3_AQI2) aqi_red = 2;


    aqm_data.data.aqi_data.mics_nh3_aqi= aqi_nh3;
    aqm_data.data.aqi_data.mics_red_aqi = aqi_red;
    aqm_data.data.aqi_data.mics_ox_aqi= aqi_ox;

}
