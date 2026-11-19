#include "zb_dimmer.h"

#include <string.h>
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "light.h"
#include "sdkconfig.h"
#include "settings.h"
#include "zb_ota.h"

static const char *TAG = "zb";

/* Custom cluster (manufacturer range 0xFC00-0xFFFF) for the settings that have
 * no standard attribute. Written from the Z2M dev console for now, later from
 * the external converter. See docs/ZIGBEE.md. */
#define CFG_CLUSTER_ID        0xFC00
#define CFG_ATTR_MIN_OUTPUT   0x0000   /* u16, units of 0.01 % (500 = 5 %) */
#define CFG_ATTR_GAMMA_X10    0x0001   /* u8, 10..40 */
#define CFG_ATTR_HOLD_RAMP_MS 0x0002   /* u16, 1000..20000 */

static volatile bool s_started;     /* esp_zb_start() has run: attributes exist */
static uint32_t s_steer_retry_ms = 1000;
static bool s_was_joined;

/* ---------- helpers ---------- */

/* ZCL string: first byte is the length */
static void zcl_str(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n > cap - 2) {
        n = cap - 2;
    }
    dst[0] = (char)n;
    memcpy(dst + 1, src, n);
    dst[n + 1] = 0;
}

static void push_state_locked(bool on, uint8_t level)
{
    esp_zb_zcl_set_attribute_val(DIMMER_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &on, false);
    esp_zb_zcl_set_attribute_val(DIMMER_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, &level, false);
}

void zb_dimmer_report(bool on, uint8_t level)
{
    if (!s_started) {
        return;
    }
    if (esp_zb_lock_acquire(pdMS_TO_TICKS(200))) {
        push_state_locked(on, level);
        esp_zb_lock_release();
    }
}

void zb_dimmer_factory_reset(void)
{
    if (!s_started) {
        esp_restart();
    }
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_factory_reset();        /* reinicia: no vuelve */
    esp_zb_lock_release();
}

/* ---------- attributes written from the network ---------- */

static uint32_t onoff_fade_ms(void)
{
    return (uint32_t)settings_onoff_transition() * 100;
}

static esp_err_t on_set_attr(const esp_zb_zcl_set_attr_value_message_t *m)
{
    ESP_RETURN_ON_FALSE(m, ESP_FAIL, TAG, "empty message");
    ESP_RETURN_ON_FALSE(m->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG,
                        "status %d", m->info.status);
    if (m->info.dst_endpoint != DIMMER_ENDPOINT || !m->attribute.data.value) {
        return ESP_OK;
    }

    uint16_t cl = m->info.cluster;
    uint16_t id = m->attribute.id;
    const void *v = m->attribute.data.value;
    bool on;
    uint8_t level;
    light_get(&on, &level);

    if (cl == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
        if (id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) {
            bool new_on = *(const bool *)v;
            ESP_LOGI(TAG, "[%lu] on_off = %d", (unsigned long)esp_log_timestamp(), new_on);
            light_set(new_on, level, onoff_fade_ms());
        } else if (id == ESP_ZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF) {
            settings_set_startup_onoff(*(const uint8_t *)v);
            ESP_LOGI(TAG, "StartUpOnOff = 0x%02x", *(const uint8_t *)v);
        }
    } else if (cl == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL) {
        if (id == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID) {
            uint8_t new_level = *(const uint8_t *)v;
            /* Logged with a timestamp to see whether the stack runs a
             * transition in steps (many calls) or jumps to the final value. */
            ESP_LOGI(TAG, "[%lu] level = %u", (unsigned long)esp_log_timestamp(), new_level);
            light_set(on, new_level, CONFIG_DIMMER_LEVEL_SMOOTH_MS);
        } else if (id == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_START_UP_CURRENT_LEVEL_ID) {
            settings_set_startup_level(*(const uint8_t *)v);
            ESP_LOGI(TAG, "StartUpCurrentLevel = 0x%02x", *(const uint8_t *)v);
        } else if (id == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_ON_OFF_TRANSITION_TIME_ID) {
            settings_set_onoff_transition(*(const uint16_t *)v);
            ESP_LOGI(TAG, "OnOffTransitionTime = %u ds", *(const uint16_t *)v);
        }
    } else if (cl == CFG_CLUSTER_ID) {
        if (id == CFG_ATTR_MIN_OUTPUT) {
            uint16_t val = *(const uint16_t *)v;
            if (val > LIGHT_MIN_OUTPUT_MAX) {
                val = LIGHT_MIN_OUTPUT_MAX;
            }
            settings_set_min_output(val);
            light_set_curve(val, settings_gamma_x10());
        } else if (id == CFG_ATTR_GAMMA_X10) {
            uint8_t val = *(const uint8_t *)v;
            val = val < 10 ? 10 : (val > 40 ? 40 : val);
            settings_set_gamma_x10(val);
            light_set_curve(settings_min_output(), val);
        } else if (id == CFG_ATTR_HOLD_RAMP_MS) {
            uint16_t val = *(const uint16_t *)v;
            val = val < 1000 ? 1000 : (val > 20000 ? 20000 : val);
            settings_set_hold_ramp_ms(val);
            ESP_LOGI(TAG, "button ramp = %u ms", val);
        }
    } else {
        ESP_LOGD(TAG, "attr cluster 0x%04x id 0x%04x", cl, id);
    }
    return ESP_OK;
}

