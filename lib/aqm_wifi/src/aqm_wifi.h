#ifndef AQM_WIFI_H
#define AQM_WIFI_H

#include <esp_wifi.h>
#include <esp_event.h>

extern EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_AP_BIT      BIT1


/**
 * @brief Initialize Wi-Fi in station mode and connect to the Access Point.
 * * @return esp_err_t ESP_OK on success
 */
void aqm_wifi_connect(void);
    // Start HTTP server for provisioning
void start_web_server(void);
void start_mdns_service(void); // aqm.local


#endif // AQM_WIFI_H