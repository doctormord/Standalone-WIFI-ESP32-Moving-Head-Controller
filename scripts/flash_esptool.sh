#!/usr/bin/env bash
# Manual esptool flash for when `pio run -t upload` can't auto-reset the board into
# bootloader mode (common on ESP32-C3 Supermini native-USB boards — see
# doc/content/history.md 2026-08-20 for the failure mode this works around).
#
# IMPORTANT: writes bootloader/partitions/boot_app0/firmware at their own individual
# offsets, NOT the merged .pio/build/*/firmware.factory.bin. That merged image is one
# contiguous blob from 0x0 through past 0x10000 and silently overwrites everything in
# between at the byte level -- including the nvs partition at 0x9000 (WiFi credentials,
# fixture patch, master brightness, all preset/chaser slots) and otadata at 0xe000. That
# exact mistake wiped a device's saved config once already (2026-08-20, see history.md/
# backlog.md) -- writing each component at its real, non-contiguous offset leaves the
# 0x8c00-0xe000 gap (which is the nvs partition) untouched, same as `pio run -t upload`
# does when it works normally.
#
# Usage:
#   1. Manually put the board in bootloader mode: unplug USB, hold BOOT, plug USB back
#      in while still holding BOOT, wait ~2s, release BOOT.
#   2. ./scripts/flash_esptool.sh [port] [--fs]
#      port defaults to /dev/cu.usbmodem1101 if omitted.
#      --fs also flashes the LittleFS data image (data/index.html + vendor/*) at its
#      partition offset -- omit this if you only rebuilt firmware, to save time and
#      avoid needlessly re-erasing/rewriting the filesystem partition.
#
# Requires a prior `pio run` (and `pio run -t buildfs` if using --fs) so the build
# artifacts under .pio/build/supermini/ exist.
#
# PARTITION TABLE: this project uses partitions_horizon.csv (app slots 1.5MB, filesystem
# 896KB), not the stock default.csv. Moving a device that still has the old table onto it
# REQUIRES this script (or `pio run -t upload`) WITH --fs: the table is written at 0x8000 and
# the filesystem offset moves from 0x290000 to 0x310000, so the old filesystem content is
# stranded where nothing looks for it and must be re-flashed. OTA cannot do any of this -- it
# only ever writes an app slot. nvs is untouched either way, so the saved configuration
# survives.

set -euo pipefail

PORT="${1:-/dev/cu.usbmodem1101}"
FLASH_FS=0
for arg in "$@"; do
  [ "$arg" = "--fs" ] && FLASH_FS=1
done

BUILD_DIR=".pio/build/supermini"
ESPTOOL="$HOME/.platformio/penv/bin/python -m esptool"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

for f in "$BUILD_DIR/bootloader.bin" "$BUILD_DIR/partitions.bin" "$BUILD_DIR/firmware.bin"; do
  [ -f "$f" ] || { echo "Missing $f -- run 'pio run' first." >&2; exit 1; }
done
[ -f "$BOOT_APP0" ] || { echo "Missing $BOOT_APP0 (unexpected framework layout)." >&2; exit 1; }

echo "Flashing firmware components at their individual partition offsets (nvs untouched)..."
$ESPTOOL --chip esp32c3 --port "$PORT" --baud 115200 --before no-reset --after hard-reset \
  write-flash \
  0x0     "$BUILD_DIR/bootloader.bin" \
  0x8000  "$BUILD_DIR/partitions.bin" \
  0xe000  "$BOOT_APP0" \
  0x10000 "$BUILD_DIR/firmware.bin"

if [ "$FLASH_FS" = "1" ]; then
  [ -f "$BUILD_DIR/littlefs.bin" ] || { echo "Missing $BUILD_DIR/littlefs.bin -- run 'pio run -t buildfs' first." >&2; exit 1; }
  # Offset is DECODED from the partition table that was just built, never hardcoded: it moved
  # once already when the table changed (default.csv 0x290000 -> partitions_horizon.csv
  # 0x310000), and writing the filesystem at a stale offset lands it on top of an app slot.
  GEN="$HOME/.platformio/packages/framework-arduinoespressif32/tools/gen_esp32part.py"
  FS_OFF=$(python3 "$GEN" "$BUILD_DIR/partitions.bin" 2>/dev/null \
           | awk -F, '$1=="spiffs"{print $4}')
  [ -n "$FS_OFF" ] || { echo "Could not read the spiffs offset from partitions.bin." >&2; exit 1; }
  echo "Flashing LittleFS image at $FS_OFF (decoded from partitions.bin)..."
  $ESPTOOL --chip esp32c3 --port "$PORT" --baud 115200 --before no-reset --after hard-reset \
    write-flash "$FS_OFF" "$BUILD_DIR/littlefs.bin"
fi

echo "Done."
