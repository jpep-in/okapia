#!/usr/bin/env bash
# Build a FAT32 SD card image for QEMU. Anything under qemu/sd-contents/ is copied in.
#   usage: make-sd-image.sh [size-in-MB]
#
# With no size, the card is sized to hold what is staged. The old default of
# 64 MB predates the disk images living there, and it turned every argument-less
# run into a truncated card — silently, because the copy failed halfway through
# after the previous card had already been deleted.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${REPO_ROOT}/qemu/sd.img"
CONTENTS="${REPO_ROOT}/qemu/sd-contents"
SIZE_MB="${1:-}"

command -v mformat >/dev/null || { echo "mtools missing: brew install mtools" >&2; exit 1; }

# Rewriting the card under a running emulator destroys that session's volume,
# and the damage looks like a mysterious boot failure afterwards. run-live.sh
# refuses to start on a busy card; this has to refuse to replace one.
if command -v lsof >/dev/null && [ -f "$IMAGE" ] && lsof -t -- "$IMAGE" >/dev/null 2>&1; then
    echo "Refusing to rebuild: ${IMAGE} is in use by another process." >&2
    lsof -- "$IMAGE" >&2 || true
    exit 1
fi

mkdir -p "$(dirname "$IMAGE")" "$CONTENTS"

# Seed the preferences the first time only. The kernel would write its own file
# on a card that has none, but that one lands inside the image and is lost at
# the next rebuild; this one lives in sd-contents/ and survives, which is what
# makes a test card reproducible. An existing file is never touched — it is the
# user's configuration by then.
# The file is called BasiliskII_Prefs whichever Macintosh reads it: the
# preferences are engine-agnostic by design (docs/preferences.md) and prefs_circle.cpp
# reads that one path from both.
PREFS="${CONTENTS}/BasiliskII_Prefs"
if [ ! -f "$PREFS" ]; then
    cp "${REPO_ROOT}/qemu/BasiliskII_Prefs.default" "$PREFS"
    printf 'preferences: seeded %s from %s\n' "$PREFS" \
        "qemu/BasiliskII_Prefs.default"
fi

# What has to fit, plus room for FAT structures and rounding.
NEEDED_MB=$(( ( $(du -sk "$CONTENTS" | cut -f1) / 1024 ) + 16 ))

# QEMU's sd interface wants a power-of-two image size.
round_up_pow2() {
    local n=64
    while [ "$n" -lt "$1" ]; do n=$(( n * 2 )); done
    printf '%s' "$n"
}

if [ -z "$SIZE_MB" ]; then
    SIZE_MB="$(round_up_pow2 "$NEEDED_MB")"
elif [ "$SIZE_MB" -lt "$NEEDED_MB" ]; then
    # Said now, not halfway through the copy: by then the old card is gone.
    printf 'Refusing to build a %s MB card: %s holds %s MB.\n' \
        "$SIZE_MB" "$CONTENTS" "$NEEDED_MB" >&2
    printf 'Use at least %s MB, or leave the size out to have it chosen.\n' \
        "$(round_up_pow2 "$NEEDED_MB")" >&2
    exit 1
fi

# Built beside the target and moved into place only once it is complete. The
# previous version of this script deleted the card first and wrote in place, so
# any failure — a full disk, a short copy — left a card that boots into nothing
# and no way back except rebuilding from sd-contents/.
STAGING="${IMAGE}.new"
trap 'rm -f "$STAGING"' EXIT

dd if=/dev/zero of="$STAGING" bs=1m count="$SIZE_MB" status=none

# The largest clusters FAT32 allows at this size. FatFs keeps no map of a file's
# clusters, so a seek inside a disk image follows the chain one link at a time
# and every read stops at a cluster boundary: mformat's default of 4 KB made
# the 500 MB image a chain of 128 000 links. FAT32 also needs more than 65 525
# clusters, or FatFs reads the volume as FAT16 — hence 70 000, with room for
# the FATs themselves. A card too small for 8 KB keeps mformat's own choice.
CLUSTER_SECTORS=""
for spc in 64 32 16; do
    if [ $(( SIZE_MB * 2048 / spc )) -ge 70000 ]; then
        CLUSTER_SECTORS="$spc"
        break
    fi
done
mformat -i "$STAGING" -F ${CLUSTER_SECTORS:+-c "$CLUSTER_SECTORS"} -v OKAPIA ::

if [ -n "$(ls -A "$CONTENTS" 2>/dev/null)" ]; then
    mcopy -i "$STAGING" -s "$CONTENTS"/* ::
fi

mv "$STAGING" "$IMAGE"
trap - EXIT

printf 'SD image: %s (%s MB, %s MB of contents)\n' "$IMAGE" "$SIZE_MB" "$NEEDED_MB"
mdir -i "$IMAGE" :: | sed 's/^/  /'
