#pragma once
#include <stdbool.h>
#include <stdint.h>

/* StartUpOnOff values (On/Off 0x4003) */
#define STARTUP_ONOFF_OFF      0x00
#define STARTUP_ONOFF_ON       0x01
#define STARTUP_ONOFF_TOGGLE   0x02
#define STARTUP_ONOFF_PREVIOUS 0xFF
/* StartUpCurrentLevel (Level 0x4000): 0x00 = minimum, 0xFF = previous */
#define STARTUP_LEVEL_MIN      0x00
#define STARTUP_LEVEL_PREVIOUS 0xFF

void settings_init(void);

/* Power-up state, with the StartUp* rules applied. */
void settings_boot_state(bool *on, uint8_t *level);

/* Saves the state 3 s after the last change (spares the flash). */
void settings_save_state_later(bool on, uint8_t level);

uint8_t settings_startup_onoff(void);
void settings_set_startup_onoff(uint8_t v);
uint8_t settings_startup_level(void);
void settings_set_startup_level(uint8_t v);
uint16_t settings_onoff_transition(void);      /* tenths of a second */
void settings_set_onoff_transition(uint16_t v);

/* Curve and button settings (custom cluster 0xFC00). */
uint16_t settings_min_output(void);            /* units of 0.01 % */
void settings_set_min_output(uint16_t v);
uint8_t settings_gamma_x10(void);
void settings_set_gamma_x10(uint8_t v);
uint16_t settings_hold_ramp_ms(void);
void settings_set_hold_ramp_ms(uint16_t v);
