#!/usr/bin/env bash
# Assembles the files of a GitHub release in dist/release/ from firmware/build
# (run `idf.py build` first; tools/fetch_bases.sh first for delta patches),
# and its notes in dist/release-notes.md from CHANGELOG.md.
#
#   DOMCOM-Dimmer-<ver>.ota, DOMCOM-Dimmer-<base>-to-<ver>.ota
#   index.json                                 Zigbee2MQTT / zigpy index, absolute URLs
#   domcom-dimmer-ota-<ver>.zip                the same as a folder for the
#                                              Zigbee2MQTT data directory (relative paths)
#   domcom-dimmer-esp32c6-<ver>.zip            build folder for flashing by cable
#   domcom-dimmer-esp32c6-<ver>-factory.bin    merged image at 0x0 (erases settings and pairing)
#   domcom-dimmer-app-<ver>.bin / .json        app binary and metadata: the base for
#                                              the next releases' patches
#
# Usage: tools/release_assets.sh <tag> [owner/repo]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TAG=$1
REPO=${2:-charliejgallo/DOMCOM-Dimmer}
VER=$(sed -n 's/.*PROJECT_VER "\(.*\)".*/\1/p' "$ROOT/firmware/CMakeLists.txt")
PY=python3
[ -x "$ROOT/.venv/bin/python" ] && PY=$ROOT/.venv/bin/python

base=${TAG#v}
base=${base%%-*}
if [ "$base" != "$VER" ]; then
    echo "tag $TAG does not match PROJECT_VER $VER" >&2
    exit 1
fi

# Index with absolute URLs to this release's files, then a second one with
# paths relative to the Zigbee2MQTT data folder, for copying by hand.
"$ROOT/tools/package.sh" --base-url "https://github.com/$REPO/releases/download/$TAG"
"$PY" "$ROOT/tools/make_ota.py" --out "$ROOT/dist/ota-local/domcom-dimmer" >/dev/null

R=$ROOT/dist/release
mkdir -p "$R"
cp "$ROOT"/dist/ota/domcom-dimmer/*.ota "$ROOT/dist/ota/domcom-dimmer/index.json" "$R/"
(cd "$ROOT/dist/ota-local" && zip -qr "$R/domcom-dimmer-ota-$VER.zip" domcom-dimmer)
(cd "$ROOT/dist" && zip -qr "$R/domcom-dimmer-esp32c6-$VER.zip" domcom-dimmer-esp32c6)
cp "$ROOT/dist/domcom-dimmer-esp32c6-v$VER@0x0.bin" "$R/domcom-dimmer-esp32c6-$VER-factory.bin"
cp "$ROOT/archive/$VER/domcom_dimmer.bin" "$R/domcom-dimmer-app-$VER.bin"
cp "$ROOT/archive/$VER/meta.json" "$R/domcom-dimmer-app-$VER.json"

# Notes: this version's CHANGELOG section, plus how to install.
{
    awk -v v="$VER" '
        $0 ~ "^## " v "( |$)" { on = 1; next }
        on && /^## / { exit }
        on { print }
    ' "$ROOT/CHANGELOG.md"
    cat <<EOF

### Files

- **Update over Zigbee:** point Zigbee2MQTT at
  \`https://github.com/$REPO/releases/latest/download/index.json\`
  (\`ota: zigbee_ota_override_index_location\`), or unzip
  \`domcom-dimmer-ota-$VER.zip\` into its data folder. Devices on a recent
  release get a small delta patch; others, the full image. See docs/OTA.md.
- **Flash by cable:** \`domcom-dimmer-esp32c6-$VER.zip\` (ESP-IDF build folder,
  keeps the settings and the pairing), or \`domcom-dimmer-esp32c6-$VER-factory.bin\`
  at 0x0 on a blank chip (erases everything).
- \`domcom-dimmer-app-$VER.*\`: the base for later patches; not needed to install.
EOF
} > "$ROOT/dist/release-notes.md"

ls -l "$R" | awk 'NF>5{print $5, $9}'
