#include "aqm_gpio.h"
#include "aqm_config.h"
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>

static const char *TAG = "AQM_GPIO";

// --- Forward declarations ---
static esp_err_t led_init(void);
static esp_err_t relay_init(void);
static esp_err_t ads1115_rdy_init(void);

esp_err_t aqm_gpio_intialize(void) {
    esp_err_t err;

    /* Configure GPIOs for outputs */
    err = led_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED pin");
        return err;
    }

    err = relay_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize relay pin");
        return err;
    }

    /* Configure GPIOs for inputs with interrupts */
    err = ads1115_rdy_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADS1115 RDY pins");
        return err;
    }

    ESP_LOGI(TAG, "All GPIOs initialized successfully");
    return ESP_OK;
}

static esp_err_t led_init(void){
    esp_err_t err = gpio_reset_pin(LED_PIN);
    if (err != ESP_OK) return err;
    err = gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    return err;
}

static esp_err_t relay_init(void){
    esp_err_t err = gpio_reset_pin(RELAY_PIN);
    if (err != ESP_OK) return err;
    err = gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    return err;
}

static esp_err_t ads1115_rdy_init(void){
    // Configure both ADC RDY pins simultaneously using a configuration structure
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Interrupt on falling edge (Active Low)
        .pin_bit_mask = (1ULL << ADC_1_RDY_PIN) | (1ULL << ADC_2_RDY_PIN), // Bit mask of the pins
        .mode = GPIO_MODE_INPUT,        // Set as input
        .pull_up_en = 1,                // Enable internal pull-up
        .pull_down_en = 0               // Disable internal pull-down
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) return err;

    // Install GPIO ISR service to allow individual pin handlers
    // 0 defines default interrupt allocation flags
    err = gpio_install_isr_service(0);
    
    // ESP_ERR_INVALID_STATE means the ISR service is already installed somewhere else
    // in your project (e.g., by another driver). This is perfectly fine, we just ignore it.
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service");
        return err;
    }

    return ESP_OK;
}