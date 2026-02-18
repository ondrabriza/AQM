#include "sensirion_common.h"
#include "sensirion_i2c.h"
#include "sen55.h"
#include <esp_log.h>

static const char *TAG = "SEN55";

void sen55_init(void) {
    ESP_LOGE(TAG, "Initializing SEN55 sensor");
    sensirion_i2c_general_call_reset();
}