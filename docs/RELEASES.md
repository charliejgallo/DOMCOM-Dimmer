# Releases

Releases are built and published by GitHub Actions
([`.github/workflows/release.yml`](../.github/workflows/release.yml)) when a
version tag is pushed. Nothing has to be built on a computer for a release.

## Making a release

1. Set `PROJECT_VER` in `firmware/CMakeLists.txt` to the new version
   (`a.b.c`, with `a` and `b` below 16: see [OTA.md](OTA.md#how-it-works)).
2. Add a `## a.b.c — date` section to `CHANGELOG.md`: it becomes the
   release notes.
3. Commit, then tag and push:

   ```bash
   git tag v0.5.0
   git push origin main v0.5.0
   ```

The workflow checks that the tag matches `PROJECT_VER`, builds the firmware
in the `espressif/idf:v5.5.5` container, downloads the app binaries of the
latest five releases as patch bases (`tools/fetch_bases.sh`), runs
`tools/release_assets.sh` and publishes the release.

A tag with a suffix (`v0.5.0-rc1`) publishes a **pre-release**: useful for
trying a build on a device first. Pre-releases are never patch bases and
`releases/latest` skips them.

**Dry run:** *Actions → Release → Run workflow* builds everything from the
current branch and leaves it as a workflow artifact (`release-dry-run`),
without publishing. Checked on 2026-11-19: build ~4.5 min in the container,
release files ~25 s, all seven files and the notes produced.

## What a release contains

| File | For |
|---|---|
| `index.json` | Zigbee2MQTT / zigpy OTA index, absolute URLs to this release's files |
| `DOMCOM-Dimmer-<ver>.ota` | full image, for any earlier version |
| `DOMCOM-Dimmer-<base>-to-<ver>.ota` | delta patch for each of the last releases |
| `domcom-dimmer-ota-<ver>.zip` | the OTA folder with relative paths, to unzip into the Zigbee2MQTT data folder |
| `domcom-dimmer-esp32c6-<ver>.zip` | ESP-IDF build folder for flashing by cable (keeps settings and pairing) |
| `domcom-dimmer-esp32c6-<ver>-factory.bin` | merged image at 0x0 for a blank chip (erases everything) |
| `domcom-dimmer-app-<ver>.bin` / `.json` | the app binary and its metadata: the base for later patches |

## Updates from the latest release

With the repository public, Zigbee2MQTT can follow the releases directly:

```yaml
ota:
  zigbee_ota_override_index_location: https://github.com/charliejgallo/DOMCOM-Dimmer/releases/latest/download/index.json
```

`releases/latest/download/` always points to the newest non-pre-release,
so every new release shows up as an update with no files to copy. Each
release's index lists its patches first, each limited to its base version,
and the full image last.

ZHA can use the same URL with zigpy's remote Zigbee2MQTT provider
(`type: z2m`, `url: …`); not tried yet.

## Release binaries and local builds

A patch only applies to the exact binary it was made from. Releases are
built in CI, so their binaries differ from a local build of the same
version (build date, toolchain host). A device flashed with a **local
build** of version X is offered the patch for X, checks it against its own
image and **refuses it** before writing anything; it then needs the full
image (by cable or from a local index).

So devices in use should run release binaries: flash
`domcom-dimmer-esp32c6-<ver>.zip` from a release, not a local build, when
installing one.

## Checked

- v0.5.0 (2026-11-19), the first release published by the workflow: built
  in CI, it found v0.4.1 as base and made a 31 KB patch against it; the
  board, running that 0.4.1, updated to 0.5.0 with it through Zigbee2MQTT
  (the release's OTA folder copied in, since the repository was still
  private).
- Still to check once public: Zigbee2MQTT following
  `releases/latest/download/index.json` by itself.

## Base releases built before the workflow

v0.4.1 was built on a computer before this workflow existed. Its release
carries that build as the base, because it is what the first devices run:
patches from it to the first CI release work for them.
