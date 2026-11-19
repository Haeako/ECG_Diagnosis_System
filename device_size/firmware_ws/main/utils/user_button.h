#ifndef ECG_UTILS_USER_BUTTON_H
#define ECG_UTILS_USER_BUTTON_H

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"

esp_err_t user_button_init(gpio_num_t button_pin, gpio_num_t recording_led_pin);
bool user_button_take_press(void);
void user_button_set_recording_led(bool enabled);

#endif
