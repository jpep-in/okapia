#!/usr/bin/env bash
# Run a built kernel under QEMU (Raspberry Pi 3, AArch64).
#   usage: run-qemu.sh [path/to/kernel8.img] [extra qemu args...]
#   -s -S can be appended to wait for GDB on :1234
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL="${1:-${REPO_ROOT}/tests/smoke/kernel8.img}"
shift || true

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/target.sh"
# QEMU's raspi3b puts the peripherals at 0x3F000000 and a Pi 4 kernel addresses
# 0xFE000000, so the wrong build does not fail — it goes quiet. Say so instead.
okapia_require_target "$REPO_ROOT" qemu "$(basename "$0")" || exit 1
SD_IMAGE="${REPO_ROOT}/qemu/sd.img"

[ -f "$KERNEL" ] || { echo "No kernel at $KERNEL — build it first." >&2; exit 1; }

# -semihosting lets a halting kernel exit QEMU by itself (LEAVE_QEMU_ON_HALT,
# sysinit.cpp:200). Without it the Mac can shut down and QEMU still hangs around.
ARGS=(-M raspi3b -kernel "$KERNEL" -serial stdio -semihosting)
# cache=writethrough: QEMU defaults to writeback, so guest writes would sit in the
# host page cache instead of the image. Real hardware has no such window — Circle
# writes straight through to the card — so don't let QEMU invent one.
[ -f "$SD_IMAGE" ] && ARGS+=(-drive "file=${SD_IMAGE},if=sd,format=raw,cache=writethrough")
# The mouse cursor does not work under QEMU (documented in Circle's doc/qemu.txt),
# but the device is still enumerated and its events arrive.
ARGS+=(-device usb-kbd -device usb-mouse)

exec qemu-system-aarch64 "${ARGS[@]}" "$@"
