#include "aqm_gpio.h"
#include "aqm_config.h"

#include <esp_timer.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>

static const char *TAG = "AQM_GPIO";

// --- Forward declarations ---
static esp_err_t led_init(void);
static esp_err_t relay_init(void);
static esp_err_t ads1115_rdy_init(void);
static esp_err_t boot_button_init(void);
static void boot_button_isr_handler(void* arg);

static uint8_t led_state = 0; // Example state variable for LED
#define DEBOUNCE_TIME_US 200000 // Debounce time in milliseconds

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
        // Install GPIO ISR service to allow individual pin handlers
    // 0 defines default interrupt allocation flaags
    err = gpio_install_isr_service(0);
    
    // ESP_ERR_INVALID_STATE means the ISR service is already installed somewhere else
    // in your project (e.g., by another driver). This is perfectly fine, we just ignore it.
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service");
        return err;
    }


    /* Configure GPIOs for inputs with interrupts */
    err = ads1115_rdy_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADS1115 RDY pins");
        return err;
    }

    err = boot_button_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BOOT button pin");
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
    err = gpio_set_level(RELAY_PIN, 1); // Ensure relay is off at startup (active LOW)
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
    
    esp_err_t err = ESP_OK;
    err = gpio_config(&io_conf);

    return err;
}


static esp_err_t boot_button_init(void){
    // Configure BOOT button pin
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,     // Interrupt on falling edge (press for Active Low)
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN), // Bit mask of the pin
        .mode = GPIO_MODE_INPUT,            // Set as input
        .pull_up_en = 0,                    // Enable internal pull-up for Active Low
        .pull_down_en = 0                   // Disable internal pull-down
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) return err;

    // Hook isr handler for specific gpio pin
    err = gpio_isr_handler_add(BOOT_BUTTON_PIN, boot_button_isr_handler, (void*) BOOT_BUTTON_PIN);
    
    return err;
}

static void IRAM_ATTR boot_button_isr_handler(void* arg) {
    static uint64_t last_isr_time = 0;
    uint64_t current_time = esp_timer_get_time();
    // Debounce: Ignore interrupts that occur within 200ms of the last one
    if (current_time - last_isr_time < DEBOUNCE_TIME_US) { 
        return;
    }
    last_isr_time = current_time;
    led_state = !led_state; // Toggle LED state
    gpio_set_level(LED_PIN, led_state);
}


esp_err_t aqm_relay_turn_on(void) {
    return gpio_set_level(RELAY_PIN, 0); // Active LOW
}

esp_err_t aqm_relay_turn_off(void) {
    return gpio_set_level(RELAY_PIN, 1); // Active LOW
}

esp_err_t aqm_led_turn_on(void) {
    return gpio_set_level(LED_PIN, 1); // Active HIGH
}

esp_err_t aqm_led_turn_off(void) {
    return gpio_set_level(LED_PIN, 0); // Active HIGH
}
