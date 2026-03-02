#ifndef AQM_GPIO_H
#define AQM_GPIO_H
#include <esp_err.h>

esp_err_t aqm_gpio_intialize(void);

esp_err_t aqm_relay_turn_on(void);

esp_err_t aqm_relay_turn_off(void);

esp_err_t aqm_led_turn_on(void);

esp_err_t aqm_led_turn_off(void);


#endif // AQM_GPIO_H