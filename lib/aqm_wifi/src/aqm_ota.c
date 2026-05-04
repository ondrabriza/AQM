#include "aqm_ota.h"
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <sys/param.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "AQM_OTA";

/**
 * @brief POST handler for receiving the firmware binary and flashing it
 */
static esp_err_t ota_update_post_handler(httpd_req_t *req) {
    esp_err_t err;
    esp_ota_handle_t update_handle = 0;
    
    // Find next OTA partition
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found!");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA partition error");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing to partition: %s", update_partition->label);

    // 2. Begin OTA update
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    // 3. Read data from HTTP stream and write to Flash
    int received = 0;
    int remaining = req->content_len;
    char buf[1024];

    while (remaining > 0) {
        int ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue; // Retry on timeout
            }
            ESP_LOGE(TAG, "Error receiving data!");
            esp_ota_abort(update_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        err = esp_ota_write(update_handle, (const void *)buf, ret);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
            esp_ota_abort(update_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash write failed");
            return ESP_FAIL;
        }

        remaining -= ret;
        received += ret;
        // Optional: Print progress every 50-100KB to avoid spamming the log
        if (received % (1024 * 100) < sizeof(buf)) {
             ESP_LOGI(TAG, "Uploaded: %d / %d bytes", received, req->content_len);
        }
    }

    // 4. End OTA update and validate
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed (%s)", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    // 5. Set the new partition as the boot partition
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Boot partition set failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA Success! Restarting in 1 second...");
    httpd_resp_sendstr(req, "OK");
    
    // Give the server a moment to send the HTTP response back to curl/PlatformIO
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

void aqm_ota_register_http_endpoint(httpd_handle_t server) {
    httpd_uri_t ota_update_uri = {
        .uri       = "/update",
        .method    = HTTP_POST,
        .handler   = ota_update_post_handler,
        .user_ctx  = NULL
    };
    
    httpd_register_uri_handler(server, &ota_update_uri);
    ESP_LOGI(TAG, "OTA endpoint /update registered.");
}