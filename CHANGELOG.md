# Changelog

## 0.3.1 — 2026-11-18

- Each level step from the stack is logged at debug level only (a 5 s
  transition used to print ~50 lines). First version meant to be installed
  over the air, to try OTA on the board.

## 0.3.0 — 2026-11-18

- Firmware updates over Zigbee (OTA Upgrade client, manufacturer 0x131B,
  image type 0x4443): the image goes to the inactive app slot, is verified
  and booted; the device asks for updates once a day.
- Rollback: a new firmware is confirmed when the Zigbee stack starts; if it
  fails before that, the bootloader returns to the previous one. Needs this
  version's bootloader, so v0.3.0 is flashed by cable once.
- `tools/make_ota.py` builds the Zigbee OTA file and a Zigbee2MQTT index;
  `tools/package.sh` runs it. The external converter enables OTA.
- Zigbee2MQTT external converter (`extras/z2m/domcom_dimmer.js`): device
  description, and the minimum output, gamma and button ramp time as
  configuration entities in Home Assistant. Firmware unchanged. Installed
  and working on Zigbee2MQTT.
- Checked: level transitions are run by the Zigbee stack and fade smoothly.
- Checked: *Interview* in Zigbee2MQTT refreshes the firmware version shown
  (0.1.0 → 0.2.1); *Reconfigure* does not.

## 0.2.1 — 2026-11-17

Tested on the legacy board with Zigbee2MQTT on 2026-11-18.

- The external button never resets the device: a long press only dims.
  Only the board's BOOT button, held 10 s, leaves the network.
- Removed the reset by five quick power cycles. Its leftover NVS key is
  erased at boot.
- Code, configuration and documentation in English.

## 0.2.0 — 2026-11-17

- Minimum output 5 % (was 0.1 %): on the legacy board the strip did not glow
  below ~5 % duty, so the bottom third of the slider was dark.
- Settings cluster 0xFC00: minimum output, gamma and button ramp time,
  writable over Zigbee and stored in flash.
- Identify effects: blink, breathe, okay, channel change, finish, stop.
- The board's BOOT button (GPIO9) works as a second button.

## 0.1.1 — 2026-11-16

- PWM at 19.5 kHz (was 4 kHz, audible hum at mid brightness), using the 4
  dithered fractional bits of the LEDC duty register.

## 0.1.0 — 2026-11-16

- First version: Zigbee router, Dimmable Light, local button, persistent
  state with power-on behaviour, identify, reset by holding the button 10 s
  or by five quick power cycles.
