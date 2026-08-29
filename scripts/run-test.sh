#!/usr/bin/env bash
# Headless regression run, then a verdict on what the run did to the volume.
#   usage: run-test.sh [seconds] [kernel]
#
# The run works on a copy of qemu/sd.img — not to look away from corruption, but
# to be able to see it. On the master image damage accumulates across runs and
# nothing is attributable; from a known-good copy, whatever fsck_hfs reports was
# caused by THIS run and this way of ending it. A copy that fails the check is
# kept, not deleted, so it can be examined.
#
# A timed run has nobody to shut the Mac down, so it ends in SIGTERM. Expect the
# volume to be dirty; that is a pulled plug, and Disk First Aid's job. What must
# NOT appear is structural damage — that would mean writes never reached the card.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SECONDS_TO_RUN="${1:-40}"
KERNEL="${2:-${REPO_ROOT}/src/kernel/kernel8.img}"
SD="${REPO_ROOT}/qemu/sd.img"

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"

[ -f "$KERNEL" ] || { echo "No kernel at $KERNEL — build it first." >&2; exit 1; }
[ -f "$SD" ]     || { echo "No SD card at $SD — run make-sd-image.sh." >&2; exit 1; }

# hdiutil refuses an extensionless raw image ("image non reconnue"), so the copy
# lives in a temp directory under a name it will accept.
WORK="$(mktemp -d -t okapia)"
CARD="${WORK}/card.img"
LOG="$(mktemp -t okapia-serial)"
CARD_MOUNT="" ; IMAGE_DEV=""

cleanup() {
    [ -n "$IMAGE_DEV" ]  && hdiutil detach "$IMAGE_DEV" -quiet 2>/dev/null || true
    [ -n "$CARD_MOUNT" ] && hdiutil detach "$CARD_MOUNT" -quiet 2>/dev/null || true
}
trap cleanup EXIT

cp "$SD" "$CARD"

qemu-system-aarch64 -M raspi3b -kernel "$KERNEL" -serial stdio -display none \
    -drive "file=${CARD},if=sd,format=raw,cache=writethrough" \
    -device usb-kbd -device usb-mouse -semihosting > "$LOG" 2>&1 &
QPID=$!

sleep "$SECONDS_TO_RUN"

# SIGKILL, not SIGTERM: SIGTERM lets QEMU flush its block devices, which is the
# gentle case and proves little. With cache=writethrough there should be nothing
# left to flush, so the harshest possible ending is also the honest test — this
# is the BlueSCSI property we want, a pulled plug that costs no structure.
kill -9 $QPID 2>/dev/null || true
wait $QPID 2>/dev/null || true

tr -d '\r' < "$LOG" > "${LOG}.txt" && mv "${LOG}.txt" "$LOG"
printf '\nserial log: %s (%s lines)\n' "$LOG" "$(wc -l < "$LOG" | tr -d ' ')"
grep -E "okapia-68k: running|okapia-video:" "$LOG" | tail -2 || true

# --- did the guest write at all? ---
# Without this, a green verdict below could simply mean nothing was exercised.
CHANGED="$( { cmp -l "$SD" "$CARD" 2>/dev/null || true; } | wc -l | tr -d ' ')"
if [ "$CHANGED" = "0" ]; then
    printf '\nwrites    : NONE — the guest never wrote, the verdict below proves nothing\n'
else
    printf '\nwrites    : %s bytes changed on the card\n' "$CHANGED"
fi

# --- what did this run do to the volume? ---
CARD_MOUNT="$(hdiutil attach -readonly -nobrowse "$CARD" 2>/dev/null | tail -1 | awk '{print $1}')" || CARD_MOUNT=""
[ -n "$CARD_MOUNT" ] || { echo "could not read the card back" >&2; exit 1; }

GUEST_IMAGE="$(find /Volumes/OKAPIA -maxdepth 1 -name '*.image' 2>/dev/null | head -1)" || GUEST_IMAGE=""
[ -n "$GUEST_IMAGE" ] || { echo "no disk image on the card" >&2; exit 1; }

# MDB drAtrb, image offset 1034: bit 8 set (0100) means unmounted cleanly.
ATTR="$( { dd if="$GUEST_IMAGE" bs=1 skip=1034 count=2 2>/dev/null || true; } | xxd -p)"
printf '\nvolume flag: %s  (%s)\n' "$ATTR" \
    "$([ "$ATTR" = "0100" ] && echo 'not marked in use' || echo 'marked in use — needs Disk First Aid')"

# Without the explicit raw class hdiutil answers "image non reconnue": a bare
# HFS volume has no header for it to sniff.
IMAGE_DEV="$(hdiutil attach -nomount -readonly -imagekey diskimage-class=CRawDiskImage \
    "$GUEST_IMAGE" 2>/dev/null | head -1 | awk '{print $1}')" || IMAGE_DEV=""
if [ -n "$IMAGE_DEV" ] && fsck_hfs -n -q "$IMAGE_DEV" >/dev/null 2>&1; then
    echo "structure : OK — every write reached the card"
    RESULT=0
else
    echo "structure : DAMAGED — writes were lost, this is ours to fix"
    RESULT=1
fi

cleanup; IMAGE_DEV="" ; CARD_MOUNT=""

# --- and does the guest recover on its own? ---
# The SIGKILL above leaves the volume marked in use, which MountVol refuses.
# HfsRepair is supposed to scavenge it before the Mac ever sees it, so boot the
# same card again and check that it did. This is the one path that writes to the
# user's volume, so it must not be trusted on a manual test alone.
printf '\n'
"${REPO_ROOT}/scripts/env.sh" >/dev/null 2>&1 || true
RECOVER_LOG="$(mktemp -t okapiarecover)"
qemu-system-aarch64 -M raspi3b -kernel "$KERNEL" -serial "file:${RECOVER_LOG}" \
    -display none -drive "file=${CARD},if=sd,format=raw,cache=writethrough" \
    -device usb-kbd -device usb-mouse -semihosting > /dev/null 2>&1 &
RPID=$!
sleep 25
kill -9 $RPID 2>/dev/null || true
wait $RPID 2>/dev/null || true

if grep -q "repaired and marked clean" "$RECOVER_LOG" 2>/dev/null; then
    echo "recovery  : OK — the volume was scavenged and remounted clean"
elif grep -q "marked in use" "$RECOVER_LOG" 2>/dev/null; then
    echo "recovery  : FAILED — the volume stayed dirty" >&2
    RESULT=1
else
    echo "recovery  : nothing to repair (the guest never dirtied the volume)"
fi
rm -f "$RECOVER_LOG"
if [ "$RESULT" -eq 0 ]; then
    rm -rf "$WORK"
else
    echo "card kept for inspection: $CARD"
fi
exit "$RESULT"