static esp_err_t action_handler(esp_zb_core_action_callback_id_t cb_id, const void *msg)
{
    switch (cb_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        return on_set_attr((const esp_zb_zcl_set_attr_value_message_t *)msg);
    case ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID:
        return zb_ota_on_value((const esp_zb_zcl_ota_upgrade_value_message_t *)msg);
    case ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID:
        return zb_ota_on_query_resp((const esp_zb_zcl_ota_upgrade_query_image_resp_message_t *)msg);
    case ESP_ZB_CORE_IDENTIFY_EFFECT_CB_ID: {
        const esp_zb_zcl_identify_effect_message_t *m = msg;
        ESP_LOGI(TAG, "effect 0x%02x (variant %u)", m->effect_id, m->effect_variant);
        light_trigger_effect(m->effect_id);
        return ESP_OK;
    }
    default:
        ESP_LOGD(TAG, "unhandled action 0x%x", cb_id);
        return ESP_OK;
    }
}

static void identify_cb(uint8_t identify_on)
{
    ESP_LOGI(TAG, "identify %s", identify_on ? "ON" : "OFF");
    light_identify(identify_on != 0);
}

/* ---------- network ---------- */

static void commissioning_cb(uint8_t mode)
{
    esp_err_t err = esp_zb_bdb_start_top_level_commissioning(mode);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "commissioning 0x%x failed: %s", mode, esp_err_to_name(err));
    }
}

