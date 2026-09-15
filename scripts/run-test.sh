#!/usr/bin/env bash
# Headless regression run, then a verdict on what the run did to the volume.
#   usage: run-test.sh [seconds] [disk-image]
#
# With no image it tests qemu/sd.img as it stands. Given one — say
# qemu/sd-contents/boot71.img — it builds a throwaway card carrying that image
# as the boot disk, so a second System can be regression-tested without
# disturbing the card you actually use.
#
# The run works on a copy of qemu/sd.img — not to look away from corruption, but
# to be able to see it. On the master image damage accumulates across runs and
# nothing is attributable; from a known-good copy, whatever fsck_hfs reports was
# caused by THIS run and this way of ending it. A copy that fails the check is
# kept, not deleted, so it can be examined.
#
# A timed run has nobody to shut the Mac down, so it ends in SIGKILL. Expect the
# volume to be dirty; that is a pulled plug, and Disk First Aid's job. What must
# NOT appear is structural damage — that would mean writes never reached the card.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SECONDS_TO_RUN="${1:-40}"
IMAGE="${2:-}"
KERNEL="${REPO_ROOT}/src/kernel/kernel8.img"
SD="${REPO_ROOT}/qemu/sd.img"

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/target.sh"
# QEMU's raspi3b puts the peripherals at 0x3F000000 and a Pi 4 kernel addresses
# 0xFE000000, so the wrong build does not fail — it goes quiet. Say so instead.
okapia_require_target "$REPO_ROOT" qemu "$(basename "$0")" || exit 1

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

if [ -n "$IMAGE" ]; then
    [ -f "$IMAGE" ] || { echo "No such image: $IMAGE" >&2; exit 1; }
    # A card with no BasiliskII_Prefs falls back to /machd76.image, so the image
    # under test takes that name on the throwaway card. Renaming here rather
    # than writing preferences keeps the test from changing what it is testing.
    command -v mformat >/dev/null || { echo "mtools missing: brew install mtools" >&2; exit 1; }
    SIZE_MB=$(( ( $(stat -f%z "$IMAGE") / 1048576 ) + 32 ))
    POW=64; while [ "$POW" -lt "$SIZE_MB" ]; do POW=$(( POW * 2 )); done
    dd if=/dev/zero of="$CARD" bs=1m count="$POW" status=none
    mformat -i "$CARD" -F -v OKAPIA ::
    mcopy -i "$CARD" "$IMAGE" ::machd76.image
    mcopy -i "$CARD" "${REPO_ROOT}/qemu/sd-contents/okapia.rom" ::okapia.rom
    printf 'testing %s on a %s MB card\n' "$(basename "$IMAGE")" "$POW"
else
    cp "$SD" "$CARD"
fi

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

# Did the Macintosh ever finish starting?
#
# The duration is deliberately not shortened when it does: killing an idle Mac
# is the gentle case, and this test exists for the harsh one. What matters is
# that a green verdict cannot come from a run that never booted — the byte count
# below already guards one half of that, and this guards the other, because a
# Mac can write its way through a boot it never completes.
if grep -q "Macintosh idle" "$LOG"; then
    printf 'boot      : the Mac reported itself idle, so it did finish starting\n'
elif grep -q "no idle patch" "$LOG"; then
    printf 'boot      : this System has no idle patch, so the boot cannot be timed\n'
else
    printf 'boot      : WARNING, the Mac never reported itself idle in %s s\n' "$SECONDS_TO_RUN"
fi

# Which volume the Mac started from, straight from the kernel's own log.
BOOT_VOLUME="$(sed -n 's/.*Boot volume: \///p' "$LOG" | tail -1 | tr -d ' ')"

# --- did the guest write at all? ---
# Without this, a green verdict below could simply mean nothing was exercised.
# The volume and not the whole card: the kernel keeps okapia.log on the card
# too, so a card compared whole always shows writes, Mac or no Mac.
if [ -n "$IMAGE" ]; then
    CHANGED="?"
elif [ -n "$BOOT_VOLUME" ]; then
    CHANGED="$( { cmp -l <(mcopy -n -i "$SD" "::${BOOT_VOLUME}" -) \
                         <(mcopy -n -i "$CARD" "::${BOOT_VOLUME}" -) 2>/dev/null || true; } \
               | wc -l | tr -d ' ')"
else
    CHANGED="$( { cmp -l "$SD" "$CARD" 2>/dev/null || true; } | wc -l | tr -d ' ')"
fi
if [ "$CHANGED" = "?" ]; then
    printf '\nwrites    : not compared (card built for this test)\n'
elif [ "$CHANGED" = "0" ]; then
    printf '\nwrites    : NONE — the guest never wrote, the verdict below proves nothing\n'
else
    printf '\nwrites    : %s bytes changed %s\n' "$CHANGED" \
        "${BOOT_VOLUME:+in $BOOT_VOLUME}"
fi

if [ -n "$BOOT_VOLUME" ]; then
    printf 'volume    : %s (the one the Mac booted)\n' "$BOOT_VOLUME"
else
    printf 'volume    : not named in the log, falling back to the first *.image\n'
fi

