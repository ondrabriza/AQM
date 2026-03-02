#include "aqm_wifi.h"
#include "aqm_config.h"
#include "aqm_datastore.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_http_server.h>

static const char *TAG = "AQM_WIFI";
static int s_retry_num = 0;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

// ------------------------------------------------------------------
// HTTP SERVER (CAPTIVE PORTAL) SECTION
// ------------------------------------------------------------------

static const char* wifi_config_html = 
    "<html><head><title>AQM Setup</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>body{font-family:sans-serif; padding:20px; background:#f4f4f4;} "
    ".box{background:#fff; padding:20px; border-radius:8px; box-shadow:0 0 10px rgba(0,0,0,0.1);} "
    "input{width:100%; margin-bottom:15px; padding:10px; border:1px solid #ccc; border-radius:4px;} "
    "input[type='submit']{background:#007BFF; color:white; border:none; cursor:pointer;}</style>"
    "</head><body><div class='box'>"
    "<h2>Wi-Fi Configuration</h2>"
    "<form action='/save' method='POST'>"
    "<label>SSID (Network Name):</label><br><input type='text' name='ssid' required><br>"
    "<label>Password:</label><br><input type='password' name='password'><br>"
    "<input type='submit' value='Save & Restart'>"
    "</form></div></body></html>";

/**
 * @brief Helper function to decode URL-encoded strings (e.g., %20 to space)
 */
static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit((int)a) && isxdigit((int)b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

static esp_err_t wifi_config_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, wifi_config_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wifi_save_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char raw_ssid[64] = {0};
    char raw_pass[128] = {0};

    if (httpd_query_key_value(buf, "ssid", raw_ssid, sizeof(raw_ssid)) == ESP_OK) {
        url_decode(g_aqm_data.wifi_config.wifi_ssid, raw_ssid);
    }
    if (httpd_query_key_value(buf, "password", raw_pass, sizeof(raw_pass)) == ESP_OK) {
        url_decode(g_aqm_data.wifi_config.wifi_pass, raw_pass);
    }
    
    ESP_LOGI(TAG, "New credentials received. SSID: %s", g_aqm_data.wifi_config.wifi_ssid);
    //ESP_LOGI(TAG, "Password: %s", g_aqm_data.wifi_config.wifi_pass);

    aqm_wifi_config_save_nvs();

    const char* resp = "<html><body><h2>Saved!</h2><p>Restarting device...</p></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();

    return ESP_OK;
}

static void start_web_server(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t get_uri = { .uri = "/", .method = HTTP_GET, .handler = wifi_config_get_handler };
        httpd_register_uri_handler(server, &get_uri);

        httpd_uri_t post_uri = { .uri = "/save", .method = HTTP_POST, .handler = wifi_save_post_handler };
        httpd_register_uri_handler(server, &post_uri);
        ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
    }
}

// ------------------------------------------------------------------
// WI-FI CONNECTION & FALLBACK SECTION
// ------------------------------------------------------------------

static void start_ap_fallback(void) {
    ESP_LOGW(TAG, "Starting Fallback Access Point: %s", AQM_AP_SSID);
    
    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AQM_AP_SSID,
            .ssid_len = strlen(AQM_AP_SSID),
            .password = AQM_AP_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .channel = 1
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    
    g_aqm_data.status.wifi_connected = false;
    
    // Start HTTP server for provisioning
    start_web_server();
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        g_aqm_data.status.wifi_connected = false;

        if (s_retry_num < AQM_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect (%d/%d)", s_retry_num, AQM_MAX_RETRY);
        } else {
            ESP_LOGE(TAG, "Connection failed. Starting AP portal.");
            start_ap_fallback();
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected! IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        g_aqm_data.status.wifi_connected = true;
    }
}

void aqm_wifi_connect(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    
    strncpy((char*)wifi_config.sta.ssid, g_aqm_data.wifi_config.wifi_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, g_aqm_data.wifi_config.wifi_pass, sizeof(wifi_config.sta.password));
    

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}