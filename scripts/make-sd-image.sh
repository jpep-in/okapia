#!/usr/bin/env bash
# Build a FAT32 SD card image for QEMU. Anything under qemu/sd-contents/ is copied in.
#   usage: make-sd-image.sh [size-in-MB]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${REPO_ROOT}/qemu/sd.img"
CONTENTS="${REPO_ROOT}/qemu/sd-contents"
SIZE_MB="${1:-64}"

command -v mformat >/dev/null || { echo "mtools missing: brew install mtools" >&2; exit 1; }

mkdir -p "$(dirname "$IMAGE")" "$CONTENTS"
rm -f "$IMAGE"

# QEMU's sd interface wants a power-of-two image size.
dd if=/dev/zero of="$IMAGE" bs=1m count="$SIZE_MB" status=none
mformat -i "$IMAGE" -F -v OKAPIA ::

if [ -n "$(ls -A "$CONTENTS" 2>/dev/null)" ]; then
    mcopy -i "$IMAGE" -s "$CONTENTS"/* ::
fi

printf 'SD image: %s (%s MB)\n' "$IMAGE" "$SIZE_MB"
mdir -i "$IMAGE" :: | sed 's/^/  /'
