#!/usr/bin/env bash
# Can the kernel read a disk image past 2 GiB?
#   usage: check-large-volume.sh [seconds]
#
# circle-newlib's _lseek took its offset as an int, so a volume of 2 GB or more
# read cleanly up to 2 GiB and returned nothing beyond (patches/circle-stdlib/0002).
# This builds a 3 GB HFS volume whose blessed System file lies past that line
# (tests/host/make_large_volume), puts it alone on a throwaway card, boots the
# kernel under QEMU and waits for the one line that needs those bytes: the
# System version read from that file before the Macintosh starts. It never
# touches qemu/sd.img.
#
# Needs about 7 GB free while it runs; everything is deleted afterwards.
#
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SECONDS_TO_WAIT="${1:-90}"
KERNEL="${REPO_ROOT}/src/kernel/kernel8.img"
SOURCE="${REPO_ROOT}/qemu/sd-contents/boot71.img"

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"
# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/target.sh"
okapia_require_target "$REPO_ROOT" qemu "$(basename "$0")" || exit 1

[ -f "$KERNEL" ] || { echo "No kernel at $KERNEL — build it first." >&2; exit 1; }
[ -f "$SOURCE" ] || { echo "No $SOURCE to take a System file from." >&2; exit 1; }
command -v mformat >/dev/null || { echo "mtools missing: brew install mtools" >&2; exit 1; }

WORK="$(mktemp -d -t okapia-large)"
QPID=""
cleanup() {
    [ -n "$QPID" ] && kill -9 "$QPID" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

make -s -C "${REPO_ROOT}/tests/host" make_large_volume
"${REPO_ROOT}/tests/host/make_large_volume" "${WORK}/large.img" 3072 "$SOURCE"

# FAT32 holds a file up to 4 GiB - 1, so a 4 GB card holds the 3 GB volume.
# The image takes the name a card without preferences falls back to.
CARD="${WORK}/card.img"
dd if=/dev/zero of="$CARD" bs=1m count=0 seek=4096 status=none
mformat -i "$CARD" -F -c 64 -v OKAPIA ::
mcopy -i "$CARD" "${WORK}/large.img" ::machd76.image
mcopy -i "$CARD" "${REPO_ROOT}/qemu/sd-contents/okapia.rom" ::okapia.rom
rm -f "${WORK}/large.img"

LOG="${WORK}/serial.log"
qemu-system-aarch64 -M raspi3b -kernel "$KERNEL" -serial stdio -display none \
    -drive "file=${CARD},if=sd,format=raw,cache=writethrough" \
    -device usb-kbd -device usb-mouse -semihosting > "$LOG" 2>&1 &
QPID=$!

VERDICT=""
for _ in $(seq "$SECONDS_TO_WAIT"); do
    sleep 1
    if grep -q 'machd76.image: "System" says System' "$LOG"; then
        VERDICT=ok
        break
    fi
    if grep -q 'machd76.image: no System version found\|No HFS volume found' "$LOG"; then
        VERDICT=unreadable
        break
    fi
done

kill -9 "$QPID" 2>/dev/null || true
wait "$QPID" 2>/dev/null || true
QPID=""

tr -d '\r' < "$LOG" | grep -E 'machd76|Boot volume|HFS volume' || true
case "$VERDICT" in
    ok)
        echo "OK: the System file past 2 GiB was read"
        ;;
    unreadable)
        echo "FAILED: the volume could not be read past 2 GiB" >&2
        exit 1
        ;;
    *)
        echo "INCONCLUSIVE: no System version line in ${SECONDS_TO_WAIT} s" >&2
        exit 1
        ;;
esac
