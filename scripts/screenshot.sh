#!/usr/bin/env bash
# Boot headless on a throwaway card and capture what is actually on the Mac's screen.
#   usage: screenshot.sh [seconds] [width height]
#
# Opcode rates and "guest buffer has content" are true of the question-mark
# floppy too, so they cannot tell a booted Finder from a stalled Mac. Only the
# screen can. Pass a width and height to reproduce run-live.sh's scaled output.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SECONDS_TO_RUN="${1:-50}"
KERNEL="${REPO_ROOT}/src/kernel/kernel8.img"
SD="${REPO_ROOT}/qemu/sd.img"

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"

[ -f "$KERNEL" ] || { echo "No kernel — build it first." >&2; exit 1; }
[ -f "$SD" ]     || { echo "No SD card — run make-sd-image.sh." >&2; exit 1; }

WORK="$(mktemp -d -t okapiashot)"
trap 'rm -rf "$WORK"' EXIT
cp "$SD" "${WORK}/card.img"

GEOMETRY=()
if [ $# -ge 3 ]; then
    GEOMETRY=(-global "bcm2835-fb.xres=$2" -global "bcm2835-fb.yres=$3")
fi

qemu-system-aarch64 -M raspi3b -kernel "$KERNEL" -serial "file:${WORK}/serial.log" \
    -display none -drive "file=${WORK}/card.img,if=sd,format=raw,cache=writethrough" \
    -device usb-kbd -device usb-mouse -semihosting \
    -monitor "unix:${WORK}/monitor.sock,server,nowait" "${GEOMETRY[@]}" &
QPID=$!

sleep "$SECONDS_TO_RUN"
echo "screendump ${WORK}/screen.ppm" | nc -U "${WORK}/monitor.sock" >/dev/null 2>&1 || true
sleep 3
kill -9 $QPID 2>/dev/null || true
wait $QPID 2>/dev/null || true

OUT="$(mktemp -t okapiascreen)".png
sips -s format png "${WORK}/screen.ppm" --out "$OUT" >/dev/null 2>&1 \
    || { echo "no screen captured" >&2; exit 1; }

grep -E "okapia-video:|okapia-68k: running" "${WORK}/serial.log" | tr -d '\r' | tail -2 || true
printf 'screen: %s\n' "$OUT"
