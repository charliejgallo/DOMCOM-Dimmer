/* DOMCOM Dimmer: Zigbee dimmer for single-colour LED strips (ESP32-C6).
 * A Zigbee router exposing a Dimmable Light, with local buttons and
 * persistent state. It works without a network: the buttons and the
 * power-up state do not depend on Zigbee. */

#include "button.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "light.h"
#include "nvs_flash.h"
#include "settings.h"
#include "zb_dimmer.h"

static const char *TAG = "main";

static void on_light_change(bool on, uint8_t level)
{
    settings_save_state_later(on, level);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "DOMCOM Dimmer %s", esp_app_get_description()->version);

    settings_init();

    light_init(settings_min_output(), settings_gamma_x10());
    bool on;
    uint8_t level;
    settings_boot_state(&on, &level);
    light_set(on, level, (uint32_t)settings_onoff_transition() * 100);
    light_set_change_cb(on_light_change);
    ESP_LOGI(TAG, "power-up: %s level %u", on ? "ON" : "OFF", level);

    button_init();
    zb_dimmer_start();
}
