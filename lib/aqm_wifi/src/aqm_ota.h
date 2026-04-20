#ifndef AQM_OTA_H
#define AQM_OTA_H

#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers the OTA update endpoint (/update) to the HTTP server.
 * * @param server Handle to the running HTTP server
 */
void aqm_ota_register_http_endpoint(httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif // AQM_OTA_H