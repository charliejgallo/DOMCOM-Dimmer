#!/usr/bin/env python3
"""Wrap the ESP-IDF app binary in a Zigbee OTA file and write a Zigbee2MQTT index.

Output (default dist/ota/domcom-dimmer/):
    DOMCOM-Dimmer-<version>.ota   Zigbee OTA upgrade file (ZCL 11.4)
    index.json                    Zigbee2MQTT override index for that file

The manufacturer code and image type come from the build's sdkconfig and the
version from firmware/CMakeLists.txt (PROJECT_VER a.b.c -> 0x00AABBCC), the
same values the firmware's OTA client reports.

Usage: tools/make_ota.py [--out DIR]
"""

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FW = ROOT / "firmware"
MODEL = "DOMCOM-Dimmer"

OTA_FILE_ID = 0x0BEEF11E
OTA_HEADER_VERSION = 0x0100
OTA_HEADER_LEN = 56
ZIGBEE_STACK_PRO = 0x0002
TAG_UPGRADE_IMAGE = 0x0000


def project_version() -> str:
    m = re.search(r'PROJECT_VER\s+"([^"]+)"', (FW / "CMakeLists.txt").read_text())
    if not m:
        raise SystemExit("PROJECT_VER not found in firmware/CMakeLists.txt")
    return m.group(1)


def file_version(ver: str) -> int:
    parts = [int(x) for x in re.match(r"(\d+)\.(\d+)\.(\d+)", ver).groups()]
    return (parts[0] & 0xFF) << 16 | (parts[1] & 0xFF) << 8 | (parts[2] & 0xFF)


def sdkconfig_hex(name: str) -> int:
    m = re.search(rf"^CONFIG_{name}=(\S+)$", (FW / "sdkconfig").read_text(), re.M)
    if not m:
        raise SystemExit(f"CONFIG_{name} not in firmware/sdkconfig (build first)")
    return int(m.group(1), 16)


def build_ota(app: bytes, manuf: int, image_type: int, fver: int, text: str) -> bytes:
    element = struct.pack("<HI", TAG_UPGRADE_IMAGE, len(app)) + app
    total = OTA_HEADER_LEN + len(element)
    header = struct.pack(
        "<IHHHHHIH32sI",
        OTA_FILE_ID,
        OTA_HEADER_VERSION,
        OTA_HEADER_LEN,
        0,                      # field control: no optional fields
        manuf,
        image_type,
        fver,
        ZIGBEE_STACK_PRO,
        text.encode("ascii")[:32].ljust(32, b"\0"),
        total,
    )
    assert len(header) == OTA_HEADER_LEN
    return header + element


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=ROOT / "dist" / "ota" / "domcom-dimmer")
    args = ap.parse_args()

    ver = project_version()
    fver = file_version(ver)
    manuf = sdkconfig_hex("DIMMER_OTA_MANUFACTURER_CODE")
    image_type = sdkconfig_hex("DIMMER_OTA_IMAGE_TYPE")
    app = (FW / "build" / "domcom_dimmer.bin").read_bytes()
    text = f"{MODEL} {ver}"

    ota = build_ota(app, manuf, image_type, fver, text)
    args.out.mkdir(parents=True, exist_ok=True)
    name = f"{MODEL}-{ver}.ota"
    (args.out / name).write_bytes(ota)

    # Paths in a Zigbee2MQTT override index are relative to its data folder,
    # where this folder is copied as-is.
    index = [{
        "url": f"{args.out.name}/{name}",
        "fileVersion": fver,
        "fileSize": len(ota),
        "manufacturerCode": manuf,
        "imageType": image_type,
        "modelId": MODEL,
        "otaHeaderString": text,
        "sha512": hashlib.sha512(ota).hexdigest(),
    }]
    (args.out / "index.json").write_text(json.dumps(index, indent=2) + "\n")

    print(f"{args.out / name}: {len(ota)} bytes, version {ver} (0x{fver:08x}), "
          f"manufacturer 0x{manuf:04x}, image type 0x{image_type:04x}")


if __name__ == "__main__":
    main()
