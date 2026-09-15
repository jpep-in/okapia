#!/usr/bin/env bash
# Render the firmware's theme specimen under QEMU and capture the screen.
#
# No SD card is involved and no emulator is started: this kernel links Circle
# and the firmware's drawing code and nothing else. It is therefore always safe
# to run alongside anything, and it can never touch a card.
#
# The specimen answers now: the kernel runs the firmware's event loop, so the
# page is asked for with an arrow key through the monitor rather than waited
# for. No argument takes the first page; a digit takes that one.
#
#   usage: specimen.sh [page] [width height]   capture, headless
#          specimen.sh live [width height]     a window, driven by hand
#
# The window opens at 1280x960 — twice the design's 640x480, so the theme lands
# on a whole scale of 2. OUTPUT_W/OUTPUT_H override it; keep them multiples of
# 640x480 or the interface is drawn at a fractional scale.
#
# "live" is the one to reach for when the question is how the interface feels
# rather than what it measures: Tab and Maj-Tab walk the focus, Espace operates
# what holds it, Retour the default button, the arrows move the list, and
# Gauche/Droite turn the page. The mouse works, and the pointer appears the
# moment it moves. No card is touched, so it is always safe to leave running.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/target.sh"
# QEMU's raspi3b puts the peripherals at 0x3F000000 and a Pi 4 kernel addresses
# 0xFE000000, so the wrong build does not fail — it goes quiet. Say so instead.
okapia_require_target "$REPO_ROOT" qemu "$(basename "$0")" || exit 1

KERNEL="${REPO_ROOT}/src/firmware/circle/kernel8.img"
OUT="${REPO_ROOT}/build/specimen.ppm"

"$MAKE" -C "${REPO_ROOT}/src/firmware/circle" >/dev/null
[ -f "$KERNEL" ] || { echo "No specimen kernel." >&2; exit 1; }

WORK="$(mktemp -d -t okapiaspec)"
trap 'rm -rf "$WORK"' EXIT

PAGE=1
LIVE=0
case "${1:-}" in
    [1-9]) PAGE="$1"; shift ;;
    live)  LIVE=1; shift ;;
esac

GEOMETRY=()
if [ $# -ge 2 ]; then
    GEOMETRY=(-global "bcm2835-fb.xres=$1" -global "bcm2835-fb.yres=$2")
fi

# Driven by hand: a window, the serial on the terminal, and no clock deciding
# anything. It ends when the window is closed.
if [ "$LIVE" = "1" ]; then
    # The frame buffer is the window: QEMU's cocoa backend sizes its window in
    # points, one per guest pixel, and does not resize it afterwards. Left at
    # the 640x480 the headless capture uses, that is a postage stamp on a modern
    # screen — and the window opens before the guest has said anything, so what
    # is on it is whatever fits. run-live.sh settled this for the emulator long
    # ago and the reasoning is the same here: 1280x960 is a comfortable window
    # AND exactly twice 640x480, so the theme lands on a whole scale factor of 2
    # instead of a fractional one that makes the curves shimmer.
    #
    # Do not reach for zoom-to-fit instead: it opens small and defers to manual
    # resizing, which brings the fractional scale straight back.
    if [ ${#GEOMETRY[@]} -eq 0 ]; then
        GEOMETRY=(-global "bcm2835-fb.xres=${OUTPUT_W:-1280}" \
                  -global "bcm2835-fb.yres=${OUTPUT_H:-960}")
    fi
    echo "Tab / Maj-Tab : le focus · Espace : actionner · Retour : bouton par défaut"
    echo "Flèches : la liste · Gauche/Droite : la page · L : la langue · souris : cliquez"
    exec qemu-system-aarch64 -M raspi3b -kernel "$KERNEL" -serial stdio \
        -display cocoa -semihosting -device usb-kbd -device usb-mouse \
        "${GEOMETRY[@]}"
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
# One arrow per page past the first. The kernel turns them itself, so the
# capture lands where it was asked to rather than where a clock left it.
n=1
while [ "$n" -lt "$PAGE" ]; do
    say "sendkey right"
    sleep 3
    n=$((n + 1))
done
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
[ "$PAGE" = "1" ] || HOST="${REPO_ROOT}/tests/host/specimen-${PAGE}.ppm"
if [ $# -lt 2 ] && [ -f "$HOST" ]; then
    if cmp -s "$OUT" "$HOST"; then
        echo "Identical to the host render."
    else
        echo "Differs from the host render ($HOST) — one of the two is stale or wrong." >&2
    fi
fi
