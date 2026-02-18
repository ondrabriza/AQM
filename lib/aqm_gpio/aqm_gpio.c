#include "aqm_gpio.h"
#include "aqm_config.h"
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
static const char *TAG = "AQM_GPIO";

static esp_err_t led_init(void);
static esp_err_t relay_init(void);

esp_err_t aqm_gpio_intialize(void) {
    /* Configure GPIOs for outputs */
    esp_err_t err = led_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED pin");
        return err;
    }
    err = relay_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize relay pin");
        return err;
    }
    return ESP_OK;
}

static esp_err_t led_init(void){
    esp_err_t err = gpio_reset_pin(LED_PIN);
    err = gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    return err;

}

static esp_err_t relay_init(void){

    esp_err_t err =gpio_reset_pin(RELAY_PIN);
    err =  gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    return err;
}