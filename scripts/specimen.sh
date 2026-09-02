#!/usr/bin/env bash
# Render the firmware's theme specimen under QEMU and capture the screen.
#
# No SD card is involved and no emulator is started: this kernel links Circle
# and the firmware's drawing code and nothing else. It is therefore always safe
# to run alongside anything, and it can never touch a card.
#
# The specimen answers now: the kernel runs the firmware's event loop, so the
# page is asked for with an arrow key through the monitor rather than waited
# for. No argument takes the first page, "2" the second.
#
#   usage: specimen.sh [2] [width height]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"

KERNEL="${REPO_ROOT}/src/firmware/circle/kernel8.img"
OUT="${REPO_ROOT}/build/specimen.ppm"

"$MAKE" -C "${REPO_ROOT}/src/firmware/circle" >/dev/null
[ -f "$KERNEL" ] || { echo "No specimen kernel." >&2; exit 1; }

WORK="$(mktemp -d -t okapiaspec)"
trap 'rm -rf "$WORK"' EXIT

PAGE=1
if [ "${1:-}" = "2" ]; then PAGE=2; shift; fi

GEOMETRY=()
if [ $# -ge 2 ]; then
    GEOMETRY=(-global "bcm2835-fb.xres=$1" -global "bcm2835-fb.yres=$2")
fi

# A keyboard and a mouse, because the specimen is an interface and not a
# picture: without them the focus never moves and nothing here is exercised.
qemu-system-aarch64 -M raspi3b -kernel "$KERNEL" -serial "file:${WORK}/serial.log" \
    -display none -semihosting -device usb-kbd -device usb-mouse \
    -monitor "unix:${WORK}/monitor.sock,server,nowait" "${GEOMETRY[@]}" &
QPID=$!

# One command per connection, with a pause: a burst down a single socket is
# dropped, which cost this project an afternoon once already (AGENTS.md).
say () { echo "$1" | nc -U "${WORK}/monitor.sock" >/dev/null 2>&1 || true; }

# Long enough for the first paint, which costs the better part of a second under
# QEMU at 640x480 and rather more above it.
sleep 5
if [ "$PAGE" = "2" ]; then
    say "sendkey right"
    sleep 4
fi
say "screendump ${WORK}/screen.ppm"
sleep 2
kill -9 $QPID 2>/dev/null || true
wait $QPID 2>/dev/null || true

grep -E 'specimen:' "${WORK}/serial.log" || true

mkdir -p "$(dirname "$OUT")"
if [ ! -f "${WORK}/screen.ppm" ]; then
    echo "No screen dump — see the serial log above." >&2
    exit 1
fi
cp "${WORK}/screen.ppm" "$OUT"
echo "Screen captured: $OUT"

# The same drawing code runs on a development machine (tests/host). When both
# were asked for the same size, they must agree pixel for pixel — that is what
# makes the host renderer a stand-in worth iterating on, and the check costs a
# comparison. A difference means the two builds disagree, which is worth
# knowing long before a design decision rests on the wrong one.
HOST="${REPO_ROOT}/tests/host/specimen.ppm"
[ "$PAGE" = "2" ] && HOST="${REPO_ROOT}/tests/host/specimen-2.ppm"
if [ $# -lt 2 ] && [ -f "$HOST" ]; then
    if cmp -s "$OUT" "$HOST"; then
        echo "Identical to the host render."
    else
        echo "Differs from the host render ($HOST) — one of the two is stale or wrong." >&2
    fi
fi
