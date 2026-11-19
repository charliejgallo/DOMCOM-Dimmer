# Roadmap

| Phase | What | Status |
|---|---|---|
| 1 | Zigbee base firmware (ESP-IDF + esp-zigbee-sdk), tested on the legacy board with jumper wires | **done** (v0.1.1) |
| 2 | Product polish | **in progress** |
| 3 | Wi-Fi firmware (ESPHome) for the same board | **in progress** |
| 4 | New PCB | specified, design next |

## Phase 1: done

- Pairs with Zigbee2MQTT through a generated definition.
- No hum at 19.5 kHz; MOSFET cold after hours at 50 %.

## Phase 2: product polish

Done:

- Identify effects (v0.2.0).
- Settings cluster 0xFC00: minimum, gamma, ramp (v0.2.0).
- Minimum output at 5 % for the legacy board (v0.2.0).
- Only BOOT leaves the network, no power-cycle reset (v0.2.1, tested).
- External converter for Zigbee2MQTT with the settings as entities
  (installed and working).
- Level transitions: the stack runs them in steps, a 5 s transition fades
  smoothly; nothing to add in the firmware.
- Zigbee OTA with rollback (v0.3.0): 0.3.0 → 0.3.1 over the air on the
  board, about an hour with Zigbee2MQTT's 50-byte blocks.
- Delta OTA (v0.4.0): patches of 1–6 % of the image, checked against the
  running firmware; readable version numbers. Tested on the board:
  0.4.0 → 0.4.1 as a 10.8 KB patch in about a minute.

Phase 2 is complete.

Optional, not planned for now (contributions welcome):

- **Larger OTA blocks.** Zigbee2MQTT sends 50-byte blocks by default.
  Larger blocks would shorten a full-image update, but they load the mesh
  more; on a busy network (dozens of devices) a slower update is the better
  trade, and delta patches already take about a minute.
- **ZHA on a real installation**, and a ZHA quirk to expose the settings
  cluster. ZHA is simulated only; ZHA users are asked to test and report
  through issues.
- **Zigbee scenes** (store and recall on/off and level).
- **Button events as Zigbee actions.** Holding the button to dim would also
  fire "long press" events, and someone who does not know about them could
  trigger automations while just dimming the light.

## Updates from releases (public repository)

Items 1 and 2 are done and working: the repository is public since
2026-11-19 and the board updated from the release URL
([RELEASES.md](RELEASES.md)).

1. **CI builds the release.** A GitHub Actions workflow on every `v*` tag:
   builds the firmware in the `espressif/idf` container, runs
   `tools/package.sh` and attaches to the release the `.ota`, `index.json`,
   the build folder (zip) and the factory image. No more building on the
   Mac for a release.
2. **A stable index URL.** `index.json` is a release asset with absolute
   URLs to the `.ota` of that same release. Every user points Zigbee2MQTT
   once at
   `https://github.com/charliejgallo/DOMCOM-Dimmer/releases/latest/download/index.json`
   (`ota: zigbee_ota_override_index_location`, which accepts URLs); each
   new release then shows up as an available update by itself, with no
   files to copy. `make_ota.py` needs an option for absolute URLs; the
   index already carries every field a web-hosted image needs (size,
   SHA-512, manufacturer, image type, version, model).
3. **ZHA too.** zigpy (ZHA) reads Zigbee2MQTT-format indexes: `z2m_local`
   for a file (documented in OTA.md, checked with zigpy's code) and `z2m`
   for a URL, so the same release index can serve ZHA users.
4. **Fully official, later.** Submitting the converter to
   zigbee-herdsman-converters makes the device supported out of the box,
   and submitting images to Koenkk/zigbee-OTA puts them in the official
   index: updates with no configuration at all. Each image needs a pull
   request there, so it fits stable versions, not every build. The license
   allows that redistribution (non-commercial); keep the `modelId` in the
   index entries, since manufacturer code 0x131B is Espressif's, shared.

Delta OTA fits on top: the release would carry a patch from the previous
version plus the full image as fallback.

## Phase 3: Wi-Fi

**In progress.** A second firmware for the same hardware, built with
ESPHome ([WIFI.md](WIFI.md)): same pins and behaviour, for Home Assistant
over Wi-Fi. The mode is the firmware that is flashed; there is no mode
jumper, because two firmwares cannot share the two app slots and keep their
OTA updates.

- Done: `esphome/domcom-dimmer.yaml` (validated and compiled with ESPHome
  2026.11).
- To do: try it on the board; possibly build it in the release workflow so
  releases carry its factory image too.
- If ESPHome adds Zigbee lights: revisit a single YAML for both modes.

## Phase 4: new PCB

Specified in [hardware/SPEC.md](../hardware/SPEC.md): two variants of the
same board for JLCPCB / PCBWay (through-hole only, and SMD for PCBA), the
ESP32-C6 Super Mini in a socket, 12/24 V up to 10 A, a gate driver and a
power MOSFET for fast edges (to lower the 5 % floor), the HW-613 buck,
channel 1 populated and footprints for a second (CCT) channel, BOOT and
USB reachable, no mode jumper.

- To do: the KiCad design and fabrication files, then the first boards.
