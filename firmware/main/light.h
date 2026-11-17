#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LIGHT_LEVEL_MIN 1
#define LIGHT_LEVEL_MAX 254

/* Upper bound of the minimum output, in units of 0.01 % (25 %). */
#define LIGHT_MIN_OUTPUT_MAX 2500

/* Identify > Trigger Effect effect ids (ZCL 3.5.2.3.3). */
#define LIGHT_EFFECT_BLINK          0x00
#define LIGHT_EFFECT_BREATHE        0x01
#define LIGHT_EFFECT_OKAY           0x02
#define LIGHT_EFFECT_CHANNEL_CHANGE 0x0b
#define LIGHT_EFFECT_FINISH         0xfe
#define LIGHT_EFFECT_STOP           0xff

/* PWM output with a gamma curve and its own fades (rendered at 200 Hz).
 * Levels are Zigbee levels (1..254); on/off is independent of the level.
 * Curve: output = min + (1 - min) * ((level - 1) / 253) ^ gamma. */

void light_init(uint16_t min_x10000, uint8_t gamma_x10);
void light_set_curve(uint16_t min_x10000, uint8_t gamma_x10);

/* Sets the target; fade_ms is the time to get there from the current output.
 * Does nothing if the target does not change. */
void light_set(bool on, uint8_t level, uint32_t fade_ms);
void light_get(bool *on, uint8_t *level);

/* Effects override the output without touching the state. */
void light_blink(int times);
void light_identify(bool active);
void light_trigger_effect(uint8_t effect_id);

/* Called on every state change (to persist it). */
typedef void (*light_change_cb_t)(bool on, uint8_t level);
void light_set_change_cb(light_change_cb_t cb);
