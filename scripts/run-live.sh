#!/usr/bin/env bash
# Run Okapia under QEMU with a visible window, so the emulated Mac's screen can
# be watched directly. Serial still goes to a log.
#   usage: run-live.sh [seconds]   (no argument: runs until closed)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"

KERNEL="${REPO_ROOT}/src/kernel/kernel8.img"
SD="${REPO_ROOT}/qemu/sd.img"
LOG="${REPO_ROOT}/qemu/serial.log"

[ -f "$KERNEL" ] || { echo "Build first: gmake -C src/kernel" >&2; exit 1; }

printf 'Serial log: %s\n' "$LOG"
qemu-system-aarch64 -M raspi3b -kernel "$KERNEL" \
    -drive "file=${SD},if=sd,format=raw" \
    -device usb-kbd -device usb-mouse \
    -serial "file:${LOG}" &
QPID=$!

if [ $# -ge 1 ]; then
    sleep "$1"
    kill $QPID 2>/dev/null || true
else
    wait $QPID
fi
