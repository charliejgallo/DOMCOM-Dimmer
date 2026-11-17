#pragma once

/* Local buttons, same behaviour as the original ESPHome dimmer:
 *  - short press: toggle
 *  - hold: dim up or down (the direction alternates on every hold and flips
 *    by itself at either end). With the light off, holding turns it on at
 *    the minimum and ramps up.
 * The board's BOOT button (GPIO9) does the same and, held for
 * CONFIG_DIMMER_RESET_HOLD_S, also leaves the Zigbee network. The external
 * button never resets. */
void button_init(void);
