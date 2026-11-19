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

## When the repository goes public: updates from releases

Today the `.ota` and its index are copied by hand into the Zigbee2MQTT data
folder. With a public repository, each GitHub release can serve them:

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
3. **ZHA too.** zigpy (ZHA) can read a Zigbee2MQTT-format index as an extra
   OTA provider, so the same URL should serve Home Assistant users without
   Zigbee2MQTT. To be checked.
4. **Fully official, later.** Submitting the converter to
   zigbee-herdsman-converters makes the device supported out of the box,
   and submitting images to Koenkk/zigbee-OTA puts them in the official
   index: updates with no configuration at all. Each image needs a pull
   request there, so it fits stable versions, not every build. The license
   allows that redistribution (non-commercial); keep the `modelId` in the
   index entries, since manufacturer code 0x131B is Espressif's, shared.

Delta OTA fits on top: the release would carry a patch from the previous
version plus the full image as fallback.

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
