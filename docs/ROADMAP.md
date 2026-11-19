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
- Zigbee OTA with rollback (v0.3.0): 0.3.0 → 0.3.1 over the air on the
  board, about an hour with Zigbee2MQTT's 50-byte blocks.
- Delta OTA (v0.4.0): patches of 1–6 % of the image, checked against the
  running firmware; readable version numbers. Tested on the board:
  0.4.0 → 0.4.1 as a 10.8 KB patch in about a minute.

To do:

- OTA block size: Zigbee2MQTT sends 50-byte blocks by default (about 1 hour
  for 650 KB). Find the largest size that works without fragmentation
  (*Show update settings* in the OTA dialog) and document it.
- ZHA on a real installation (simulated only); a quirk to expose the
  settings cluster there.
- Scenes: store and recall on/off and level.

Not planned for now:

- Button events as Zigbee actions. Holding the button to dim would also
  fire "long press" events, and someone who does not know about them could
  trigger automations while just dimming the light.

## When the repository goes public: updates from releases

Suggested moment: once the release workflow below works (delta OTA is
already tested on the board), so that the first public version can be
installed and updated without building anything. ZHA is simulated only:
the README asks ZHA users to report through issues.

Items 1 and 2 are done ([RELEASES.md](RELEASES.md)); they need the
repository to be public for the URL to work without a login.

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
