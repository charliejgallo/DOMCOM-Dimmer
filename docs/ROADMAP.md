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

To do:

- Check Home Assistant ZHA; a quirk if it needs one.
- Zigbee OTA (the second app slot is already in the partition table).
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
