#include "zb_ota.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "zb_dimmer.h"

static const char *TAG = "ota";

#define OTA_QUERY_INTERVAL_MIN 1440     /* ask the server once a day */
#define OTA_MAX_DATA_SIZE      223      /* bytes per Image Block */
#define OTA_HW_VERSION         1
#define ELEMENT_HEADER_LEN     6        /* tag id (2) + length (4) */
#define TAG_UPGRADE_IMAGE      0x0000

static const esp_partition_t *s_part;
static esp_ota_handle_t s_handle;
static bool s_active;

/* The element header can in theory arrive split across blocks, so it is
 * collected byte by byte before the image data starts. */
static uint8_t s_elem_hdr[ELEMENT_HEADER_LEN];
static uint8_t s_elem_hdr_len;
static uint32_t s_elem_len;
static uint32_t s_written;
static uint32_t s_received;
static int64_t s_t0;
static int s_last_pct;

uint32_t zb_ota_running_version(void)
{
    unsigned a = 0, b = 0, c = 0;
    sscanf(esp_app_get_description()->version, "%u.%u.%u", &a, &b, &c);
    return ((uint32_t)(a & 0xFF) << 16) | ((uint32_t)(b & 0xFF) << 8) | (c & 0xFF);
}

void zb_ota_add_cluster(esp_zb_cluster_list_t *cl)
{
    esp_zb_ota_cluster_cfg_t cfg = {
        .ota_upgrade_file_version = zb_ota_running_version(),
        .ota_upgrade_manufacturer = CONFIG_DIMMER_OTA_MANUFACTURER_CODE,
        .ota_upgrade_image_type = CONFIG_DIMMER_OTA_IMAGE_TYPE,
        .ota_upgrade_downloaded_file_ver = 0xFFFFFFFF,
    };
    esp_zb_attribute_list_t *ota = esp_zb_ota_cluster_create(&cfg);

    esp_zb_zcl_ota_upgrade_client_variable_t var = {
        .timer_query = OTA_QUERY_INTERVAL_MIN,
        .hw_version = OTA_HW_VERSION,
        .max_data_size = OTA_MAX_DATA_SIZE,
    };
    uint16_t server_addr = 0xFFFF;
    uint8_t server_ep = 0xFF;
    esp_zb_ota_cluster_add_attr(ota, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID, &var);
    esp_zb_ota_cluster_add_attr(ota, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ADDR_ID, &server_addr);
    esp_zb_ota_cluster_add_attr(ota, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ENDPOINT_ID, &server_ep);
    esp_zb_cluster_list_add_ota_cluster(cl, ota, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);

    ESP_LOGI(TAG, "client: manufacturer 0x%04x, image type 0x%04x, file version 0x%08" PRIx32,
             CONFIG_DIMMER_OTA_MANUFACTURER_CODE, CONFIG_DIMMER_OTA_IMAGE_TYPE,
             zb_ota_running_version());
}

static void reset_state(void)
{
    s_active = false;
    s_elem_hdr_len = 0;
    s_elem_len = 0;
    s_written = 0;
    s_received = 0;
    s_last_pct = -1;
}

static void abort_update(const char *why)
{
    ESP_LOGE(TAG, "update aborted: %s", why);
    if (s_active) {
        esp_ota_abort(s_handle);
    }
    reset_state();
}

