# Wi-Fi firmware (ESPHome)

The same hardware can run either the **Zigbee firmware** (the main one,
`firmware/`) or a **Wi-Fi firmware** built with ESPHome
([`esphome/domcom-dimmer.yaml`](../esphome/domcom-dimmer.yaml)) that talks
to Home Assistant over its native API. The mode is chosen by which
firmware is flashed; the board does not change.

## Why two firmwares and no mode jumper

- ESPHome has native Zigbee on the ESP32-C6, but up to 2026.11 it covers
  sensors, switches and numbers, **not lights**; a single ESPHome YAML for
  both modes is not possible yet.
- A jumper read at power-up would need both firmwares in flash at once,
  each in one of the two app slots. Each firmware's OTA writes to "the
  other slot", so updating one would overwrite the other.

If ESPHome adds Zigbee lights, this can be revisited.

## Zigbee vs Wi-Fi

| | Zigbee firmware | Wi-Fi firmware (ESPHome) |
|---|---|---|
| Talks to | Zigbee2MQTT / ZHA, through a coordinator | Home Assistant (ESPHome API), or the built-in web page |
| PWM | 19.5 kHz, LEDC dithering | 19.5 kHz, LEDC dithering |
| Curve | gamma 2.8, minimum 5 % | gamma 2.8, minimum 5 % |
| Minimum output | setting (cluster 0xFC00) | setting (*Minimum output*) |
| Gamma | setting (cluster 0xFC00) | fixed in the YAML (`gamma_correct`) |
| Button ramp time | setting | setting (*Button ramp time*) |
| Button | press toggles, hold dims | the same |
| BOOT held 10 s | leaves the Zigbee network | factory reset: forgets Wi-Fi and settings |
| Effects | blink, breathe, okay, channel change | Breathe |
| Updates | Zigbee OTA, delta patches | ESPHome OTA over Wi-Fi (encrypted) |
| Power-up state | previous by default, configurable | previous, off if unknown |

Both keep the button out of automations: a long press only dims.

## Installing

1. In Home Assistant, the **ESPHome Device Builder** add-on (or the
   `esphome` command line, 2026.11 or later).
2. Create a device and paste `esphome/domcom-dimmer.yaml`, or copy it next
   to a `secrets.yaml` made from
   [`secrets.example.yaml`](../esphome/secrets.example.yaml): Wi-Fi network
   and password, a fallback hotspot password and an API encryption key
   (`openssl rand -base64 32`). The same key protects OTA uploads.
3. **First flash by USB**: *Install → Plug into this computer* in the
   Device Builder, `esphome run domcom-dimmer.yaml`, or
   [web.esphome.io](https://web.esphome.io) with the factory image the
   Device Builder downloads. From then on, updates go over Wi-Fi.
4. Home Assistant discovers the device; add it with the same API key.

`name_add_mac_suffix` gives every board its own name (`domcom-dimmer-xxxxxx`)
from the same YAML. Pins, frequency, minimum and ramp are `substitutions`
at the top of the file.

Without a known network the device opens a fallback hotspot (the device
name, with the `ap_password`) and a captive portal to choose a network.

## Switching modes

The two firmwares use **different partition tables**, so switching is a
full flash by cable:

- **Zigbee → Wi-Fi:** flash the ESPHome firmware by USB (step 3 above).
  The Device Builder writes a complete image.
- **Wi-Fi → Zigbee:** flash the Zigbee firmware's **factory image**
  (`domcom-dimmer-esp32c6-<ver>-factory.bin` from a release, at 0x0), not
  the build folder: the factory image also clears the areas ESPHome used.
  Then pair the device again.

## Status

The YAML validates and compiles with ESPHome 2026.11.0 for the ESP32-C6
(esp-idf 5.5.6): 56 % of the app slot, 32 % of the RAM. **Not yet tried on
the board.**
