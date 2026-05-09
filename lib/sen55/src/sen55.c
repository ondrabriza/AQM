#include "sensirion_common.h"
#include "sensirion_i2c.h"
#include "sen55.h"

#include "aqm_datastore.h"
#include "aqm_config.h"

#include <esp_log.h>

static const char *TAG = "SEN55";

static ring_buffer_t pm_buffer = {0};
static ring_buffer_t voc_buffer = {0};
static ring_buffer_t nox_buffer = {0};

void aqm_calculate_sen55_aqi(void){

    
    uint8_t aqi_pm = 1;
    uint8_t aqi_voc = 1;
    uint8_t aqi_nox = 1;

    push_to_buffer(&pm_buffer,  (float)aqm_data.data.sen55.pm2_5/10.0f);
    push_to_buffer(&voc_buffer, (float)aqm_data.data.sen55.voc_index/10.0f);
    push_to_buffer(&nox_buffer, (float)aqm_data.data.sen55.nox_index/10.0f);


    float pm_5s = get_recent_average(&pm_buffer, 5);
    float pm_30s = get_recent_average(&pm_buffer, 30);
    float pm_60s = get_recent_average(&pm_buffer, 60);

    float voc_5s = get_recent_average(&voc_buffer, 5);
    float voc_30s = get_recent_average(&voc_buffer, 30);
    float voc_60s = get_recent_average(&voc_buffer, 60);


    float nox_5s = get_recent_average(&nox_buffer, 5);
    float nox_30s = get_recent_average(&nox_buffer, 30);
    float nox_60s = get_recent_average(&nox_buffer, 60);

    ESP_LOGI(TAG, "SEN55 AQI Calc - PM2.5: 5s: %.1f, 30s: %.1f, 60s: %.1f | VOC: 5s: %.1f, 30s: %.1f, 60s: %.1f | NOx: 5s: %.1f, 30s: %.1f, 60s: %.1f", 
            pm_5s, pm_30s, pm_60s, voc_5s, voc_30s, voc_60s, nox_5s, nox_30s, nox_60s);

    if (pm_5s > PM_AQI4) aqi_pm = 4;
    else if (pm_30s > PM_AQI3) aqi_pm = 3;
    else if (pm_60s > PM_AQI2) aqi_pm = 2;

    if(voc_5s > LIMIT_VOC_AQI4) aqi_voc = 4;
    else if (voc_30s > LIMIT_VOC_AQI3) aqi_voc = 3;
    else if (voc_60s > LIMIT_VOC_AQI2) aqi_voc = 2;

    if(nox_5s > LIMIT_NOX_AQI4) aqi_nox = 4;
    else if (nox_30s > LIMIT_NOX_AQI3) aqi_nox = 3;
    else if (nox_60s > LIMIT_NOX_AQI2) aqi_nox = 2;

    aqm_data.data.aqi_data.pm_aqi = aqi_pm;
    aqm_data.data.aqi_data.voc_aqi = aqi_voc;
    aqm_data.data.aqi_data.nox_aqi = aqi_nox;

}
