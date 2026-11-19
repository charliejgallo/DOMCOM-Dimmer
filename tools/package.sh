#!/bin/zsh
# Builds dist/ from firmware/build, for esptool or any flasher that takes
# ESP-IDF build folders or address-tagged images:
#   dist/domcom-dimmer-esp32c6/              folder with flasher_args.json
#                                            (update: keeps NVS and the Zigbee network)
#   dist/domcom-dimmer-esp32c6-vX@0x0.bin    single merged image at 0x0
#                                            (factory: its 0xFF padding erases both)
#   dist/ota/domcom-dimmer/                  Zigbee OTA file + Zigbee2MQTT index
set -e
ROOT=${0:A:h:h}
B=$ROOT/firmware/build
VER=$(sed -n 's/.*PROJECT_VER "\(.*\)".*/\1/p' $ROOT/firmware/CMakeLists.txt)
D=$ROOT/dist/domcom-dimmer-esp32c6
rm -rf $ROOT/dist
mkdir -p $D/bootloader $D/partition_table
cp $B/flasher_args.json $B/ota_data_initial.bin $B/domcom_dimmer.bin $D/
cp $B/bootloader/bootloader.bin $D/bootloader/
cp $B/partition_table/partition-table.bin $D/partition_table/
(cd $B && python -m esptool --chip esp32c6 merge_bin -o "$ROOT/dist/domcom-dimmer-esp32c6-v$VER@0x0.bin" @flash_args >/dev/null)
python3 $ROOT/tools/make_ota.py
ls -l $ROOT/dist $D | awk 'NF>5{print $5, $9}'
