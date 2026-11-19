#!/usr/bin/env python3
"""Build Zigbee OTA files (full image and delta patches) and a Zigbee2MQTT index.

Run after `idf.py build` (tools/package.sh does). Output, default
dist/ota/domcom-dimmer/:

    DOMCOM-Dimmer-<ver>.ota              full image (element tag 0x0000)
    DOMCOM-Dimmer-<base>-to-<ver>.ota    delta patch from <base> (tag 0xF000)
    index.json                           Zigbee2MQTT override index

Every packaged version is kept in archive/<ver>/ (app binary + meta.json):
those are the bases for later patches. A patch rebuilds the new app on the
device from the exact image it runs, so each one is offered only to that
version (minFileVersion = maxFileVersion = base); the full image comes last
in the index and serves every other version.

The manufacturer code and image type come from the build's sdkconfig, the
version from firmware/CMakeLists.txt, encoded like zb_ota_running_version()
in firmware/main/zb_ota.c.

Delta patches need detools (pip install -r tools/requirements.txt).

Usage: tools/make_ota.py [--out DIR] [--base-url URL] [--no-delta] [--replace-archive]
"""

import argparse
import hashlib
import io
import json
import re
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FW = ROOT / "firmware"
ARCHIVE = ROOT / "archive"
MODEL = "DOMCOM-Dimmer"

OTA_FILE_ID = 0x0BEEF11E
OTA_HEADER_VERSION = 0x0100
OTA_HEADER_LEN = 56
ZIGBEE_STACK_PRO = 0x0002
TAG_UPGRADE_IMAGE = 0x0000
TAG_DELTA_PATCH = 0xF000

DELTA_MAGIC = 0xFCCDDE10          # esp_delta_ota patch header
DELTA_HEADER_LEN = 64
DELTA_SINCE = (0, 4, 0)            # first firmware that applies patches
DELTA_MAX_RATIO = 0.7              # skip patches that save little


def parse_ver(ver: str) -> tuple:
    m = re.match(r"(\d+)\.(\d+)\.(\d+)$", ver)
    if not m:
        raise SystemExit(f"version '{ver}' is not a.b.c")
    return tuple(int(x) for x in m.groups())


def project_version() -> str:
    m = re.search(r'PROJECT_VER\s+"([^"]+)"', (FW / "CMakeLists.txt").read_text())
    if not m:
        raise SystemExit("PROJECT_VER not found in firmware/CMakeLists.txt")
    return m.group(1)


def zigbee_lib_version() -> tuple:
    lock = (FW / "dependencies.lock").read_text()
    m = re.search(r"espressif/esp-zigbee-lib:\n(?:\s+.*\n)*?\s+version: (\d+)\.(\d+)\.(\d+)", lock)
    if not m:
        raise SystemExit("esp-zigbee-lib version not found in firmware/dependencies.lock")
    return tuple(int(x) for x in m.groups())


def file_version(ver: tuple, stack: tuple) -> int:
    """ZCL layout, same as zb_ota_running_version(): app release (a.b as
    nibbles), app build (c), stack release, stack build."""
    a, b, c = ver
    if a > 15 or b > 15 or c > 255:
        raise SystemExit("version must be a.b.c with a, b < 16 and c < 256")
    sa, sb, sc = stack
    return ((a << 4 | b) << 24) | (c << 16) | ((sa & 0xF) << 12 | (sb & 0xF) << 8) | (sc & 0xFF)


def sdkconfig_hex(name: str) -> int:
    m = re.search(rf"^CONFIG_{name}=(\S+)$", (FW / "sdkconfig").read_text(), re.M)
    if not m:
        raise SystemExit(f"CONFIG_{name} not in firmware/sdkconfig (build first)")
    return int(m.group(1), 16)


def app_digest(app: bytes) -> bytes:
    """The SHA-256 appended to an ESP-IDF app image, which is what
    esp_partition_get_sha256() returns for the running app."""
    if app[0] != 0xE9 or app[23] != 1:
        raise SystemExit("app binary has no appended SHA-256")
    digest = app[-32:]
    if hashlib.sha256(app[:-32]).digest() != digest:
        raise SystemExit("app binary's appended SHA-256 does not match its contents")
    return digest


def ota_file(element_tag: int, payload: bytes, manuf: int, image_type: int,
             fver: int, text: str) -> bytes:
    element = struct.pack("<HI", element_tag, len(payload)) + payload
    header = struct.pack(
        "<IHHHHHIH32sI",
        OTA_FILE_ID, OTA_HEADER_VERSION, OTA_HEADER_LEN,
        0,                      # field control: no optional fields
        manuf, image_type, fver, ZIGBEE_STACK_PRO,
        text.encode("ascii")[:32].ljust(32, b"\0"),
        OTA_HEADER_LEN + len(element),
    )
    return header + element


