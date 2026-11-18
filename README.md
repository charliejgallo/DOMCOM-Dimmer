# DOMCOM Dimmer

Zigbee dimmer for single-colour LED strips (12/24 V, low-side MOSFET) on an
ESP32-C6. It works with Zigbee2MQTT and Home Assistant, keeps working
without a network, and has a local push button.

It replaces an ESPHome dimmer on a Wemos D1 mini (ESP8266, Wi-Fi). The old
YAML is kept, without secrets, in [`legacy/`](legacy/dimmer-taller.yaml).

> **Status:** work in progress. The firmware runs on the legacy board
> (hand-wired with jumper cables) and pairs with Zigbee2MQTT. A dedicated PCB
> comes later; see the [roadmap](docs/ROADMAP.md).

## Features

- **Zigbee 3.0 router** (it strengthens the mesh), standard *Dimmable Light*
  (device 0x0101). Zigbee2MQTT picks it up with a generated definition:
  on/off, brightness, power-on behaviour, identify and effects.
- **Silent PWM:** 19.5 kHz, above the audible band, with ~16 effective bits
  (12 bits plus 4 dithered by the C6 LEDC).
- **Brightness curve:** gamma 2.8 (as ESPHome) with an adjustable floor, so
  level 1 already lights the strip.
- **Local button**, same feel as the ESPHome original: press to toggle,
  hold to dim (the direction alternates on every hold).
- **Works offline:** the button and the power-up state never depend on the
  network. The state is saved to flash and restored according to
  *power-on behaviour*.
- **Effects** from the Identify cluster: blink, breathe, okay, channel
  change, finish and stop.
- **Settings over Zigbee** (custom cluster 0xFC00): minimum output, gamma
  and button ramp time, kept in flash. An external converter for
  Zigbee2MQTT ([`extras/z2m`](extras/z2m/domcom_dimmer.js)) shows them in
  Home Assistant.
- Only the board's **BOOT button** (held 10 s) leaves the network; the
  external button never does, so dimming with a long press is always safe.

## Quick start

1. Wire the ESP32-C6 as in [docs/HARDWARE.md](docs/HARDWARE.md#test-wiring):
   GPIO18 to the MOSFET gate, GPIO19 to the button, GND, 5 V.
2. Build and flash ([docs/FIRMWARE.md](docs/FIRMWARE.md)):

   ```bash
   cd firmware
   source ~/esp/esp-idf/export.sh
   idf.py build
   idf.py -p /dev/cu.usbmodemXXXX flash monitor
   ```

   `tools/package.sh` builds `dist/` for other flashers: an ESP-IDF build
   folder (updates keep the pairing) and a merged factory image (erases the
   settings and the Zigbee network).
3. In Zigbee2MQTT, *Permit join*. The strip blinks twice when it joins.
4. Optional: install the [external converter](docs/ZIGBEE.md#installing-the-converter)
   to get the settings in Home Assistant.

## Documentation

| Document | Contents |
|---|---|
| [docs/HARDWARE.md](docs/HARDWARE.md) | Legacy board, test wiring, pin choices, what was measured (hum, MOSFET, minimum) |
| [docs/ZIGBEE.md](docs/ZIGBEE.md) | Endpoint and clusters, behaviour, effects, custom settings cluster, leaving the network |
| [docs/FIRMWARE.md](docs/FIRMWARE.md) | Build, flash, configuration, code layout, flash storage |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Phases: Zigbee base, product polish, Wi-Fi, new PCB |
| [CHANGELOG.md](CHANGELOG.md) | Changes per version |

## License

[PolyForm Noncommercial 1.0.0](LICENSE): free for any non-commercial use,
including personal projects, study and research. Commercial use needs a
separate license from the author.

The firmware links Espressif's esp-zigbee-lib and esp-zboss-lib, which have
their own licenses.
