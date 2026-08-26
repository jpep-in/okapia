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
# Window size is the frame buffer size: QEMU's cocoa backend sizes its window in
# points, one per guest pixel. So 1280x960 — exactly twice the Mac's 640x480 —
# gives both a comfortable window and an integer scale factor of 2, where every
# guest pixel becomes a clean 2x2 block with no interpolation.
#
# Do not add zoom-to-fit: it opens a tiny window and defers to manual resizing,
# which reintroduces a fractional scale and makes the picture shimmer.
#
# OUTPUT_W/OUTPUT_H override this; keep them multiples of 640x480.
qemu-system-aarch64 -M raspi3b -kernel "$KERNEL" \
    -drive "file=${SD},if=sd,format=raw" \
    -device usb-kbd -device usb-mouse \
    -global "bcm2835-fb.xres=${OUTPUT_W:-1280}" -global "bcm2835-fb.yres=${OUTPUT_H:-960}" \
    -display cocoa \
    -serial "file:${LOG}" &
QPID=$!

if [ $# -ge 1 ]; then
    sleep "$1"
    kill $QPID 2>/dev/null || true
else
    wait $QPID
fi