/* Strips the element header and writes the image bytes. */
static esp_err_t write_block(const uint8_t *p, uint16_t len)
{
    while (len && s_elem_hdr_len < ELEMENT_HEADER_LEN) {
        s_elem_hdr[s_elem_hdr_len++] = *p++;
        len--;
        if (s_elem_hdr_len == ELEMENT_HEADER_LEN) {
            uint16_t tag = s_elem_hdr[0] | (s_elem_hdr[1] << 8);
            s_elem_len = s_elem_hdr[2] | (s_elem_hdr[3] << 8) | (s_elem_hdr[4] << 16) |
                         ((uint32_t)s_elem_hdr[5] << 24);
            if (tag != TAG_UPGRADE_IMAGE) {
                ESP_LOGE(TAG, "unsupported element tag 0x%04x", tag);
                return ESP_ERR_INVALID_ARG;
            }
            ESP_LOGI(TAG, "image element: %" PRIu32 " bytes", s_elem_len);
        }
    }
    if (!len) {
        return ESP_OK;
    }
    if (s_written + len > s_elem_len) {
        ESP_LOGE(TAG, "more data than the element declares");
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = esp_ota_write(s_handle, p, len);
    if (err == ESP_OK) {
        s_written += len;
    }
    return err;
}

esp_err_t zb_ota_on_value(const esp_zb_zcl_ota_upgrade_value_message_t *m)
{
    if (m->info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "status 0x%x in upgrade %d", m->info.status, m->upgrade_status);
        return ESP_OK;
    }

    esp_err_t err = ESP_OK;
    switch (m->upgrade_status) {
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
        reset_state();
        s_part = esp_ota_get_next_update_partition(NULL);
        if (!s_part) {
            ESP_LOGE(TAG, "no inactive app slot");
            return ESP_FAIL;
        }
        err = esp_ota_begin(s_part, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
            return err;
        }
        s_active = true;
        s_t0 = esp_timer_get_time();
        ESP_LOGI(TAG, "start: version 0x%08" PRIx32 ", %" PRIu32 " bytes, to %s",
                 m->ota_header.file_version, m->ota_header.image_size, s_part->label);
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE:
        if (!s_active) {
            return ESP_FAIL;
        }
        if (m->payload && m->payload_size) {
            err = write_block(m->payload, m->payload_size);
            if (err != ESP_OK) {
                abort_update(esp_err_to_name(err));
                return err;
            }
            s_received += m->payload_size;
            int pct = m->ota_header.image_size ? (int)(100ULL * s_received / m->ota_header.image_size) : 0;
            if (pct / 10 != s_last_pct / 10) {
                s_last_pct = pct;
                ESP_LOGI(TAG, "%d %% (%" PRIu32 "/%" PRIu32 ")", pct, s_received, m->ota_header.image_size);
            }
        }
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY:
        ESP_LOGI(TAG, "apply");
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK:
        if (!s_active || s_elem_hdr_len < ELEMENT_HEADER_LEN || s_written != s_elem_len) {
            ESP_LOGE(TAG, "check failed: %" PRIu32 " of %" PRIu32 " bytes written", s_written, s_elem_len);
            abort_update("incomplete image");
            return ESP_FAIL;
        }
        /* esp_ota_end() verifies the app image (header, segments, SHA-256). */
        err = esp_ota_end(s_handle);
        s_active = false;
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "image rejected: %s", esp_err_to_name(err));
            reset_state();
            return err;
        }
        ESP_LOGI(TAG, "image verified, %" PRIu32 " bytes in %lld s", s_written,
                 (long long)((esp_timer_get_time() - s_t0) / 1000000));
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH:
        err = esp_ota_set_boot_partition(s_part);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGW(TAG, "finished, restarting into version 0x%08" PRIx32, m->ota_header.file_version);
        esp_restart();
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ABORT:
        abort_update("by the server");
        break;

    default:
        ESP_LOGI(TAG, "status %d", m->upgrade_status);
        break;
    }
    return err;
}

esp_err_t zb_ota_on_query_resp(const esp_zb_zcl_ota_upgrade_query_image_resp_message_t *m)
{
    if (m->info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGI(TAG, "no image available (status 0x%x)", m->info.status);
        return ESP_OK;
    }
    ESP_LOGI(TAG, "image offered: version 0x%08" PRIx32 ", %" PRIu32 " bytes",
             m->file_version, m->image_size);
    if (m->image_size == 0 || m->file_version == 0) {
        return ESP_FAIL;      /* reject */
    }
    return ESP_OK;
}

static void server_found(esp_zb_zdp_status_t st, uint16_t addr, uint8_t ep, void *ctx)
{
    if (st != ESP_ZB_ZDP_STATUS_SUCCESS) {
        ESP_LOGI(TAG, "no OTA server on the coordinator");
        return;
    }
    ESP_LOGI(TAG, "OTA server 0x%04x/%u, query every %u min", addr, ep, OTA_QUERY_INTERVAL_MIN);
    esp_zb_ota_upgrade_client_query_interval_set(DIMMER_ENDPOINT, OTA_QUERY_INTERVAL_MIN);
    esp_zb_ota_upgrade_client_query_image_req(addr, ep);
}

void zb_ota_start_queries(void)
{
    static uint16_t clusters[] = {ESP_ZB_ZCL_CLUSTER_ID_OTA_UPGRADE};
    esp_zb_zdo_match_desc_req_param_t req = {
        .dst_nwk_addr = 0x0000,
        .addr_of_interest = 0x0000,
        .profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .num_in_clusters = 1,
        .num_out_clusters = 0,
        .cluster_list = clusters,
    };
    esp_zb_zdo_match_cluster(&req, server_found, NULL);
}

void zb_ota_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware confirmed, rollback cancelled");
    }
}
