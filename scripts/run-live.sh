#!/usr/bin/env bash
# Run Okapia under QEMU with a visible window, so the emulated Mac's screen can
# be watched directly. Serial still goes to a log.
#   usage: run-live.sh [seconds]   (no argument: runs until closed)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/target.sh"
# QEMU's raspi3b puts the peripherals at 0x3F000000 and a Pi 4 kernel addresses
# 0xFE000000, so the wrong build does not fail — it goes quiet. Say so instead.
okapia_require_target "$REPO_ROOT" qemu "$(basename "$0")" || exit 1

KERNEL="${REPO_ROOT}/src/kernel/kernel8.img"
SD="${REPO_ROOT}/qemu/sd.img"
LOG="${REPO_ROOT}/qemu/serial.log"
# A control socket on the live session, so its screen can be captured without
# disturbing it: echo "screendump /tmp/x.ppm" | nc -U "$MONITOR"
MONITOR="${OKAPIA_MONITOR:-/tmp/okapia-monitor.sock}"
rm -f "$MONITOR"

[ -f "$KERNEL" ] || { echo "Build first: gmake -C src/kernel" >&2; exit 1; }
[ -f "$SD" ] || { echo "No card: run scripts/make-sd-image.sh" >&2; exit 1; }

# One image carries both Macintoshes, so there is nothing left to check here:
# whichever engine the card's startup volume asks for is entered by a function
# call inside the running kernel (src/kernel/okapia_boot.cpp).

printf 'Serial log: %s\n' "$LOG"
# Window size is the frame buffer size: QEMU's cocoa backend sizes its window in
# points, one per guest pixel. So 1280x960 — exactly twice the Mac's 640x480 —
# gives both a comfortable window and an integer scale factor of 2, where every
# guest pixel becomes a clean 2x2 block with no interpolation.
#
# Do not add zoom-to-fit: it opens a tiny window and defers to manual resizing,
# which reintroduces a fractional scale and makes the picture shimmer.
#
# Two emulators writing the same card is a guaranteed way to destroy the volume,
# and it looks exactly like random corruption after the fact. This is the only
# script that runs on the master card, so it is the one that has to refuse.
if command -v lsof >/dev/null && lsof -t -- "$SD" >/dev/null 2>&1; then
    echo "Refusing to start: ${SD} is already open by another process." >&2
    echo "Close the running emulator first — two of them will corrupt the volume." >&2
    lsof -- "$SD" >&2 || true
    exit 1
fi

# OUTPUT_W/OUTPUT_H override this; keep them multiples of 640x480.
qemu-system-aarch64 -M raspi3b -kernel "$KERNEL" \
    -drive "file=${SD},if=sd,format=raw,cache=writethrough" \
    -device usb-kbd -device usb-mouse \
    -semihosting \
    -global "bcm2835-fb.xres=${OUTPUT_W:-1280}" -global "bcm2835-fb.yres=${OUTPUT_H:-960}" \
    -display cocoa \
    -monitor "unix:${MONITOR},server,nowait" \
    -serial "file:${LOG}" &
QPID=$!

if [ $# -ge 1 ]; then
    sleep "$1"
    kill $QPID 2>/dev/null || true
else
    wait $QPID
fi