# A small helper: report the volume state on the card as it stands.
#   $1 = label
# Sets VOL_FLAG and VOL_FSCK ("ok" / "bad" / "?").
inspect_card() {
    VOL_FLAG="?" ; VOL_FSCK="?"

    # A mount point of our own, never /Volumes/OKAPIA: every Okapia card is
    # called OKAPIA, so with a real one plugged in the test card mounts as
    # "OKAPIA 1" and a hard-coded path inspects somebody's actual Macintosh.
    # It did, reporting that card's crashed volume as this run's damage.
    local MNT="${WORK}/card"
    mkdir -p "$MNT"
    CARD_MOUNT="$(hdiutil attach -readonly -nobrowse -mountpoint "$MNT" "$CARD" 2>/dev/null | tail -1 | awk '{print $1}')" || CARD_MOUNT=""
    [ -n "$CARD_MOUNT" ] || { echo "could not read the card back" >&2; return 1; }

    # The volume the kernel actually started from, which it names in the log.
    # Guessing from the card instead would inspect whichever image sorts first
    # — and on a card with two Systems that is a volume the run may never have
    # touched, so a green verdict would mean nothing.
    GUEST_IMAGE=""
    if [ -n "$BOOT_VOLUME" ] && [ -f "${MNT}/${BOOT_VOLUME}" ]; then
        GUEST_IMAGE="${MNT}/${BOOT_VOLUME}"
    else
        # Also the path when the log named a volume the mounted card does not
        # show under that name — 8.3 truncation, a case difference from mcopy.
        # Guessing is worse than naming, but far better than giving up.
        [ -z "$BOOT_VOLUME" ] || echo "warning: ${BOOT_VOLUME} not found on the card, guessing" >&2
        GUEST_IMAGE="$(find "$MNT" -maxdepth 1 -name '*.image' 2>/dev/null | head -1)" || GUEST_IMAGE=""
    fi
    [ -f "$GUEST_IMAGE" ] || { cleanup; CARD_MOUNT=""; echo "no disk image on the card" >&2; return 1; }

    # MDB drAtrb, image offset 1034: bit 8 set (0100) means unmounted cleanly.
    VOL_FLAG="$( { dd if="$GUEST_IMAGE" bs=1 skip=1034 count=2 2>/dev/null || true; } | xxd -p)"

    # Without the explicit raw class hdiutil answers "image non reconnue": a
    # bare HFS volume has no header for it to sniff.
    IMAGE_DEV="$(hdiutil attach -nomount -readonly -imagekey diskimage-class=CRawDiskImage \
        "$GUEST_IMAGE" 2>/dev/null | head -1 | awk '{print $1}')" || IMAGE_DEV=""
    if [ -n "$IMAGE_DEV" ]; then
        if fsck_hfs -n -q "$IMAGE_DEV" >/dev/null 2>&1; then VOL_FSCK="ok"; else VOL_FSCK="bad"; fi
    fi
    cleanup; IMAGE_DEV="" ; CARD_MOUNT=""
    return 0
}

# --- after the kill: dirty is expected, damage is not ---
# A volume marked in use is what a pulled plug leaves on a real Mac too, and
# fsck_hfs condemns it on that basis alone. So this reading is informational;
# the verdict comes after the recovery below.
inspect_card || exit 1
printf '\nafter kill: flag %s (%s), fsck says %s\n' "$VOL_FLAG" \
    "$([ "$VOL_FLAG" = "0100" ] && echo 'clean' || echo 'in use, as expected')" "$VOL_FSCK"

# --- does the guest recover on its own? ---
# HfsRepair is supposed to scavenge the volume before the Mac ever sees it. Boot
# the same card again to make it happen — with a marker asking the kernel to
# stop right after the repair, because otherwise the Mac remounts the volume
# within two seconds and marks it in use again, hiding the very thing being
# measured. This is the one path that writes to someone's disk unasked, so it
# does not get to rely on goodwill.
mcopy -i "$CARD" -o /dev/null ::repair-only 2>/dev/null || \
    { : > "${WORK}/marker"; mcopy -i "$CARD" -o "${WORK}/marker" ::repair-only; }

RECOVER_LOG="$(mktemp -t okapiarecover)"
qemu-system-aarch64 -M raspi3b -kernel "$KERNEL" -serial "file:${RECOVER_LOG}" \
    -display none -drive "file=${CARD},if=sd,format=raw,cache=writethrough" \
    -device usb-kbd -device usb-mouse -semihosting > /dev/null 2>&1 &
RPID=$!
sleep 20
kill -9 $RPID 2>/dev/null || true
wait $RPID 2>/dev/null || true

RESULT=0
if grep -q "repaired and marked clean" "$RECOVER_LOG" 2>/dev/null; then
    echo "recovery  : the volume was scavenged and remounted clean"
elif grep -q "marked in use" "$RECOVER_LOG" 2>/dev/null; then
    echo "recovery  : FAILED — the volume stayed dirty" >&2
    RESULT=1
else
    echo "recovery  : nothing to repair (the guest never dirtied the volume)"
fi
rm -f "$RECOVER_LOG"

# --- the verdict: what the volume looks like once recovery has run ---
# This is the property that matters. A hard kill may leave the volume marked in
# use; what it must never leave is structural damage that survives the repair.
inspect_card || exit 1
printf 'after repair: flag %s, fsck says %s\n' "$VOL_FLAG" "$VOL_FSCK"

if [ "$VOL_FSCK" = "bad" ]; then
    echo "VERDICT   : FAILED — the volume is still damaged after repair" >&2
    RESULT=1
elif [ -z "$BOOT_VOLUME" ]; then
    # The kernel did not name what it started from, so the volume inspected
    # above was chosen by sorting order. It may be one the run never touched,
    # and a clean bill on it says nothing at all. docs/topics/storage.md: anything that
    # judges "did this survive" must name what it judged.
    echo "VERDICT   : INCONCLUSIVE — the log names no boot volume, so the" >&2
    echo "            volume inspected was a guess. Nothing is proved." >&2
    RESULT=1
elif [ "$RESULT" -eq 0 ]; then
    echo "VERDICT   : OK — a hard kill costs nothing the repair cannot undo"
fi

if [ "$RESULT" -eq 0 ]; then
    rm -rf "$WORK"
else
    echo "card kept for inspection: $CARD"
fi
exit "$RESULT"