def make_patch(base: bytes, new: bytes) -> bytes:
    """esp_delta_ota patch: 64-byte header + detools sequential/heatshrink,
    checked by applying it back here."""
    try:
        import detools
    except ImportError:
        raise SystemExit("detools missing: pip install -r tools/requirements.txt")
    out = io.BytesIO()
    detools.create_patch(io.BytesIO(base), io.BytesIO(new), out, compression="heatshrink")
    body = out.getvalue()

    rebuilt = io.BytesIO()
    detools.apply_patch(io.BytesIO(base), io.BytesIO(body), rebuilt)
    if rebuilt.getvalue() != new:
        raise SystemExit("patch check failed: applying it does not give the new image")

    header = struct.pack("<I", DELTA_MAGIC) + app_digest(base)
    return header.ljust(DELTA_HEADER_LEN, b"\0") + body


def archive_current(ver: str, fver: int, app: bytes, replace: bool) -> None:
    d = ARCHIVE / ver
    meta = {"version": ver, "fileVersion": fver, "sha256": app_digest(app).hex(), "size": len(app)}
    old = d / "meta.json"
    if old.exists():
        prev = json.loads(old.read_text())
        if prev["sha256"] != meta["sha256"] and not replace:
            raise SystemExit(
                f"archive/{ver} holds a different build of {ver}. Devices may run that one, "
                f"and patches made from it would not apply to this one.\n"
                f"Bump PROJECT_VER, or pass --replace-archive if {ver} was never distributed.")
    d.mkdir(parents=True, exist_ok=True)
    (d / "domcom_dimmer.bin").write_bytes(app)
    old.write_text(json.dumps(meta, indent=2) + "\n")


def archived_bases(current: tuple) -> list:
    bases = []
    for meta_path in sorted(ARCHIVE.glob("*/meta.json")):
        meta = json.loads(meta_path.read_text())
        v = parse_ver(meta["version"])
        if DELTA_SINCE <= v < current:
            bases.append((v, meta, (meta_path.parent / "domcom_dimmer.bin").read_bytes()))
    return sorted(bases, reverse=True)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=ROOT / "dist" / "ota" / "domcom-dimmer")
    ap.add_argument("--base-url", help="absolute URL prefix for the files in the index "
                    "(web hosting); default: paths relative to the Zigbee2MQTT data folder")
    ap.add_argument("--no-delta", action="store_true", help="full image only")
    ap.add_argument("--replace-archive", action="store_true",
                    help="overwrite archive/<ver> with this build")
    args = ap.parse_args()

    ver = project_version()
    vt = parse_ver(ver)
    fver = file_version(vt, zigbee_lib_version())
    manuf = sdkconfig_hex("DIMMER_OTA_MANUFACTURER_CODE")
    image_type = sdkconfig_hex("DIMMER_OTA_IMAGE_TYPE")
    app = (FW / "build" / "domcom_dimmer.bin").read_bytes()

    archive_current(ver, fver, app, args.replace_archive)

    if args.out.exists():
        shutil.rmtree(args.out)
    args.out.mkdir(parents=True)

    prefix = args.base_url.rstrip("/") + "/" if args.base_url else f"{args.out.name}/"
    entries = []

    def add(name: str, data: bytes, text: str, extra: dict) -> None:
        (args.out / name).write_bytes(data)
        entries.append({
            "url": prefix + name,
            "fileVersion": fver,
            "fileSize": len(data),
            "manufacturerCode": manuf,
            "imageType": image_type,
            "modelId": MODEL,
            "otaHeaderString": text,
            "sha512": hashlib.sha512(data).hexdigest(),
            **extra,
        })
        print(f"  {name}: {len(data)} bytes")

    full_text = f"{MODEL} {ver}"
    full = ota_file(TAG_UPGRADE_IMAGE, app, manuf, image_type, fver, full_text)
    print(f"{MODEL} {ver}: file version 0x{fver:08x}, manufacturer 0x{manuf:04x}, "
          f"image type 0x{image_type:04x}")

    if not args.no_delta:
        for bv, meta, base in archived_bases(vt):
            patch = make_patch(base, app)
            if len(patch) > DELTA_MAX_RATIO * len(app):
                print(f"  from {meta['version']}: patch {len(patch)} bytes, not worth it, skipped")
                continue
            text = f"{MODEL} {meta['version']}>{ver}"
            data = ota_file(TAG_DELTA_PATCH, patch, manuf, image_type, fver, text)
            add(f"{MODEL}-{meta['version']}-to-{ver}.ota", data, text,
                {"minFileVersion": meta["fileVersion"], "maxFileVersion": meta["fileVersion"]})

    # Last: Zigbee2MQTT takes the first entry that matches the device.
    add(f"{MODEL}-{ver}.ota", full, full_text, {})

    (args.out / "index.json").write_text(json.dumps(entries, indent=2) + "\n")
    print(f"  index.json: {len(entries)} entries")


if __name__ == "__main__":
    sys.exit(main())
