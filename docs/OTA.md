# Firmware updates over Zigbee (OTA)

Since v0.3.0 the dimmer can be updated from Zigbee2MQTT, without opening it
or plugging in a cable. The light keeps working during the download.

## First install: by cable

Firmware before v0.3.0 has no OTA client, so v0.3.0 goes in once by cable,
with the **build folder** (`dist/domcom-dimmer-esp32c6/`, see
[FIRMWARE.md](FIRMWARE.md#flash)). That also writes the new bootloader,
which rollback needs. The folder keeps the pairing; the merged `@0x0` image
would erase it.

## How it works

- The firmware is a Zigbee **OTA Upgrade client** (cluster 0x0019) with
  manufacturer code **0x131B** (Espressif) and image type **0x4443**
  ("DC", DOMCOM). The file version is `PROJECT_VER` as `0x00AABBCC`
  (0.3.0 → `0x00000300`).
- Once on the network it finds the coordinator's OTA server and asks it
  for an image **once a day**; Zigbee2MQTT can also ask it at any time
  ("Check for updates").
- The image is written to the inactive app slot (`ota_0` / `ota_1`) while
  the light keeps running. When the download ends, `esp_ota_end()` checks
  the image (header, segments, SHA-256) before telling the server it is
  valid; then the device restarts into it.
- **Rollback:** the new firmware boots as "pending". It confirms itself when
  the Zigbee stack starts. If it crashes or resets before that, the
  bootloader goes back to the previous firmware.
- Settings, state and the Zigbee network live outside the app slots and
  survive the update.

## Building an image

`tools/package.sh` (after `idf.py build`) runs `tools/make_ota.py`, which
writes:

```
dist/ota/domcom-dimmer/
  DOMCOM-Dimmer-<version>.ota   Zigbee OTA file: 56-byte header + one element (tag 0) with the app
  index.json                    Zigbee2MQTT override index for that file
```

The manufacturer code and image type come from `sdkconfig`
(`DIMMER_OTA_MANUFACTURER_CODE`, `DIMMER_OTA_IMAGE_TYPE`), the version from
`firmware/CMakeLists.txt`. **Bump `PROJECT_VER` for every image**:
Zigbee2MQTT only offers a version higher than the one running.

The image type keeps each hardware variant's images apart: a board with
different pins or a different driver should get its own image type.

## Zigbee2MQTT setup (once)

1. Copy the `dist/ota/domcom-dimmer` folder into the Zigbee2MQTT data
   directory (next to `configuration.yaml`).
2. Add to `configuration.yaml`:

   ```yaml
   ota:
     zigbee_ota_override_index_location: domcom-dimmer/index.json
   ```

3. Restart Zigbee2MQTT.

The external converter must be installed: it is what tells Zigbee2MQTT
that the device supports OTA ([ZIGBEE.md](ZIGBEE.md#installing-the-converter)).

For each new version, replace the folder's contents with the new
`dist/ota/domcom-dimmer` (the index lists only the newest image). No restart
is needed: Zigbee2MQTT reads the index on every check.

## Updating

Zigbee2MQTT → **OTA** page (or the device page's OTA section) → *Check for
updates* on the dimmer → *Update*. The progress shows in Zigbee2MQTT. At
the end the device restarts (the light returns to its state with the usual
fade) and Zigbee2MQTT shows the new version.

The image is ~650 KB, so the download takes several minutes; it depends on
the route and the traffic on the network.

## Checked

- The `.ota` file and the index parse with zigbee-herdsman 10.8.0 (Zigbee2MQTT
  2.13): header, manufacturer, image type, version, one element.
- No image in the public Zigbee2MQTT index uses manufacturer code 0x131B,
  so no other product's image can be offered to the dimmer.
- On the board: pending (v0.3.0 by cable, then v0.3.1 over the air).
