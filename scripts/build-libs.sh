#!/usr/bin/env bash
# Configure and build circle-stdlib (newlib + Circle) for one target.
# Run once per target; scripts/build-qemu.sh and build-pi.sh only build our kernel.
#   usage: build-libs.sh qemu | 3 | 4 | 5
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STDLIB="${REPO_ROOT}/external/circle-stdlib"
TARGET="${1:-qemu}"

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"

case "$TARGET" in
    qemu) CONFIGURE_ARGS=(-r 3 --qemu) ;;
    3|4|5) CONFIGURE_ARGS=(-r "$TARGET") ;;
    *) echo "usage: $(basename "$0") qemu|3|4|5" >&2; exit 1 ;;
esac

# macOS ships Bash 3.2 (no mapfile) and BSD getopt (no --long): circle-stdlib's
# configure needs GNU versions of both. install-tools.sh puts GNU getopt on PATH.
BASH5="$(brew --prefix bash)/bin/bash"
[ -x "$BASH5" ] || { echo "GNU bash 5 missing: brew install bash" >&2; exit 1; }

printf '\n==> Configuring circle-stdlib for %s\n' "$TARGET"
cd "$STDLIB"
"$BASH5" ./configure --aarch64 -p aarch64-none-elf- "${CONFIGURE_ARGS[@]}"

printf '\n==> Building newlib and Circle\n'
gmake newlib circle

printf '\n==> Checking libraries\n'
missing=0
for lib in libs/circle/lib/libcircle.a libs/circle/lib/usb/libusb.a \
           libs/circle/lib/input/libinput.a libs/circle/lib/fs/libfs.a \
           libs/circle/lib/sched/libsched.a libs/circle/lib/net/libnet.a \
           libs/circle/addon/SDCard/libsdcard.a libs/circle/addon/fatfs/libfatfs.a \
           libs/circle/addon/qemu/libqemusupport.a \
           libs/circle/addon/wlan/libwlan.a \
           install/aarch64-none-circle/lib/libc.a install/aarch64-none-circle/lib/libm.a \
           install/aarch64-none-circle/lib/libcirclenewlib.a; do
    if [ -f "$lib" ]; then printf '  ok        %s\n' "${lib##*/}"
    else printf '  MISSING   %s\n' "$lib"; missing=1; fi
done
[ "$missing" -eq 0 ] || { echo "Build incomplete." >&2; exit 1; }

printf '\n==> circle-stdlib ready for %s\n' "$TARGET"
