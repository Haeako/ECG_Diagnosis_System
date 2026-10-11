#include "user_button.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define USER_BUTTON_DEBOUNCE_MS 50U

static gpio_num_t button_pin = GPIO_NUM_NC;
static gpio_num_t recording_led_pin = GPIO_NUM_NC;

esp_err_t user_button_init(gpio_num_t new_button_pin, gpio_num_t new_recording_led_pin)
{
    gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << new_button_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&button_config);
    if (ret != ESP_OK) return ret;

    gpio_config_t led_config = {
        .pin_bit_mask = 1ULL << new_recording_led_pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&led_config);
    if (ret != ESP_OK) return ret;

    ret = gpio_set_level(new_recording_led_pin, 0);
    if (ret == ESP_OK) {
        button_pin = new_button_pin;
        recording_led_pin = new_recording_led_pin;
    }
    return ret;
}

bool user_button_take_press(void)
{
    static int stable_level = 1;
    static int previous_level = 1;
    static TickType_t last_change_tick;

    if (button_pin == GPIO_NUM_NC) return false;

    int level = gpio_get_level(button_pin);
    TickType_t now = xTaskGetTickCount();
    if (level != previous_level) {
        previous_level = level;
        last_change_tick = now;
    }
    if (level != stable_level
        && (now - last_change_tick) >= pdMS_TO_TICKS(USER_BUTTON_DEBOUNCE_MS)) {
        stable_level = level;
        return stable_level == 0;
    }
    return false;
}

void user_button_set_recording_led(bool enabled)
{
    if (recording_led_pin != GPIO_NUM_NC) {
        (void)gpio_set_level(recording_led_pin, enabled ? 1 : 0);
    }
}
