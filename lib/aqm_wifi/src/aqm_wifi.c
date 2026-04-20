#include "aqm_wifi.h"
#include "aqm_config.h"
#include "aqm_datastore.h"
#include "aqm_ota.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h> // Required for malloc/free
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include <mdns.h>

static const char *TAG = "AQM_WIFI";
static int s_retry_num = 0;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

EventGroupHandle_t s_wifi_event_group;

// Increased allocation size to fit the larger HTML/JS payload securely
#define HTML_MALLOC_SIZE 8192

// ------------------------------------------------------------------
// HTTP SERVER (CAPTIVE PORTAL & DASHBOARD) SECTION
// ------------------------------------------------------------------

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

/**
 * @brief GET API: Returns current sensor data and statuses in JSON format
 */
static esp_err_t api_data_get_handler(httpd_req_t *req) {
    char json_response[768];
    
    snprintf(json_response, sizeof(json_response),
        "{\"temp\":%.1f,\"hum\":%.1f,\"pm25\":%.1f,\"so2\":%.1f,\"red\":%.2f,\"h2s\":%.1f,\"nh3\":%.2f,\"ox\":%.2f,\"voc\":%d,\"nox\":%d,\"v33\":%d,\"v5v\":%d,"
        "\"relay\":%d,\"led\":%d,\"mbtcp\":%d,\"wifi\":%d}",
        aqm_data.data.sen55.temperature/200.0f,
        aqm_data.data.sen55.humidity/100.0f,
        aqm_data.data.sen55.pm2_5/10.0f,
        aqm_data.data.gases.so2_ppm/10.0f,
        aqm_data.data.gases.mics_red/100.0f,
        aqm_data.data.gases.h2s_ppm/10.0f,
        aqm_data.data.gases.mics_nh3/100.0f,
        aqm_data.data.gases.mics_ox/100.0f,
        aqm_data.data.sen55.voc_index/10,
        aqm_data.data.sen55.nox_index/10,
        aqm_data.data.status.v3v3_mv,
        aqm_data.data.status.v5v_mv,
        aqm_data.data.status.status_word.flags.relay_state,
        aqm_data.data.status.status_word.flags.led_state,
        aqm_data.data.status.status_word.flags.mb_tcp_en,
        aqm_data.data.status.status_word.flags.wifi_en
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief POST API: Receives commands from sliders to instantly toggle hardware states
 */
static esp_err_t api_control_post_handler(httpd_req_t *req) {
    char buf[128];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char target[32] = {0};
    char val_str[8] = {0};

    // Expects URL-encoded format: target=relay&val=1
    if (httpd_query_key_value(buf, "target", target, sizeof(target)) == ESP_OK &&
        httpd_query_key_value(buf, "val", val_str, sizeof(val_str)) == ESP_OK) {
        
        uint8_t val = (atoi(val_str) > 0) ? 1 : 0;

        if (strcmp(target, "relay") == 0)      aqm_data.config.control_word.flags.relay_state = val;
        else if (strcmp(target, "led") == 0)   aqm_data.config.control_word.flags.led_state = val;
        else if (strcmp(target, "mbtcp") == 0) aqm_data.config.control_word.flags.mb_tcp_en = val;
        else if (strcmp(target, "wifi") == 0)  aqm_data.config.control_word.flags.wifi_en = val;

        aqm_datastore_set_flag_cw_changed();
        
        ESP_LOGI(TAG, "Control API - Changed %s to %d", target, val);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_send_500(req);
    return ESP_FAIL;
}

/**
 * @brief POST API: Sets current or manual resistance values as new R0 baselines
 */
static esp_err_t api_baseline_post_handler(httpd_req_t *req) {
    char buf[128];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char target[32] = {0};
    char val_str[32] = {0};
    uint32_t manual_val = 0;

    // 1. Identify which gas channel is being calibrated
    if (httpd_query_key_value(buf, "target", target, sizeof(target)) == ESP_OK) {
        
        // 2. Check if a manual value was provided
        if (httpd_query_key_value(buf, "val", val_str, sizeof(val_str)) == ESP_OK) {
            if (strlen(val_str) > 0) {
                manual_val = (uint32_t)atol(val_str);
            }
        }

        // 3. Apply baseline: Manual if provided, otherwise fetch current sensor resistance
        if (strcmp(target, "red") == 0) {
            aqm_data.config.mics_r0.red_r0 = (manual_val > 0) ? manual_val : aqm_data.data.gases.mics_red_r;
        } else if (strcmp(target, "nh3") == 0) {
            aqm_data.config.mics_r0.nh3_r0 = (manual_val > 0) ? manual_val : aqm_data.data.gases.mics_nh3_r;
        } else if (strcmp(target, "ox") == 0) {
            aqm_data.config.mics_r0.ox_r0 = (manual_val > 0) ? manual_val : aqm_data.data.gases.mics_ox_r;
        } else if (strcmp(target, "all") == 0) {
            // "ALL" button ignores manual inputs and captures current states
            aqm_data.config.mics_r0.red_r0 = aqm_data.data.gases.mics_red_r;
            aqm_data.config.mics_r0.nh3_r0 = aqm_data.data.gases.mics_nh3_r;
            aqm_data.config.mics_r0.ox_r0  = aqm_data.data.gases.mics_ox_r;
        }

        // Trigger save to NVS to ensure calibration survives reboot
        // Note: Implement or use your specific save function here if different
        // aqm_control_word_save_nvs(); 

        if (manual_val > 0) {
            ESP_LOGI(TAG, "Baseline API - %s set MANUALLY to: %lu", target, (unsigned long)manual_val);
        } else {
            ESP_LOGI(TAG, "Baseline API - %s set AUTOMATICALLY to current readings", target);
        }

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_send_500(req);
    return ESP_FAIL;
}

/**
 * @brief HTTP GET Handler: Static HTML skeleton + Javascript
 */
static esp_err_t wifi_config_get_handler(httpd_req_t *req) {
    esp_netif_ip_info_t ip_info;
    char ip_str[16] = "0.0.0.0";
    if (s_sta_netif != NULL) {
        esp_netif_get_ip_info(s_sta_netif, &ip_info);
        if (ip_info.ip.addr != 0) {
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
        }
    }

    char *html = malloc(HTML_MALLOC_SIZE); 
    if (html == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for HTML");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    snprintf(html, HTML_MALLOC_SIZE,
        "<html><head><title>AQM Dashboard</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>"
        "body{font-family:sans-serif; padding:20px; background:#f4f4f4;} "
        ".box{background:#fff; padding:20px; border-radius:8px; box-shadow:0 0 10px rgba(0,0,0,0.1); margin-bottom:20px;} "
        "h2{margin-top:0; color:#333; border-bottom:2px solid #007BFF; padding-bottom:5px;} "
        "input[type='text'], input[type='password'], input[type='number']{width:100%%; margin-bottom:15px; padding:10px; border:1px solid #ccc; border-radius:4px; box-sizing: border-box;} "
        "input[type='submit'], .btn{background:#007BFF; color:white; border:none; padding:12px 20px; cursor:pointer; border-radius:4px; font-size:16px; width:100%%;} "
        ".btn-success{background:#28a745;} .btn-outline{background:#fff; color:#007BFF; border:1px solid #007BFF;} "
        ".switch {position: relative; display: inline-block; width: 50px; height: 24px; vertical-align: middle;}"
        ".switch input {opacity: 0; width: 0; height: 0;}"
        ".slider {position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 24px;}"
        ".slider:before {position: absolute; content: ''; height: 18px; width: 18px; left: 3px; bottom: 3px; background-color: white; transition: .4s; border-radius: 50%%;}"
        "input:checked + .slider {background-color: #28a745;}"
        "input:checked + .slider:before {transform: translateX(26px);}"
        ".flex-row {display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; border-bottom: 1px solid #eee; padding-bottom: 10px;}"
        ".data-grid {display: grid; grid-template-columns: repeat(auto-fit, minmax(110px, 1fr)); gap: 10px;}"
        ".data-item {background: #f8f9fa; padding: 10px; border-radius: 5px; text-align: center; border: 1px solid #ddd;}"
        ".data-val {font-size: 1.2em; font-weight: bold; color: #007BFF;}"
        "</style>"
        "<script>"
        "function setBaseline(target, name) {"
        "  let val = '';"
        "  if (target !== 'all') {"
        "    let inputEl = document.getElementById('val_' + target);"
        "    if (inputEl && inputEl.value !== '') { val = inputEl.value; }"
        "  }"
        "  let msg = val ? ('Set ' + name + ' baseline to ' + val + '?') : ('Set CURRENT resistance as new baseline for ' + name + '?');"
        "  if(confirm(msg + ' Ensure the sensor is in clean air!')) {"
        "    fetch('/api/baseline', {"
        "      method: 'POST',"
        "      headers: {'Content-Type': 'application/x-www-form-urlencoded'},"
        "      body: 'target=' + target + '&val=' + val"
        "    }).then(r => r.json()).then(d => { alert('Baseline updated successfully!'); if(target!=='all') document.getElementById('val_'+target).value = ''; }).catch(e => console.error(e));"
        "  }"
        "}"
        "function toggleSetting(target, obj) {"
        "  let val = obj.checked ? 1 : 0;"
        "  fetch('/api/control', {"
        "    method: 'POST',"
        "    headers: {'Content-Type': 'application/x-www-form-urlencoded'},"
        "    body: 'target=' + target + '&val=' + val"
        "  }).catch(e => console.error(e));"
        "}"
        "function updateData() {"
        "  fetch('/api/data').then(r => r.json()).then(d => {"
        "    document.getElementById('temp').innerText = d.temp;"
        "    document.getElementById('hum').innerText = d.hum;"
        "    document.getElementById('pm25').innerText = d.pm25;"
        "    document.getElementById('so2').innerText = d.so2;"
        "    document.getElementById('red').innerText = d.red;"
        "    document.getElementById('h2s').innerText = d.h2s;"
        "    document.getElementById('nh3').innerText = d.nh3;"
        "    document.getElementById('ox').innerText = d.ox;"
        "    document.getElementById('voc').innerText = d.voc;"
        "    document.getElementById('nox').innerText = d.nox;"
        "    document.getElementById('v33').innerText = d.v33;"
        "    document.getElementById('v5v').innerText = d.v5v;"
        "    document.getElementById('chk_relay').checked = d.relay;"
        "    document.getElementById('chk_led').checked = d.led;"
        "    document.getElementById('chk_mbtcp').checked = d.mbtcp;"
        "    document.getElementById('chk_wifi').checked = d.wifi;"
        "  }).catch(e => console.error(e));"
        "}"
        "function doFactoryReset() {"
        "  if(confirm('Are you sure you want to completely erase all settings and restore factory defaults? The device will restart.')) {"
        "    fetch('/api/reset', { method: 'POST' }).then(() => {"
        "      alert('Device is resetting. Please reconnect to the default Wi-Fi after 10 seconds.');"
        "      window.location.reload();"
        "    });"
        "  }"
        "}"
        "setInterval(updateData, 2000);"
        "window.onload = updateData;"
        "</script>"
        "</head><body>"

        "<div class='box'>"
        "<h2>Device Dashboard</h2>"
        "<p><strong>Network IP:</strong> %s</p>"
        "<div class='data-grid'>"
        "<div class='data-item'>Temp<br><span id='temp' class='data-val'>--</span> &deg;C</div>"
        "<div class='data-item'>Humidity<br><span id='hum' class='data-val'>--</span> %%</div>"
        "<div class='data-item'>PM 2.5<br><span id='pm25' class='data-val'>--</span> &mu;g/m&sup3;</div>"
        "<div class='data-item'>VOC<br><span id='voc' class='data-val'>--</span> Idx</div>"
        "<div class='data-item'>NOX<br><span id='nox' class='data-val'>--</span> Idx</div>"
        "<div class='data-item'>SO2<br><span id='so2' class='data-val'>--</span> ppm</div>"
        "<div class='data-item'>H2S<br><span id='h2s' class='data-val'>--</span> ppm</div>"
        "<div class='data-item'>RED<br><span id='red' class='data-val'>--</span> ratio</div>"
        "<div class='data-item'>NH3<br><span id='nh3' class='data-val'>--</span> ratio</div>"
        "<div class='data-item'>OX<br><span id='ox' class='data-val'>--</span> ratio</div>"
        "<div class='data-item'>3.3V Rail<br><span id='v33' class='data-val'>--</span> mV</div>"
        "<div class='data-item'>5V Rail<br><span id='v5v' class='data-val'>--</span> mV</div>"
        "</div>"
        "</div>"

        "<div class='box'>"
        "<h2>Sensor Calibration</h2>"
        "<p style='font-size:0.9em; color:#666;'>Leave input blank to use current sensor reading, or enter a specific resistance value manually.</p>"
        
        "<div class='flex-row'>"
        "  <span><strong>RED</strong> Channel</span>"
        "  <div style='display:flex; gap:5px;'>"
        "    <input type='number' id='val_red' placeholder='Auto (Current R)' style='margin:0; width:150px;'>"
        "    <button class='btn btn-outline' style='width:auto; padding:8px 15px;' onclick='setBaseline(\"red\", \"RED\")'>Save</button>"
        "  </div>"
        "</div>"

        "<div class='flex-row'>"
        "  <span><strong>NH3</strong> Channel</span>"
        "  <div style='display:flex; gap:5px;'>"
        "    <input type='number' id='val_nh3' placeholder='Auto (Current R)' style='margin:0; width:150px;'>"
        "    <button class='btn btn-outline' style='width:auto; padding:8px 15px;' onclick='setBaseline(\"nh3\", \"NH3\")'>Save</button>"
        "  </div>"
        "</div>"

        "<div class='flex-row'>"
        "  <span><strong>OX</strong> Channel</span>"
        "  <div style='display:flex; gap:5px;'>"
        "    <input type='number' id='val_ox' placeholder='Auto (Current R)' style='margin:0; width:150px;'>"
        "    <button class='btn btn-outline' style='width:auto; padding:8px 15px;' onclick='setBaseline(\"ox\", \"OX\")'>Save</button>"
        "  </div>"
        "</div>"

        "<button class='btn btn-success' style='margin-top:10px;' onclick='setBaseline(\"all\", \"ALL CHANNELS\")'>Auto-Calibrate ALL to Current Readings</button>"
        "</div>"

        "<div class='box'>"
        "<h2>Settings & Control</h2>"
        "<div class='flex-row'><span>Relay Status</span> <label class='switch'><input type='checkbox' id='chk_relay' onchange='toggleSetting(\"relay\", this)'><span class='slider'></span></label></div>"
        "<div class='flex-row'><span>LED Status</span> <label class='switch'><input type='checkbox' id='chk_led' onchange='toggleSetting(\"led\", this)'><span class='slider'></span></label></div>"
        "<div class='flex-row'><span>Modbus TCP</span> <label class='switch'><input type='checkbox' id='chk_mbtcp' onchange='toggleSetting(\"mbtcp\", this)'><span class='slider'></span></label></div>"
        "<div class='flex-row'><span>Wi-Fi Enabled</span> <label class='switch'><input type='checkbox' id='chk_wifi' onchange='toggleSetting(\"wifi\", this)'><span class='slider'></span></label></div>"

        "<form action='/save' method='POST' style='margin-top:30px; border-top: 1px solid #ccc; padding-top:20px;'>"
        "<h3>Wi-Fi Network Setup</h3>"
        "<label>SSID (Network Name):</label><br><input type='text' name='ssid' value='%s'><br>"
        "<label>Password:</label><br><input type='password' name='password'><br>"
        "<input type='submit' value='Save Wi-Fi & Restart'>"
        "</form></div>"

        "<div style='margin-top:40px; text-align:center;'>"
        "<button onclick='doFactoryReset()' style='background:#dc3545; color:white; border:none; padding:10px 15px; border-radius:4px; cursor:pointer;'>Factory Reset</button>"
        "</div>"
        
        "</body></html>",
        
        ip_str,
        aqm_data.wifi_config.wifi_ssid
    );

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    
    free(html);
    return ESP_OK;
}

/**
 * @brief HTTP POST Handler: Processes ONLY Wi-Fi credential changes and restarts
 */
static esp_err_t wifi_save_post_handler(httpd_req_t *req) {
    char buf[512]; 
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        ESP_LOGE(TAG, "POST payload too large");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char raw_val[128] = {0};

    // 1. Process Wi-Fi credentials
    if (httpd_query_key_value(buf, "ssid", raw_val, sizeof(raw_val)) == ESP_OK) {
        url_decode(aqm_data.wifi_config.wifi_ssid, raw_val);
    }
    if (httpd_query_key_value(buf, "password", raw_val, sizeof(raw_val)) == ESP_OK) {
        url_decode(aqm_data.wifi_config.wifi_pass, raw_val);
    }

    ESP_LOGI(TAG, "Wi-Fi Settings Updated. SSID: %s", aqm_data.wifi_config.wifi_ssid);

    aqm_wifi_config_save_nvs();
    aqm_control_word_save_nvs();

    // 3. Send Response
    const char* resp = "<html><body style='font-family:sans-serif; text-align:center; padding:50px;'>"
                       "<h2>Settings Saved!</h2><p>Applying Wi-Fi configuration and restarting device...</p>"
                       "</body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    
    // 4. Restart device to apply network changes
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();

    return ESP_OK;
}

/**
 * @brief Factory reset
 */
static esp_err_t api_factory_reset_post_handler(httpd_req_t *req) {
    ESP_LOGW(TAG, "Factory reset requested via Web Dashboard!");

    // Send response to client before restarting the system
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\",\"msg\":\"Resetting...\"}", HTTPD_RESP_USE_STRLEN);

    // Call your function to fill NVS memory with default values
    aqm_datastore_fill_nvs_with_defaults();

    ESP_LOGW(TAG, "Factory reset applied. Restarting device in 1 second...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

/**
 * @brief Starts the web server and registers paths
 */
void start_web_server(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192; // Ensure sufficient stack size for HTML allocations

    if (httpd_start(&server, &config) == ESP_OK) {
        // Register main HTML page
        httpd_uri_t get_uri = { .uri = "/", .method = HTTP_GET, .handler = wifi_config_get_handler };
        httpd_register_uri_handler(server, &get_uri);

        // Register API for reading status and values
        httpd_uri_t api_data_uri = { .uri = "/api/data", .method = HTTP_GET, .handler = api_data_get_handler };
        httpd_register_uri_handler(server, &api_data_uri);

        // Register API for async slider toggling
        httpd_uri_t api_ctrl_uri = { .uri = "/api/control", .method = HTTP_POST, .handler = api_control_post_handler };
        httpd_register_uri_handler(server, &api_ctrl_uri);

        // Register API for baseline calibration
        httpd_uri_t api_baseline_uri = { .uri = "/api/baseline", .method = HTTP_POST, .handler = api_baseline_post_handler };
        httpd_register_uri_handler(server, &api_baseline_uri);

        // Register API for Factory Reset
        httpd_uri_t api_reset_uri = { .uri = "/api/reset", .method = HTTP_POST, .handler = api_factory_reset_post_handler };
        httpd_register_uri_handler(server, &api_reset_uri);

        // Register form for saving Wi-Fi settings
        httpd_uri_t post_uri = { .uri = "/save", .method = HTTP_POST, .handler = wifi_save_post_handler };
        httpd_register_uri_handler(server, &post_uri);

        // Register the OTA firmware update endpoint
        aqm_ota_register_http_endpoint(server);
        
        ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
    }
}

/**
 * @brief Initializes mDNS and registers services
 */
void start_mdns_service(void) {
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGE(TAG, "mDNS init failed: %d", err);
    }

    mdns_hostname_set("aqm");
    mdns_instance_name_set("AQM Environmental Sensor");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    ESP_LOGI(TAG, "mDNS active: aqm.local");
}


// ------------------------------------------------------------------
// WI-FI CONNECTION & FALLBACK SECTION
// ------------------------------------------------------------------

/**
 * @brief Starts the ESP32 in Access Point mode as a fallback
 */
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
    
    ESP_LOGI(TAG, "Connect to AP '%s' and go to 192.168.4.1 in your browser.", AQM_AP_SSID);
}

/**
 * @brief Event handler for Wi-Fi and IP events
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi started, attempting to connect...");
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < AQM_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect (%d/%d)", s_retry_num, AQM_MAX_RETRY);
        } else {
            ESP_LOGE(TAG, "Connection failed. Starting AP portal.");
            start_ap_fallback();
            xEventGroupSetBits(s_wifi_event_group, WIFI_AP_BIT);
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected! IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief Initializes Wi-Fi stack and connects to the network
 */
void aqm_wifi_connect(void) {
    s_wifi_event_group = xEventGroupCreate();
    
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
    
    if (strlen(aqm_data.wifi_config.wifi_ssid) > 0) {
        ESP_LOGI(TAG, "Using saved Wi-Fi credentials from NVS.");
        strncpy((char*)wifi_config.sta.ssid, aqm_data.wifi_config.wifi_ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char*)wifi_config.sta.password, aqm_data.wifi_config.wifi_pass, sizeof(wifi_config.sta.password));

        ESP_LOGI(TAG, "Target SSID: %s", wifi_config.sta.ssid);
    } else {
        ESP_LOGI(TAG, "NVS is empty. Using default Wi-Fi credentials from config.h.");
        strncpy((char*)wifi_config.sta.ssid, AQM_WIFI_SSID, sizeof(wifi_config.sta.ssid));
        strncpy((char*)wifi_config.sta.password, AQM_WIFI_PASS, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}