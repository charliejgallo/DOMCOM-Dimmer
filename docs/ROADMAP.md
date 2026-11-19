# Roadmap

| Phase | What | Status |
|---|---|---|
| 1 | Zigbee base firmware (ESP-IDF + esp-zigbee-sdk), tested on the legacy board with jumper wires | **done** (v0.1.1) |
| 2 | Product polish | **in progress** |
| 3 | Wi-Fi mode (ESPHome), Zigbee/Wi-Fi selection | to decide |
| 4 | New PCB | planned |

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
- Zigbee OTA with rollback (v0.3.0); 0.3.0 → 0.3.1 over the air in
  progress on the board.

To do:

- **Delta OTA** (high priority for the user): send only the difference
  from the installed version instead of the whole ~650 KB app. Espressif's
  Zigbee SDK supports it (`CONFIG_ZB_DELTA_OTA`, built on ESP-IDF's
  `esp_delta_ota` with a patch made by `esp_delta_ota_patch_gen`); a small
  change should become tens of KB and an update of a couple of minutes
  instead of ~1 hour. To work out: the patch needs the exact base image,
  so the index must offer the right patch for the installed version (or a
  full image as fallback), and rollback must keep working.
- OTA block size: Zigbee2MQTT sends 50-byte blocks by default (about 1 hour
  for 650 KB). Find the largest size that works without fragmentation
  (*Show update settings* in the OTA dialog) and document it.
- Version encoding: `PROJECT_VER` lands in the low bytes of the file
  version, so Zigbee2MQTT shows it as "Stack 0.3". Use the ZCL layout
  (application release and build in the high bytes) so it reads
  "App 0.3 build 1"; the new numbers are higher than the old ones, so
  updates keep working.

- Check Home Assistant ZHA; a quirk if it needs one.
- Scenes: store and recall on/off and level.
- Button events as Zigbee actions, so automations can use them.

## Phase 3: Wi-Fi (to decide)

Goal: the same hardware usable over Wi-Fi with Home Assistant, ideally
through ESPHome. Options:

- An ESPHome firmware for the C6 (a port of the legacy YAML), flashed
  instead of the Zigbee one.
- A jumper read at boot to choose the mode. The two stacks cannot share the
  2.4 GHz radio comfortably, so it is one mode or the other.
- If ESPHome adds Zigbee lights, a single ESPHome YAML for both modes.

## Phase 4: new PCB

- ESP32-C6 module.
- HW-613 buck converter instead of the Mini360.
- Logic-level MOSFET with a gate driver.
- BOOT button reachable; external button terminal.
- Mode jumper if phase 3 needs it.
