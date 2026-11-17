# Changelog

## 0.2.1 — 2026-11-17

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
