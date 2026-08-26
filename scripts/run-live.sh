#!/usr/bin/env bash
# Run Okapia under QEMU with a visible window, so the emulated Mac's screen can
# be watched directly. Serial still goes to a log.
#   usage: run-live.sh [seconds]   (no argument: runs until closed)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"

KERNEL="${REPO_ROOT}/src/kernel/kernel8.img"
SD="${REPO_ROOT}/qemu/sd.img"
LOG="${REPO_ROOT}/qemu/serial.log"

[ -f "$KERNEL" ] || { echo "Build first: gmake -C src/kernel" >&2; exit 1; }

printf 'Serial log: %s\n' "$LOG"
# 2560x1920 is exactly four times the Mac's 640x480, so the compositor scales by
# an integer factor and every guest pixel becomes a clean 4x4 block — no
# interpolation anywhere. On a Retina screen macOS then draws the window at half
# size in points, giving a 1280x960 window: large, and pixel-exact.
#
# If the picture still shimmers, QEMU's own View menu has a Zoom Interpolation
# entry; turning it off keeps edges hard. Zoom To Fit allows free resizing at
# the cost of a fractional scale.
#
# OUTPUT_W/H can be overridden to match a different display.
qemu-system-aarch64 -M raspi3b -kernel "$KERNEL" \
    -drive "file=${SD},if=sd,format=raw" \
    -device usb-kbd -device usb-mouse \
    -global "bcm2835-fb.xres=${OUTPUT_W:-2560}" -global "bcm2835-fb.yres=${OUTPUT_H:-1920}" \
    -display cocoa,zoom-to-fit=on \
    -serial "file:${LOG}" &
QPID=$!

if [ $# -ge 1 ]; then
    sleep "$1"
    kill $QPID 2>/dev/null || true
else
    wait $QPID
fi
