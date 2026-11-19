#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "esp_zigbee_core.h"

/* Zigbee OTA Upgrade client (cluster 0x0019). The image is a standard Zigbee
 * OTA file (tools/make_ota.py) whose single element (tag 0x0000) is the
 * ESP-IDF app binary; it is written to the inactive app slot and booted with
 * rollback: if the new firmware does not reach a working Zigbee stack, the
 * bootloader returns to the previous one on the next reset. */

/* File version of the running firmware: PROJECT_VER "a.b.c" as 0x00AABBCC. */
uint32_t zb_ota_running_version(void);

/* Adds the OTA client cluster to the endpoint's cluster list. */
void zb_ota_add_cluster(esp_zb_cluster_list_t *cl);

/* Core action callbacks (ESP_ZB_CORE_OTA_UPGRADE_*). */
esp_err_t zb_ota_on_value(const esp_zb_zcl_ota_upgrade_value_message_t *m);
esp_err_t zb_ota_on_query_resp(const esp_zb_zcl_ota_upgrade_query_image_resp_message_t *m);

/* Once on the network: finds the coordinator's OTA server and starts the
 * periodic image query. Call from the Zigbee task. */
void zb_ota_start_queries(void);

/* Confirms the running image (cancels a pending rollback). */
void zb_ota_mark_valid(void);
