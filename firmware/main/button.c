#include "button.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "light.h"
#include "sdkconfig.h"
#include "settings.h"
#include "zb_dimmer.h"

#ifndef CONFIG_DIMMER_BOOT_BUTTON_GPIO
#define CONFIG_DIMMER_BOOT_BUTTON_GPIO -1
#endif
#ifndef CONFIG_DIMMER_BUTTON_ACTIVE_LOW
#define CONFIG_DIMMER_BUTTON_ACTIVE_LOW 0
#endif

static const char *TAG = "button";

#define POLL_MS         10
#define DEBOUNCE_MS     30
#define HOLD_START_MS   500     /* same 0.5 s delay as the original ESPHome YAML */
#define RAMP_TICK_MS    40
#define REPORT_EVERY_MS 500     /* Zigbee reports while the button is held */

typedef struct {
    int gpio;
    bool active_low;
    bool can_reset;     /* holding it CONFIG_DIMMER_RESET_HOLD_S leaves the network */
    const char *name;
} button_cfg_t;

static bool read_pressed(const button_cfg_t *b)
{
    int v = gpio_get_level(b->gpio);
    return b->active_low ? v == 0 : v != 0;
}

static uint32_t onoff_fade_ms(void)
{
    return (uint32_t)settings_onoff_transition() * 100;
}

static void button_task(void *arg)
{
    const button_cfg_t *b = arg;
    bool stable = false;            /* debounced state */
    bool raw_last = false;
    int64_t raw_since = 0;

    int64_t press_t0 = 0;
    bool holding = false;
    bool reset_done = false;
    bool dir_up = true;             /* direction of the last ramp */
    float ramp_level = 0;
    int64_t last_ramp = 0, last_report = 0;
    float step = 1.0f;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        int64_t now = esp_timer_get_time() / 1000;

        bool raw = read_pressed(b);
        if (raw != raw_last) {
            raw_last = raw;
            raw_since = now;
        }
        bool edge = false;
        if (raw != stable && now - raw_since >= DEBOUNCE_MS) {
            stable = raw;
            edge = true;
        }

        if (edge && stable) {                       /* pressed */
            press_t0 = now;
            holding = false;
            reset_done = false;
            continue;
        }

        if (edge && !stable) {                      /* released */
            bool on;
            uint8_t level;
            light_get(&on, &level);
            if (!holding) {
                on = !on;
                light_set(on, level, onoff_fade_ms());
                ESP_LOGI(TAG, "%s: click -> %s", b->name, on ? "ON" : "OFF");
            } else {
                ESP_LOGI(TAG, "%s: ramp ended at %u", b->name, level);
            }
            holding = false;
            zb_dimmer_report(on, level);
            continue;
        }

        if (!stable) {
            continue;
        }

        /* Held */
        int64_t held = now - press_t0;

#if CONFIG_DIMMER_RESET_HOLD_S > 0
        if (b->can_reset && !reset_done && held >= CONFIG_DIMMER_RESET_HOLD_S * 1000LL) {
            reset_done = true;
            ESP_LOGW(TAG, "%s: factory reset", b->name);
            light_blink(3);
            vTaskDelay(pdMS_TO_TICKS(1000));
            zb_dimmer_factory_reset();
        }
#endif

        if (held < HOLD_START_MS) {
            continue;
        }

        if (!holding) {
            holding = true;
            bool on;
            uint8_t level;
            light_get(&on, &level);
            if (!on) {
                level = LIGHT_LEVEL_MIN;
                dir_up = true;
                light_set(true, level, 0);
            } else if (level >= LIGHT_LEVEL_MAX) {
                dir_up = false;
            } else if (level <= LIGHT_LEVEL_MIN) {
                dir_up = true;
            } else {
                dir_up = !dir_up;
            }
            ramp_level = level;
            step = (float)(LIGHT_LEVEL_MAX - LIGHT_LEVEL_MIN) * RAMP_TICK_MS
                   / (float)settings_hold_ramp_ms();
            last_ramp = now;
            last_report = now;
            ESP_LOGI(TAG, "%s: ramp %s from %u", b->name, dir_up ? "up" : "down", level);
        }

        if (now - last_ramp >= RAMP_TICK_MS) {
            last_ramp = now;
            ramp_level += dir_up ? step : -step;
            if (ramp_level > LIGHT_LEVEL_MAX) {
                ramp_level = LIGHT_LEVEL_MAX;
            } else if (ramp_level < LIGHT_LEVEL_MIN) {
                ramp_level = LIGHT_LEVEL_MIN;
            }
            light_set(true, (uint8_t)(ramp_level + 0.5f), RAMP_TICK_MS + 20);
        }

        if (now - last_report >= REPORT_EVERY_MS) {
            last_report = now;
            bool on;
            uint8_t level;
            light_get(&on, &level);
            zb_dimmer_report(on, level);
        }
    }
}

static void start_button(button_cfg_t *b)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << b->gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = b->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = b->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    xTaskCreate(button_task, b->name, 3072, b, 4, NULL);
    ESP_LOGI(TAG, "%s on GPIO%d", b->name, b->gpio);
}

void button_init(void)
{
#if CONFIG_DIMMER_BUTTON_GPIO >= 0
    static button_cfg_t main_btn = {
        .gpio = CONFIG_DIMMER_BUTTON_GPIO,
        .active_low = CONFIG_DIMMER_BUTTON_ACTIVE_LOW,
        /* Never resets: it is the button anyone in the room uses, and a long
         * press to dim must not take the device off the network. */
        .can_reset = false,
        .name = "button",
    };
    start_button(&main_btn);
#endif
#if CONFIG_DIMMER_BOOT_BUTTON_GPIO >= 0
    /* The board's BOOT button is only a strapping pin at power-up; afterwards
     * it is a plain input. It behaves like the external button and is the
     * only one that leaves the network when held. */
    static button_cfg_t boot_btn = {
        .gpio = CONFIG_DIMMER_BOOT_BUTTON_GPIO,
        .active_low = true,
        .can_reset = true,
        .name = "boot",
    };
    start_button(&boot_btn);
#endif
}
