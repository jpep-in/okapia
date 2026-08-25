#!/usr/bin/env bash
# Run a built kernel under QEMU (Raspberry Pi 3, AArch64).
#   usage: run-qemu.sh [path/to/kernel8.img] [extra qemu args...]
#   -s -S can be appended to wait for GDB on :1234
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL="${1:-${REPO_ROOT}/tests/smoke/kernel8.img}"
shift || true
SD_IMAGE="${REPO_ROOT}/qemu/sd.img"

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"

[ -f "$KERNEL" ] || { echo "No kernel at $KERNEL — build it first." >&2; exit 1; }

ARGS=(-M raspi3b -kernel "$KERNEL" -serial stdio)
[ -f "$SD_IMAGE" ] && ARGS+=(-drive "file=${SD_IMAGE},if=sd,format=raw")
# The mouse cursor does not work under QEMU (documented in Circle's doc/qemu.txt),
# but the device is still enumerated and its events arrive.
ARGS+=(-device usb-kbd -device usb-mouse)

exec qemu-system-aarch64 "${ARGS[@]}" "$@"