static void on_joined(void)
{
    esp_zb_ieee_addr_t epid;
    esp_zb_get_extended_pan_id(epid);
    ESP_LOGI(TAG, "joined: PAN 0x%04hx, channel %d, addr 0x%04hx, EPID %02x%02x%02x%02x%02x%02x%02x%02x",
             esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address(),
             epid[7], epid[6], epid[5], epid[4], epid[3], epid[2], epid[1], epid[0]);
    s_steer_retry_ms = 1000;

    /* Make the attributes match what the output shows. */
    bool on;
    uint8_t level;
    light_get(&on, &level);
    push_state_locked(on, level);

    zb_ota_start_queries();
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p = signal_struct->p_app_signal;
    esp_err_t st = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig = *p;

    switch (sig) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "stack ready, initialising");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (st != ESP_OK) {
            ESP_LOGW(TAG, "start failed (%s), retrying", esp_err_to_name(st));
            esp_zb_scheduler_alarm(commissioning_cb, ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
            break;
        }
        /* The stack started: a freshly updated firmware is good to keep. */
        zb_ota_mark_valid();
        if (esp_zb_bdb_is_factory_new()) {
            ESP_LOGI(TAG, "no network: searching to join (permit join on the coordinator)");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGI(TAG, "rebooted with a stored network");
            s_was_joined = true;
            on_joined();
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (st == ESP_OK) {
            on_joined();
            if (!s_was_joined) {
                s_was_joined = true;
                light_blink(2);             /* confirms pairing */
            }
        } else {
            ESP_LOGI(TAG, "no network found (%s), retry in %lu ms", esp_err_to_name(st),
                     (unsigned long)s_steer_retry_ms);
            esp_zb_scheduler_alarm(commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING,
                                   s_steer_retry_ms);
            if (s_steer_retry_ms < 30000) {
                s_steer_retry_ms *= 2;
            }
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_LEAVE: {
        esp_zb_zdo_signal_leave_params_t *lp = esp_zb_app_signal_get_params(p);
        if (lp && lp->leave_type == ESP_ZB_NWK_LEAVE_TYPE_RESET) {
            ESP_LOGW(TAG, "removed from the network: resetting to pair again");
            esp_zb_factory_reset();
        }
        break;
    }

    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
        if (st == ESP_OK) {
            uint8_t *t = esp_zb_app_signal_get_params(p);
            ESP_LOGI(TAG, "permit join %u s", t ? *t : 0);
        }
        break;

    default:
        ESP_LOGI(TAG, "signal 0x%x, status %s", sig, esp_err_to_name(st));
        break;
    }
}

/* ---------- data model ---------- */

static esp_zb_cluster_list_t *build_clusters(bool on, uint8_t level)
{
    const esp_app_desc_t *app = esp_app_get_description();
    static char manuf[34], model[34], sw[34], date[18];
    zcl_str(manuf, sizeof(manuf), CONFIG_DIMMER_MANUFACTURER);
    zcl_str(model, sizeof(model), CONFIG_DIMMER_MODEL);
    zcl_str(sw, sizeof(sw), app->version);
    char d[17];
    snprintf(d, sizeof(d), "%s", app->date);   /* "Nov 16 2026", max 16 chars */
    zcl_str(date, sizeof(date), d);

    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE,
    };
    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manuf);
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model);
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, sw);
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, date);

    esp_zb_identify_cluster_cfg_t id_cfg = {.identify_time = 0};
    esp_zb_groups_cluster_cfg_t gr_cfg = {.groups_name_support_id = 0};
    esp_zb_scenes_cluster_cfg_t sc_cfg = {0};

    esp_zb_on_off_cluster_cfg_t oo_cfg = {.on_off = on};
    esp_zb_attribute_list_t *onoff = esp_zb_on_off_cluster_create(&oo_cfg);
    uint8_t su_onoff = settings_startup_onoff();
    esp_zb_on_off_cluster_add_attr(onoff, ESP_ZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF, &su_onoff);

    esp_zb_level_cluster_cfg_t lv_cfg = {.current_level = level};
    esp_zb_attribute_list_t *lvl = esp_zb_level_cluster_create(&lv_cfg);
    uint8_t min_l = LIGHT_LEVEL_MIN, max_l = LIGHT_LEVEL_MAX;
    uint16_t tt = settings_onoff_transition();
    uint8_t su_level = settings_startup_level();
    uint8_t options = 0;
    esp_zb_level_cluster_add_attr(lvl, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_MIN_LEVEL_ID, &min_l);
    esp_zb_level_cluster_add_attr(lvl, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_MAX_LEVEL_ID, &max_l);
    esp_zb_level_cluster_add_attr(lvl, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_OPTIONS_ID, &options);
    esp_zb_level_cluster_add_attr(lvl, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_ON_OFF_TRANSITION_TIME_ID, &tt);
    esp_zb_level_cluster_add_attr(lvl, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_START_UP_CURRENT_LEVEL_ID, &su_level);

    esp_zb_cluster_list_t *cl = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(cl, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(cl, esp_zb_identify_cluster_create(&id_cfg),
                                             ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_groups_cluster(cl, esp_zb_groups_cluster_create(&gr_cfg),
                                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_scenes_cluster(cl, esp_zb_scenes_cluster_create(&sc_cfg),
                                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_on_off_cluster(cl, onoff, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_level_cluster(cl, lvl, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    uint16_t min_out = settings_min_output();
    uint8_t gamma = settings_gamma_x10();
    uint16_t ramp = settings_hold_ramp_ms();
    esp_zb_attribute_list_t *cfg = esp_zb_zcl_attr_list_create(CFG_CLUSTER_ID);
    esp_zb_custom_cluster_add_custom_attr(cfg, CFG_ATTR_MIN_OUTPUT, ESP_ZB_ZCL_ATTR_TYPE_U16,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &min_out);
    esp_zb_custom_cluster_add_custom_attr(cfg, CFG_ATTR_GAMMA_X10, ESP_ZB_ZCL_ATTR_TYPE_U8,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &gamma);
    esp_zb_custom_cluster_add_custom_attr(cfg, CFG_ATTR_HOLD_RAMP_MS, ESP_ZB_ZCL_ATTR_TYPE_U16,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &ramp);
    esp_zb_cluster_list_add_custom_cluster(cl, cfg, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    zb_ota_add_cluster(cl);
    return cl;
}

static void zb_task(void *arg)
{
    esp_zb_cfg_t cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,
        .install_code_policy = false,
        .nwk_cfg.zczr_cfg = {.max_children = 10},
    };
    esp_zb_init(&cfg);

    bool on;
    uint8_t level;
    light_get(&on, &level);

    esp_zb_ep_list_t *eps = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep = {
        .endpoint = DIMMER_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_DIMMABLE_LIGHT_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(eps, build_clusters(on, level), ep);
    ESP_ERROR_CHECK(esp_zb_device_register(eps));

    esp_zb_core_action_handler_register(action_handler);
    esp_zb_identify_notify_handler_register(DIMMER_ENDPOINT, identify_cb);
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));
    s_started = true;
    esp_zb_stack_main_loop();
    vTaskDelete(NULL);
}

void zb_dimmer_start(void)
{
    esp_zb_platform_config_t pcfg = {
        .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&pcfg));

    xTaskCreate(zb_task, "zigbee", 8192, NULL, 5, NULL);
}
