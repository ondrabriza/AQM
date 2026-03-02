#include "aqm_datastore.h"
#include <string.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>

static const char *TAG = "AQM_DATASTORE";

aqm_data_t g_aqm_data;

void aqm_datastore_init(void) {
    // Zero out the entire structure
    memset(&g_aqm_data, 0, sizeof(aqm_data_t));

    // Try to load saved data from Flash memory
    aqm_wifi_config_load_nvs();
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
    err = nvs_set_blob(my_handle, "wifi_cfg", &g_aqm_data.wifi_config, sizeof(aqm_wifi_config_t));
    
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
    err = nvs_get_blob(my_handle, "wifi_cfg", &g_aqm_data.wifi_config, &required_size);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Loaded saved Wi-Fi. SSID: %s", g_aqm_data.wifi_config.wifi_ssid);
    } else {
        ESP_LOGW(TAG, "No Wi-Fi configuration found in NVS.");
    }

    nvs_close(my_handle);
}