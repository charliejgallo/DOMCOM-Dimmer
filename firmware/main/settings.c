#include "settings.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "light.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "settings";
static const char *NS = "dimmer";

#define SAVE_DELAY_US      3000000

static nvs_handle_t s_nvs;
static esp_timer_handle_t s_save_timer;

static uint8_t s_startup_onoff = STARTUP_ONOFF_PREVIOUS;
static uint8_t s_startup_level = STARTUP_LEVEL_PREVIOUS;
static uint16_t s_onoff_tt = CONFIG_DIMMER_ONOFF_FADE_DS;
static uint16_t s_min_output = CONFIG_DIMMER_MIN_DUTY_X10000;
static uint8_t s_gamma_x10 = CONFIG_DIMMER_GAMMA_X10;
static uint16_t s_hold_ramp_ms = CONFIG_DIMMER_HOLD_RAMP_MS;
static bool s_saved_on;
static uint8_t s_saved_level = LIGHT_LEVEL_MAX;

static volatile bool s_pending_on;
static volatile uint8_t s_pending_level;

static uint8_t get_u8(const char *key, uint8_t def)
{
    uint8_t v;
    return nvs_get_u8(s_nvs, key, &v) == ESP_OK ? v : def;
}

static void put_u8(const char *key, uint8_t v)
{
    if (nvs_set_u8(s_nvs, key, v) == ESP_OK) {
        nvs_commit(s_nvs);
    }
}

static uint16_t get_u16(const char *key, uint16_t def)
{
    uint16_t v;
    return nvs_get_u16(s_nvs, key, &v) == ESP_OK ? v : def;
}

static void put_u16(const char *key, uint16_t v)
{
    if (nvs_set_u16(s_nvs, key, v) == ESP_OK) {
        nvs_commit(s_nvs);
    }
}

static void save_cb(void *arg)
{
    bool on = s_pending_on;
    uint8_t level = s_pending_level;
    if (on != s_saved_on) {
        nvs_set_u8(s_nvs, "on", on);
        s_saved_on = on;
    }
    if (level != s_saved_level) {
        nvs_set_u8(s_nvs, "lvl", level);
        s_saved_level = level;
    }
    nvs_commit(s_nvs);
    ESP_LOGD(TAG, "state saved: %s %u", on ? "ON" : "OFF", level);
}

void settings_init(void)
{
    ESP_ERROR_CHECK(nvs_open(NS, NVS_READWRITE, &s_nvs));

    s_startup_onoff = get_u8("su_onoff", STARTUP_ONOFF_PREVIOUS);
    s_startup_level = get_u8("su_lvl", STARTUP_LEVEL_PREVIOUS);
    s_saved_on = get_u8("on", 0) != 0;
    s_saved_level = get_u8("lvl", LIGHT_LEVEL_MAX);
    s_onoff_tt = get_u16("onoff_tt", CONFIG_DIMMER_ONOFF_FADE_DS);
    s_min_output = get_u16("min_out", CONFIG_DIMMER_MIN_DUTY_X10000);
    s_gamma_x10 = get_u8("gamma", CONFIG_DIMMER_GAMMA_X10);
    s_hold_ramp_ms = get_u16("ramp_ms", CONFIG_DIMMER_HOLD_RAMP_MS);

    const esp_timer_create_args_t a = {.callback = save_cb, .name = "save"};
    ESP_ERROR_CHECK(esp_timer_create(&a, &s_save_timer));

    /* Leftover of the power-cycle reset (v0.1.x-v0.2.0, removed in v0.2.1). */
    if (nvs_erase_key(s_nvs, "pc") == ESP_OK) {
        nvs_commit(s_nvs);
    }
}

void settings_boot_state(bool *on, uint8_t *level)
{
    switch (s_startup_onoff) {
    case STARTUP_ONOFF_OFF:    *on = false; break;
    case STARTUP_ONOFF_ON:     *on = true; break;
    case STARTUP_ONOFF_TOGGLE: *on = !s_saved_on; break;
    default:                   *on = s_saved_on; break;
    }
    if (s_startup_level == STARTUP_LEVEL_MIN) {
        *level = LIGHT_LEVEL_MIN;
    } else if (s_startup_level == STARTUP_LEVEL_PREVIOUS) {
        *level = s_saved_level;
    } else {
        *level = s_startup_level;
    }
    if (*level < LIGHT_LEVEL_MIN || *level > LIGHT_LEVEL_MAX) {
        *level = LIGHT_LEVEL_MAX;
    }
}

void settings_save_state_later(bool on, uint8_t level)
{
    s_pending_on = on;
    s_pending_level = level;
    esp_timer_stop(s_save_timer);
    esp_timer_start_once(s_save_timer, SAVE_DELAY_US);
}

uint8_t settings_startup_onoff(void) { return s_startup_onoff; }
uint8_t settings_startup_level(void) { return s_startup_level; }
uint16_t settings_onoff_transition(void) { return s_onoff_tt; }

void settings_set_startup_onoff(uint8_t v)
{
    if (v != s_startup_onoff) {
        s_startup_onoff = v;
        put_u8("su_onoff", v);
    }
}

void settings_set_startup_level(uint8_t v)
{
    if (v != s_startup_level) {
        s_startup_level = v;
        put_u8("su_lvl", v);
    }
}

void settings_set_onoff_transition(uint16_t v)
{
    if (v != s_onoff_tt) {
        s_onoff_tt = v;
        put_u16("onoff_tt", v);
    }
}

uint16_t settings_min_output(void) { return s_min_output; }
uint8_t settings_gamma_x10(void) { return s_gamma_x10; }
uint16_t settings_hold_ramp_ms(void) { return s_hold_ramp_ms; }

void settings_set_min_output(uint16_t v)
{
    if (v != s_min_output) {
        s_min_output = v;
        put_u16("min_out", v);
    }
}

void settings_set_gamma_x10(uint8_t v)
{
    if (v != s_gamma_x10) {
        s_gamma_x10 = v;
        put_u8("gamma", v);
    }
}

void settings_set_hold_ramp_ms(uint16_t v)
{
    if (v != s_hold_ramp_ms) {
        s_hold_ramp_ms = v;
        put_u16("ramp_ms", v);
    }
}
