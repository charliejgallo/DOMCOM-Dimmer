#!/usr/bin/env bash
# Downloads the app binaries of the latest published releases into
# archive/<version>/ (domcom_dimmer.bin + meta.json), so that
# tools/make_ota.py can make delta patches from them. Each release carries
# them as domcom-dimmer-app-<version>.bin / .json.
#
# Pre-releases and drafts are skipped, and so is the version being built:
# patches go only from what devices may actually run to something newer.
#
# Usage: tools/fetch_bases.sh [count]     (default: the latest 5 releases)
# Needs the GitHub CLI (gh), authenticated (GH_TOKEN in CI).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
COUNT=${1:-5}
CURRENT=$(sed -n 's/.*PROJECT_VER "\(.*\)".*/\1/p' "$ROOT/firmware/CMakeLists.txt")

gh release list --exclude-pre-releases --exclude-drafts --limit "$COUNT" \
        --json tagName --jq '.[].tagName' |
while read -r tag; do
    ver=${tag#v}
    [ "$ver" = "$CURRENT" ] && continue
    d=$ROOT/archive/$ver
    tmp=$(mktemp -d)
    if gh release download "$tag" -D "$tmp" \
            -p "domcom-dimmer-app-$ver.bin" -p "domcom-dimmer-app-$ver.json" 2>/dev/null &&
       [ -f "$tmp/domcom-dimmer-app-$ver.bin" ] && [ -f "$tmp/domcom-dimmer-app-$ver.json" ]; then
        mkdir -p "$d"
        mv "$tmp/domcom-dimmer-app-$ver.bin" "$d/domcom_dimmer.bin"
        mv "$tmp/domcom-dimmer-app-$ver.json" "$d/meta.json"
        echo "base $ver from $tag"
    else
        echo "$tag has no base binary, skipped"
    fi
    rm -rf "$tmp"
done
