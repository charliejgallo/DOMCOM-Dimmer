# Firmware

ESP-IDF 5.5 project in [`firmware/`](../firmware), target ESP32-C6, with
Espressif's Zigbee SDK (esp-zigbee-lib 1.6.x / esp-zboss-lib 1.6.x, fetched
by the component manager).

## Why ESP-IDF and not ESPHome

ESPHome has native Zigbee on the ESP32-C6 since 2026.5, but up to 2026.10 it
only exposes sensors, binary sensors, switches and numbers; there is no
`light`. A dimmer therefore goes through Espressif's SDK. If ESPHome adds
Zigbee lights, the Wi-Fi phase can be rethought around a single YAML for
both modes.

## Build

```bash
cd firmware
source ~/esp/esp-idf/export.sh
idf.py build
```

`idf.py set-target esp32c6` is only needed if `sdkconfig` is missing (the
target is in `sdkconfig.defaults`). After changing a `Kconfig` default, delete
`sdkconfig` so the new default is picked up.

## Flash

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

The C6 is flashed over its own USB (USB-Serial-JTAG). Check the port is
the right board first: esptool refuses a different chip, but not a
different C6.

For other flashers, `tools/package.sh` (after a build) creates `dist/`:

| | |
|---|---|
| `dist/domcom-dimmer-esp32c6/` | ESP-IDF build folder with `flasher_args.json`: **use it to update** |
| `dist/domcom-dimmer-esp32c6-vX.Y.Z@0x0.bin` | single merged image, written at 0x0: **factory image** |

The two are not equivalent:

- The **folder** writes only the bootloader, the partition table, `otadata`
  and the app. The NVS (state, settings) and `zb_storage` (the Zigbee
  network) survive: the device stays paired.
- The **merged image** fills the gaps between those parts with 0xFF, and
  the NVS and `zb_storage` sit in those gaps: it **erases the settings and
  the Zigbee network**. Use it for a blank chip or a clean start, then pair
  again.

## Configuration

`idf.py menuconfig` → *DOMCOM Dimmer*:

| Option | Default | |
|---|---|---|
| `DIMMER_PWM_GPIO` | 18 | MOSFET gate |
| `DIMMER_PWM_INVERTED` | no | |
| `DIMMER_PWM_FREQ_HZ` | 19500 | resolution follows from 80 MHz / f |
| `DIMMER_BUTTON_GPIO` | 19 | -1 = none |
| `DIMMER_BUTTON_ACTIVE_LOW` | yes | button to GND, internal pull-up |
| `DIMMER_BOOT_BUTTON_GPIO` | 9 | -1 = do not use it |
| `DIMMER_GAMMA_X10` | 28 | also over Zigbee |
| `DIMMER_MIN_DUTY_X10000` | 500 | also over Zigbee |
| `DIMMER_ONOFF_FADE_DS` | 4 | initial `OnOffTransitionTime` |
| `DIMMER_LEVEL_SMOOTH_MS` | 150 | smoothing between level steps |
| `DIMMER_HOLD_RAMP_MS` | 4000 | also over Zigbee |
| `DIMMER_RESET_HOLD_S` | 10 | seconds on BOOT to leave the network |
| `DIMMER_MANUFACTURER` / `DIMMER_MODEL` | `DOMCOM` / `DOMCOM-Dimmer` | Basic cluster |

Settings that also exist over Zigbee take the `menuconfig` value only until
they are written once; after that the flash value wins.

## Code layout

| File | Role |
|---|---|
| `main.c` | NVS, power-up state, starts the modules |
| `light.c` | LEDC PWM, curve, fades and effects, rendered at 200 Hz from an `esp_timer` |
| `button.c` | one task per button: debounce, click, hold ramp, reset on BOOT |
| `settings.c` | NVS storage, power-up rules, delayed state save |
| `zb_dimmer.c` | Zigbee: data model, signals, attribute and effect callbacks |

Notes:

- `light_set()` sets a target; the renderer interpolates from wherever the
  output is. Effects override the output without touching the target.
- The duty is written straight to the LEDC register to use its 4 fractional
  bits (the IDF API only writes the integer part).
- `powf` runs outside the critical section: the C6 has no FPU.
- Local changes reach Zigbee through `zb_dimmer_report()`, which takes the
  stack lock; during a hold ramp it reports every 500 ms and at the end.

## Flash storage

Partitions (4 MB):

| Name | Offset | Size | |
|---|---|---|---|
| nvs | 0x9000 | 24 KB | application settings and state |
| otadata | 0xF000 | 8 KB | |
| phy_init | 0x11000 | 4 KB | |
| zb_storage | 0x12000 | 16 KB | Zigbee network and stack data |
| zb_fct | 0x16000 | 4 KB | Zigbee factory data |
| ota_0 | 0x20000 | 1.875 MB | application |
| ota_1 | 0x200000 | 1.875 MB | reserved for Zigbee OTA |

NVS keys (namespace `dimmer`):

| Key | Type | |
|---|---|---|
| `on`, `lvl` | u8 | last state |
| `su_onoff`, `su_lvl` | u8 | `StartUpOnOff`, `StartUpCurrentLevel` |
| `onoff_tt` | u16 | `OnOffTransitionTime`, tenths of a second |
| `min_out` | u16 | minimum output, units of 0.01 % |
| `gamma` | u8 | gamma × 10 |
| `ramp_ms` | u16 | button ramp, ms |

The `pc` key (power-cycle counter, v0.1.x–v0.2.0) is erased at boot.
