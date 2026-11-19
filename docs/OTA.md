# Firmware updates over Zigbee (OTA)

Since v0.3.0 the dimmer can be updated from Zigbee2MQTT, without opening it
or plugging in a cable. The light keeps working during the download.

Since v0.4.0 an update can be a **delta patch**: only the difference from
the installed firmware travels, typically 1–6 % of the full image. That
turns an update of about an hour into one or a few minutes.

## First install: by cable

Firmware before v0.3.0 has no OTA client, and firmware before v0.4.0
cannot apply patches. The first version with each feature goes in once by
cable, with the **build folder** (`dist/domcom-dimmer-esp32c6/`, see
[FIRMWARE.md](FIRMWARE.md#flash)), or over the air as a full image. The
folder also writes the bootloader, which rollback needs, and keeps the
pairing; the merged `@0x0` image would erase it.

## How it works

- The firmware is a Zigbee **OTA Upgrade client** (cluster 0x0019) with
  manufacturer code **0x131B** (Espressif) and image type **0x4443**
  ("DC", DOMCOM).
- **File version** (v0.4.0 on): the ZCL layout, one byte each from high to
  low: application release, application build, stack release, stack build.
  `PROJECT_VER` a.b.c gives release `a.b` (two nibbles) and build `c`; the
  stack bytes are the esp-zigbee-lib version. 0.4.1 with esp-zigbee-lib
  1.6.8 is `0x04011608`, which Zigbee2MQTT shows as "App 0.4 build 1 |
  Stack 1.6 build 8". So `a` and `b` stay below 16. (v0.3.x used
  `0x00AABBCC`; every new number is higher, so updates from it work.)
- Once on the network it finds the coordinator's OTA server and asks it
  for an image **once a day**; Zigbee2MQTT can also ask it at any time
  ("Check for updates").
- The download goes to the inactive app slot (`ota_0` / `ota_1`) while the
  light keeps running. When it ends, `esp_ota_end()` checks the image
  (header, segments, SHA-256) before telling the server it is valid; then
  the device restarts into it.
- **Rollback:** the new firmware boots as "pending". It confirms itself when
  the Zigbee stack starts. If it crashes or resets before that, the
  bootloader goes back to the previous firmware.
- Settings, state and the Zigbee network live outside the app slots and
  survive the update.

### Full images and delta patches

An OTA file has one element, and its tag says what it carries:

| Tag | Content | Applied by |
|---|---|---|
| `0x0000` (standard) | the whole app binary | written as it arrives |
| `0xF000` (manufacturer range) | an [esp_delta_ota](https://components.espressif.com/components/espressif/esp_delta_ota) patch | rebuilt on the device from the running app |

A patch is a 64-byte header (magic `0xfccdde10`, the SHA-256 of the app it
was made from, reserved bytes) plus a detools patch compressed with
heatshrink. The device:

1. compares that SHA-256 with its running app's (`esp_partition_get_sha256`)
   **before writing anything**, and refuses a patch made for another
   firmware;
2. applies the patch block by block: it reads the running app from its own
   slot and writes the result into the other slot;
3. verifies the result with `esp_ota_end()` like a full image. A patch that
   produced anything other than the exact new firmware fails there, and the
   device keeps running the old one.

## Building images

After `idf.py build`, `tools/package.sh` runs `tools/make_ota.py`, which:

- **archives** the build in `archive/<version>/` (app binary and
  `meta.json`). Those are the bases for later patches, so the archive must
  hold exactly what was distributed: if `archive/<version>` already has a
  different build, it stops and asks to bump `PROJECT_VER` (or
  `--replace-archive` for a version never handed out). `archive/` is not in
  git; keep it backed up;
- makes a **patch from each archived version** from 0.4.0 on, checks it by
  applying it on the computer and comparing with the new image, and skips
  it if it is larger than 70 % of the full image;
- writes the **full image** and the **index**:

```
dist/ota/domcom-dimmer/
  DOMCOM-Dimmer-0.4.0-to-0.4.1.ota   patch, offered only to devices on 0.4.0
  DOMCOM-Dimmer-0.4.1.ota            full image, for any other version
  index.json                         Zigbee2MQTT override index
```

Zigbee2MQTT takes the **first** index entry that matches the device, and
each patch entry is limited to its base version (`minFileVersion` =
`maxFileVersion`), so patches come first and the full image last.

Patches need `detools` on the computer:

```bash
python3 -m venv .venv
.venv/bin/pip install -r tools/requirements.txt
```

`package.sh` uses `.venv` when it exists. `make_ota.py --no-delta` builds
the full image only; `--base-url` writes absolute URLs into the index, for
web hosting (see the [roadmap](ROADMAP.md)).

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
`dist/ota/domcom-dimmer`. No restart is needed: Zigbee2MQTT reads the index
on every check.

## Home Assistant (ZHA)

ZHA's OTA (zigpy) can read the same folder through its `z2m_local`
provider. In Home Assistant's `configuration.yaml`, with the
`domcom-dimmer` folder copied into the Home Assistant config directory:

```yaml
zha:
  zigpy_config:
    ota:
      extra_providers:
        - type: z2m_local
          index_file: /config/domcom-dimmer/index.json
```

Restart Home Assistant; the dimmer's firmware update entity then offers new
versions. **Not tried on a real ZHA**: checked with zigpy 2.4.1's own code
(the configuration is accepted, the index validates against its schema,
both files parse, and its selection gives the patch to a device on 0.4.0,
the full image to one on 0.3.1 and nothing to one on 0.4.1). Each index
entry carries `path` (relative to the index, read by zigpy) besides `url`
(relative to the Zigbee2MQTT data folder, read by Zigbee2MQTT).

## Updating

Zigbee2MQTT → **OTA** page (or the device page's OTA section) → *Check for
updates* on the dimmer → *Update*. The progress shows in Zigbee2MQTT. At
the end the device restarts (the light returns to its state with the usual
fade) and Zigbee2MQTT shows the new version.

Zigbee2MQTT sends 50-byte blocks by default. A full image (~680 KB) takes
about **an hour**, a patch **about a minute** (both measured on the board,
one hop from the coordinator). The light works normally meanwhile.

In the OTA dialog the index path has to be typed again every time it opens
(`domcom-dimmer/index.json` in *Custom index*), or pick the official index
as source: the override index from `configuration.yaml` is added to it.

## Checked

- The `.ota` files and the index parse with zigbee-herdsman 10.8.0
  (Zigbee2MQTT 2.13): header, manufacturer, image type, version, element.
- Image selection, run with zigbee-herdsman's own matching code against the
  0.4.1 index: a device on 0.4.0 gets the 10.8 KB patch, one on 0.3.1 the
  full image, one on 0.4.1 nothing.
- Patch sizes: 0.4.0 → 0.4.1 (version change only) **10.8 KB, 1.6 %** of
  the 678 KB image; 0.3.1 → 0.4.0 (OTA code and the patch library added)
  40 KB, 5.9 %. Every patch is checked by applying it on the computer.
- The SHA-256 the patch header carries is the one esptool reports as the
  image's validation hash, which is what the device compares.
- The same index and files with zigpy 2.4.1 (ZHA), as above.
- No image in the public Zigbee2MQTT index uses manufacturer code 0x131B,
  so no other product's image can be offered to the dimmer.
- On the board (2026-11-18): v0.3.0 flashed by cable, then **0.3.0 → 0.3.1
  over the air** (full image) from Zigbee2MQTT with the local index: about
  an hour with the default 50-byte blocks, the light working all along; the
  device restarted into 0.3.1 without re-pairing.
- On the board, delta (2026-11-18): 0.4.0 flashed by cable, then **0.4.0 →
  0.4.1 as a 10.8 KB patch** from Zigbee2MQTT. Zigbee2MQTT picked the patch
  from the index by itself; the download took about a minute (from 55 % to
  100 % in 8 s) instead of an hour, the device restarted into 0.4.1 without
  re-pairing, and Zigbee2MQTT shows "App: 0.4 build 1 | Stack: 1.6 build 8".
