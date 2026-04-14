#include "aqm_datastore.h"
#include <string.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>

static const char *TAG = "AQM_DATASTORE";

aqm_data_t aqm_data;

void aqm_datastore_init(void) {
    // Zero out the entire structure
    memset(&aqm_data, 0, sizeof(aqm_data_t));

    // Try to load saved data from Flash memory
    aqm_wifi_config_load_nvs();
    aqm_control_word_load_nvs();
}

void aqm_wifi_config_save_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open NVS namespace "storage" in read/write mode
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS!", esp_err_to_name(err));
        return;
    }

    // Save the wifi_config structure as a BLOB (Binary Large Object)
    err = nvs_set_blob(my_handle, "wifi_cfg", &aqm_data.wifi_config, sizeof(aqm_wifi_config_t));
    
    if (err == ESP_OK) {
        // Write must be committed
        err = nvs_commit(my_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Wi-Fi configuration successfully saved to Flash.");
        }
    } else {
        ESP_LOGE(TAG, "Error writing to NVS!");
    }

    // Close the handle
    nvs_close(my_handle);
}

void aqm_wifi_config_load_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open NVS in read-only mode
    err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS memory is empty or not initialized (normal on first boot).");
        return;
    }

    size_t required_size = sizeof(aqm_wifi_config_t);
    err = nvs_get_blob(my_handle, "wifi_cfg", &aqm_data.wifi_config, &required_size);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Loaded saved Wi-Fi. SSID: %s", aqm_data.wifi_config.wifi_ssid);
    } else {
        ESP_LOGW(TAG, "No Wi-Fi configuration found in NVS.");
    }

    nvs_close(my_handle);
}

void aqm_control_word_save_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open NVS namespace "storage" in read/write mode
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS!", esp_err_to_name(err));
        return;
    }

    // Save the control word as a 16-bit value
    err = nvs_set_u16(my_handle, "ctrl_word", aqm_data.config.control_word.word);
    
    if (err == ESP_OK) {
        // Write must be committed
        err = nvs_commit(my_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Control word successfully saved to Flash.");
        }
    } else {
        ESP_LOGE(TAG, "Error writing control word to NVS!");
    }

    // Close the handle
    nvs_close(my_handle);
}

void aqm_control_word_load_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open NVS in read-only mode
    err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS memory is empty or not initialized (normal on first boot).");
        return;
    }

    // Load the control word as a 16-bit value
    err = nvs_get_u16(my_handle, "ctrl_word", &aqm_data.config.control_word.word);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Loaded saved control word from NVS.");
    } else {
        ESP_LOGW(TAG, "No control word found in NVS.");
    }

    nvs_close(my_handle);
}

void aqm_datastore_fill_nvs_with_defaults(void){
    // Fill the control word with default values (e.g., all features enabled)
    aqm_data.config.control_word.flags.measure_en = 1;
    aqm_data.config.control_word.flags.wifi_en = 1;
    aqm_data.config.control_word.flags.relay_state = 0; // Relay off by default
    aqm_data.config.control_word.flags.led_state = 0;   // LED off by default
    aqm_data.config.control_word.flags.web_server_en = 1;
    aqm_data.config.control_word.flags.mb_tcp_en = 1;
    aqm_data.config.control_word.flags.mb_rtu_en = 1;

    aqm_data.wifi_config.wifi_ssid[0] = '\0'; // Empty SSID
    aqm_data.wifi_config.wifi_pass[0] = '\0'; // Empty
    

    // Save these defaults to NVS
    aqm_control_word_save_nvs();
    aqm_wifi_config_save_nvs();
}


/**
 * @brief Fills the datastore with dummy/test data for debugging purposes.
 */
void aqm_datastore_fill_test_data(void) {
    ESP_LOGI(TAG, "Filling datastore with TEST DATA...");

    // --- 1. System Status (Reg 0) ---
    aqm_data.data.status.status_word.flags.wifi_en = 1;
    aqm_data.data.status.status_word.flags.relay_state = 1;

    // --- 2. RAW ADC Values (Reg 1 - 7) ---
    aqm_data.data.adc_raw.so2_raw_val = 15400;
    aqm_data.data.adc_raw.v3v3_raw_val = 26500;
    aqm_data.data.adc_raw.v5v_raw_val = 20100;
    aqm_data.data.adc_raw.h2s_raw_val = 8400;
    aqm_data.data.adc_raw.mics_red_raw_val = 12000;
    aqm_data.data.adc_raw.mics_nh3_raw_val = 9500;
    aqm_data.data.adc_raw.mics_ox_raw_val = 11000;

    // --- 3. Calculated Gas PPM (Reg 8 - 12) ---
    aqm_data.data.gases.so2_ppm = 2.45f;
    aqm_data.data.gases.h2s_ppm = 0.85f;
    aqm_data.data.gases.mics_ox_r = 15.2f;
    aqm_data.data.gases.mics_nh3_r = 5.1f;
    aqm_data.data.gases.mics_red_r = 1.2f;

    // --- 4. Voltage Rails (Reg 13 - 14) ---
    aqm_data.data.status.v3v3_mv = 3290;  // 3.29 V
    aqm_data.data.status.v5v_mv = 5050;   // 5.05 V

    // --- 5. SEN55 Climate Data (Reg 15 - 16) ---
    aqm_data.data.sen55.temperature = 23.5f; // 23.5 °C
    aqm_data.data.sen55.humidity = 45.2f;    // 45.2 %

    // --- 6. SEN55 PM & Index Data (Reg 17 - 22) ---
    aqm_data.data.sen55.pm1_0 = 5.0f;        // 5 ug/m3
    aqm_data.data.sen55.pm2_5 = 12.5f;       // 12.5 ug/m3
    aqm_data.data.sen55.pm4_0 = 14.0f;
    aqm_data.data.sen55.pm10_0 = 18.0f;
    aqm_data.data.sen55.voc_index = 115.0f;  // 115 points
    aqm_data.data.sen55.nox_index = 20.0f;   // 20 points

    aqm_data.data.status.timestamp = 65540; // 1 hour uptime
}


void aqm_datastore_set_flag_cw_changed(void){

    // Set the cw_changed flag in the control word to indicate that it was changed
    aqm_data.config.control_word.flags.cw_changed = 1;
}