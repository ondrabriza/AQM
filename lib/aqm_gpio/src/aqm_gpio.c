#include "aqm_gpio.h"
#include "aqm_config.h"
#include "aqm_datastore.h"

#include <esp_timer.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AQM_GPIO";

// --- Forward declarations ---
static esp_err_t aqm_led_init(void);
static esp_err_t aqm_relay_init(void);
static esp_err_t aqm_adcs_rdy_pins_init(void);
static esp_err_t boot_button_init(void);
static void boot_button_isr_handler(void* arg);
static void factory_reset_task(void *pvParameters); // Nový task pro reset

static uint8_t led_state = 0; 
#define DEBOUNCE_TIME_US 200000    // 200 ms pro debounce
#define LONG_PRESS_TIME_US 10000000 // 10 seconds for factory reset

static TaskHandle_t reset_task_handle = NULL;

esp_err_t aqm_gpio_intialize(void) {
    esp_err_t err;

    /* Configure GPIOs for outputs */
    err = aqm_led_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED pin");
        return err;
    }

    err = aqm_relay_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize relay pin");
        return err;
    }

    // Install GPIO ISR service to allow individual pin handlers
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service");
        return err;
    }

    // Vytvoření asynchronního tasku pro obsluhu továrního resetu
    xTaskCreate(factory_reset_task, "factory_reset_task", 4096, NULL, 5, &reset_task_handle);

    /* Configure GPIOs for inputs with interrupts */
    err = aqm_adcs_rdy_pins_init();
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

static esp_err_t aqm_led_init(void){
    esp_err_t err = gpio_reset_pin(LED_PIN);
    if (err != ESP_OK) return err;
    err = gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    led_state = 0;
    return err;
}

static esp_err_t aqm_relay_init(void){
    esp_err_t err = gpio_reset_pin(RELAY_PIN);
    if (err != ESP_OK) return err;
    err = gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    err = gpio_set_level(RELAY_PIN, 1); // Ensure relay is off at startup (active LOW)
    return err;
}

static esp_err_t aqm_adcs_rdy_pins_init(void){
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, 
        .pin_bit_mask = (1ULL << ADC_1_RDY_PIN) | (1ULL << ADC_2_RDY_PIN),
        .mode = GPIO_MODE_INPUT,        
        .pull_up_en = 1,                
        .pull_down_en = 0               
    };
    return gpio_config(&io_conf);
}

static esp_err_t boot_button_init(void){
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,     // Změna na ANYEDGE pro měření délky stisku
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN), 
        .mode = GPIO_MODE_INPUT,            
        .pull_up_en = 0,                    
        .pull_down_en = 0                   
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) return err;

    err = gpio_isr_handler_add(BOOT_BUTTON_PIN, boot_button_isr_handler, (void*) BOOT_BUTTON_PIN);
    return err;
}

static void IRAM_ATTR boot_button_isr_handler(void* arg) {
    static uint64_t press_start_time = 0;
    static bool is_pressed = false;
    
    uint64_t current_time = esp_timer_get_time();
    int pin_level = gpio_get_level(BOOT_BUTTON_PIN); // 0 = stisknuto, 1 = uvolněno

    if (pin_level == 0) { 
        // Hrana dolů (Tlačítko bylo stisknuto)
        if (!is_pressed && (current_time - press_start_time > DEBOUNCE_TIME_US)) {
            press_start_time = current_time;
            is_pressed = true;
            //ESP_LOGI(TAG, "BOOT button pressed");

        }
    } else {
        // Hrana nahoru (Tlačítko bylo uvolněno)
        if (is_pressed) {
            is_pressed = false;
            uint64_t press_duration = current_time - press_start_time;
            //ESP_LOGI(TAG, "BOOT button unpressed");

            if (press_duration >= LONG_PRESS_TIME_US) {
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                if (reset_task_handle != NULL) {
                    vTaskNotifyGiveFromISR(reset_task_handle, &xHigherPriorityTaskWoken);
                    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
                }
            } else if (press_duration >= DEBOUNCE_TIME_US) {
                aqm_led_toggle();
                

            }
        }
    }
}

// --- Task pro bezpečné provedení továrního resetu mimo ISR ---
static void factory_reset_task(void *pvParameters) {
    while (1) {
        // Task spí a čeká, dokud nedostane signál z ISR
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGW(TAG, "Long press detected. Performing factory reset...");

        aqm_datastore_fill_nvs_with_defaults(); // Fills NVS with default values for control word and Wi-Fi config

        ESP_LOGW(TAG, "Factory reset applied. Restarting device in 1 second...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
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

esp_err_t aqm_led_toggle(void) {
    led_state = !led_state;
    return gpio_set_level(LED_PIN, led_state);
}