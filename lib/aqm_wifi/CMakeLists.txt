idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "src"
                       REQUIRES esp_wifi esp_event
                       PRIV_REQUIRES esp_netif esp_http_server log aqm_config aqm_datastore)