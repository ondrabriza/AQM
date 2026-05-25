#ifndef AQM_WIFI_H
#define AQM_WIFI_H

#include <esp_wifi.h>
#include <esp_event.h>

extern EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_AP_BIT      BIT1


/**
 * @brief Connect to Wi-Fi using credentials from NVS or defaults from config.h. Sets up event handlers for Wi-Fi and IP events. If connection fails after max retries, starts AP fallback mode.
 */
void aqm_wifi_connect(void);
    
/**
 * @brief Start the web server for handling HTTP requests.
 * 
 */
void start_web_server(void);

/**
 * @brief Start mDNS service for hostname aqm.local, allowing users to access the web interface without needing to know the IP address.
 * 
 */
void start_mdns_service(void); // aqm.local


#endif // AQM_WIFI_H