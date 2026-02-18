#ifndef AQM_WIFI_H
#define AQM_WIFI_H


/**
 * @brief Initialize Wi-Fi in station mode and connect to the Access Point.
 * * @return esp_err_t ESP_OK on success
 */
void aqm_wifi_connect(void);

#endif // AQM_WIFI_H