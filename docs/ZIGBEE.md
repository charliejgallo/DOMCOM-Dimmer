# Zigbee

## Device

| | |
|---|---|
| Role | Router (DC powered, strengthens the mesh) |
| Endpoint | 1, Home Automation profile, *Dimmable Light* (0x0101) |
| Manufacturer / model | `DOMCOM` / `DOMCOM-Dimmer` (Basic 0x0004 / 0x0005) |
| Power source | DC source (0x04) |
| Firmware version | Basic `swBuildId` (0x4000) and `dateCode` (0x0006) |
| Channels | 11–26 |
| Stack | Espressif esp-zigbee-lib 1.6.8 / esp-zboss-lib 1.6.4 |

### Server clusters

| Cluster | Attributes beyond the mandatory ones |
|---|---|
| Basic (0x0000) | manufacturer, model, `swBuildId`, `dateCode` |
| Identify (0x0003) | Trigger Effect, see [Effects](#effects) |
| Groups (0x0004) | |
| Scenes (0x0005) | not handled by the application yet |
| On/Off (0x0006) | `StartUpOnOff` (0x4003) |
| Level Control (0x0008) | `MinLevel` 1, `MaxLevel` 254, `Options`, `OnOffTransitionTime` (0x0010), `StartUpCurrentLevel` (0x4000) |
| DOMCOM settings (0xFC00) | see [Settings cluster](#settings-cluster-0xfc00) |

Client cluster: OTA Upgrade (0x0019), manufacturer code 0x131B, image type
0x4443; see [OTA.md](OTA.md).

## Zigbee2MQTT

Without anything extra, Zigbee2MQTT generates a definition from the
clusters ("Generated support") and exposes state, brightness, *power-on
behaviour*, identify, effects and link quality.

The external converter in [`extras/z2m/domcom_dimmer.js`](../extras/z2m/domcom_dimmer.js)
adds the device description and the [settings](#settings-cluster-0xfc00)
as configuration entities, which Home Assistant shows under the device's
*Configuration*:

| Entity | Unit | Range | Attribute |
|---|---|---|---|
| `minimum_output` | % | 0–25, step 0.01 | 0xFC00 / 0x0000 |
| `gamma` | | 1.0–4.0, step 0.1 | 0xFC00 / 0x0001 |
| `button_ramp_time` | s | 1–20, step 0.1 | 0xFC00 / 0x0002 |

### Installing the converter

1. Copy `extras/z2m/domcom_dimmer.js` into the `external_converters` folder
   of the Zigbee2MQTT data directory (next to `configuration.yaml`; create
   the folder if it does not exist).
2. Restart Zigbee2MQTT. The log lists the converter as loaded.
3. On the device page, *Reconfigure* (the yellow button). The device does
   not need to be paired again; *Reconfigure* reads the current settings so
   that they show up.

Written for Zigbee2MQTT 2.x and checked against zigbee-herdsman-converters
26.90.0 (the version in Zigbee2MQTT 2.13).

After flashing a new firmware, Zigbee2MQTT keeps showing the version it
read when the device was paired: it reads the Basic cluster only during the
interview. Use *Interview* on the device page (the cyan "i" button) to read
it again. It does not unpair the device. Checked: after *Interview* the
device page went from 0.1.0 to 0.2.1.

## Behaviour

- **Pairing:** a device with no network searches on its own, with retries
  backing off up to 30 s. Use *Permit join* on the coordinator. The strip
  blinks twice when it joins.
- **On/off** uses the fade in `OnOffTransitionTime` (default 0.4 s).
- **Brightness:** transitions (`transition` in Zigbee2MQTT, the transition
  time of Move to Level) are run by the Zigbee stack, which steps
  `CurrentLevel` over the requested time; the firmware smooths each step over
  150 ms. Checked on the board: a 5 s transition fades smoothly.
- **Power-on behaviour** (`StartUpOnOff`): off, on, toggle or previous
  (default). `StartUpCurrentLevel`: minimum, previous (default) or a fixed
  level. Both are stored in flash; the state is saved 3 s after the last
  change.
- **Local buttons** update the attributes, so reports go out: Home Assistant
  follows changes made at the wall.

## Effects

Identify > *Trigger Effect* (ZCL 3.5.2.3.3), the variants for a non-colour
light:

| Effect | Id | What it does |
|---|---|---|
| `blink` | 0x00 | on 0.5 s, off 0.5 s, back to the state |
| `breathe` | 0x01 | up and down in 1 s, 15 times |
| `okay` | 0x02 | two flashes |
| `channel_change` | 0x0b | maximum 0.5 s, then minimum 7.5 s |
| `finish_effect` | 0xfe | ends breathe at the end of the current cycle; stops identify |
| `stop_effect` | 0xff | stops right away |

*Identify* (IdentifyTime) blinks the strip once a second while it lasts.
Effects never change the stored state: the output returns to it when they
end.

## Settings cluster (0xFC00)

A custom cluster, in the manufacturer-specific range, for settings that
have no standard attribute. Values are clamped to the range and stored in
flash.

| Attribute | Type | Meaning | Range | Default |
|---|---|---|---|---|
| 0x0000 | uint16 | Output at level 1, in units of 0.01 % (500 = 5 %) | 0–2500 | 500 |
| 0x0001 | uint8 | Gamma × 10 | 10–40 | 28 |
| 0x0002 | uint16 | Full ramp time while holding a button, ms | 1000–20000 | 4000 |

Output curve:

```
output = min + (1 − min) · ((level − 1) / 253) ^ gamma
```

With the [external converter](#installing-the-converter) they are regular
settings in Zigbee2MQTT and Home Assistant. Without it, write them from
Zigbee2MQTT: device page → *Dev console* → endpoint `1`, cluster `64512`
(0xFC00), the attribute id, the raw value, *Write*. No manufacturer code is
needed.

How to pick the minimum: set brightness to 1 (the lowest the slider allows)
and raise the minimum until the strip just glows. On the legacy board that
is ~500; see [HARDWARE.md](HARDWARE.md#dead-zone-below-5--duty).

## Leaving the network

- **Hold the board's BOOT button for 10 s.** The strip blinks three times,
  the device erases its Zigbee storage, restarts and searches for a network.
- **Remove it from Zigbee2MQTT** (*Remove*, without *force*): the
  coordinator sends a leave and the device resets itself the same way.

The external button never resets the device: anyone holding it to dim must
not take the light off the network. The power-cycle reset that existed up to
v0.2.0 was removed for the same reason.
