#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DIMMER_ENDPOINT 1

/* Starts the Zigbee stack as a router in its own task. */
void zb_dimmer_start(void);

/* Publishes a local change (buttons) to the ZCL attributes so that reports
 * go out. Safe from any task. */
void zb_dimmer_report(bool on, uint8_t level);

/* Leaves the network, erases the Zigbee storage and restarts. */
void zb_dimmer_factory_reset(void);
