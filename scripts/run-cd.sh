#!/usr/bin/env bash
# Boot the Mac OS 8.6 install CD, in a window, to install onto a volume.
#   usage: run-cd.sh
#
# The card is qemu/sd-cd.img and nothing else uses it, so a session here cannot
# touch the card run-live.sh boots. It carries the install disc, the PowerMac
# ROM and no volume at all: the target is made from Okapia's own boot menu.
#
# The disc is the startup volume, and it is not the order of the lines that says
# so — `bootdriver` carries CDROMRefNum, -62, which main.cpp:139 writes into the
# parameter RAM at 0x7a, where a Macintosh keeps its startup device. That is the
# same setting the C key stood for.
#
# End the session with Special -> Shut Down, as AGENTS.md asks. Anything else is
# a pulled plug on a Macintosh that is writing to the volume it is installing.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL="${REPO_ROOT}/src/kernel/kernel8.img"
CARD="${REPO_ROOT}/qemu/sd-cd.img"
MONITOR=/tmp/okapia-cd.sock

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"

[ -f "$KERNEL" ] || { echo "No kernel — make -C src/kernel first." >&2; exit 1; }
[ -f "$CARD" ]   || { echo "No ${CARD}." >&2; exit 1; }

# Never two emulators on one card: that is how a volume is destroyed, and
# afterwards it reads as random corruption.
if command -v lsof >/dev/null && lsof -- "$CARD" >/dev/null 2>&1; then
    echo "Something already has ${CARD} open. Refusing to start a second one." >&2
    exit 1
fi

rm -f "$MONITOR"
echo "Monitor: echo \"screendump /tmp/x.ppm\" | nc -U ${MONITOR}"

# cache=writethrough: QEMU's drive defaults to writeback, which parks guest
# writes in the host page cache and invents a durability hole hardware has not.
exec qemu-system-aarch64 -M raspi3b -kernel "$KERNEL" \
    -serial "file:${REPO_ROOT}/qemu/serial-cd.log" \
    -drive "file=${CARD},if=sd,format=raw,cache=writethrough" \
    -device usb-kbd -device usb-mouse -semihosting \
    -monitor "unix:${MONITOR},server,nowait" \
    -display cocoa
