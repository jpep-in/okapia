#!/usr/bin/env bash
# Stage the boot files for a real Raspberry Pi 4 card in rpi4-sd-contents/.
#   usage: make-pi-sd.sh
#
# QEMU takes the kernel on its command line and needs nothing else; a real board
# takes everything from the card, so this directory holds what QEMU never had:
# the Raspberry Pi firmware, the device tree, the ARM stub and a config.txt.
#
# Disk images are deliberately left out — that is the "essentials" part. They are
# hundreds of megabytes, they change on their own as the Mac writes to them, and
# a staging directory that copies them would be a second copy going stale. Copy
# the one you want onto the card by hand; the script names the candidates.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOOT="${REPO_ROOT}/external/circle-stdlib/libs/circle/boot"
CONTENTS="${REPO_ROOT}/qemu/sd-contents"
KERNEL="${REPO_ROOT}/src/kernel/kernel8.img"
OUT="${REPO_ROOT}/rpi4-sd-contents"

# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/env.sh"

# The trap this script exists to close: one tree is built for one target at a
# time, and a kernel built against the QEMU configuration carries CIRCLE_QEMU
# and NO_SDHOST — no real SD driver at all. It boots under QEMU and dies on the
# board with nothing on the serial port to say why.
# shellcheck source=/dev/null
. "${REPO_ROOT}/scripts/target.sh"
okapia_require_target "$REPO_ROOT" pi4 "$(basename "$0")" || exit 1

[ -f "$KERNEL" ] || { echo "No kernel at ${KERNEL} — make -C src/kernel" >&2; exit 1; }

# Downloaded, not built, and not in the repository. Ask rather than fetch: this
# needs the network, and the rest of the script does not.
FIRMWARE=(start4.elf fixup4.dat bcm2711-rpi-4-b.dtb)
missing=()
for file in "${FIRMWARE[@]}"; do
    [ -f "${BOOT}/${file}" ] || missing+=("$file")
done
if [ "${#missing[@]}" -ne 0 ]; then
    printf 'Missing Raspberry Pi firmware: %s\n' "${missing[*]}" >&2
    printf 'Download it (needs the network): make -C %s\n' "${BOOT#"${REPO_ROOT}/"}" >&2
    exit 1
fi

# This one is built here from Circle's own source with our toolchain, so build
# it rather than complaining about it. Needed on the Pi 4 for FIQ support, and
# named by the [pi4] section of config64.txt.
if [ ! -f "${BOOT}/armstub8-rpi4.bin" ]; then
    printf '==> Building armstub8-rpi4.bin\n'
    "$MAKE" -C "$BOOT" armstub64 >/dev/null
fi

rm -rf "$OUT"
mkdir -p "$OUT"

for file in "${FIRMWARE[@]}" armstub8-rpi4.bin; do
    cp "${BOOT}/${file}" "$OUT/"
done

# config64.txt names kernel8-rpi4.img in its [pi4] section; our Makefile always
# writes kernel8.img whatever the target. Rename here rather than patch the
# config, so the card keeps Circle's own convention.
cp "$KERNEL" "${OUT}/kernel8-rpi4.img"

# The appended lines go under [all]: config.txt sections are sticky, and
# config64.txt ends inside [pi5], so anything added without this would apply to
# a board that is not the one being staged.
{
    cat "${BOOT}/config64.txt"
    cat <<'EOF'

[all]

# Okapia: the screen belongs to the Mac and the firmware sizes the frame buffer
# (okapia_output.cpp asks for 0x0). The firmware reads the monitor's EDID and
# takes its preferred mode, which is what gives a 2560x1440 monitor 2560x1440;
# the kernel logs both, and warns when they differ. Forcing a mode here used to
# pin every monitor to 1080p. hdmi_force_hotplug still brings HDMI up on a board
# started with nothing plugged in. To pin a mode anyway, CEA 1080p60 for example:
#hdmi_group=1
#hdmi_mode=16
hdmi_force_hotplug=1
#
# No TV safety border. The firmware reserves one around CEA modes -- a 1920x1080
# output came back as 1824x984 in a black frame -- and a computer display has
# nothing to hide there.
disable_overscan=1
#
# No rainbow square while the firmware loads, and no pause before the kernel:
# the first thing on the screen is Okapia's own. Neither is needed by anything.
disable_splash=1
boot_delay=0
#
# A monitor the firmware will not drive at its own size can be told. Seen on a
# Philips 246B1, whose EDID prefers 2560x1440 and which the firmware drove at
# CEA 1080p anyway; 87 is a custom mode and hdmi_cvt describes it (60 Hz,
# 16:9, reduced blanking):
#hdmi_group=2
#hdmi_mode=87
#hdmi_cvt=2560 1440 60 3 0 0 1
#max_framebuffer_width=2560
#max_framebuffer_height=1440
EOF
} > "${OUT}/config.txt"

# Everything staged for QEMU that is not a disk image: the ROMs, the
# preferences, the shared folder. kernel8.img is excluded by the same rule that
# excludes the disk images and would be the stale QEMU build anyway.
if [ -d "$CONTENTS" ]; then
    while IFS= read -r entry; do
        name="$(basename "$entry")"
        case "$name" in
            # .DS_Store and friends: the Finder's, not the Macintosh's.
            .*) continue ;;
            *.image|*.img|*.toast|*.hda|*.dsk|*.hfv) continue ;;
        esac
        cp -R "$entry" "$OUT/"
    done < <(find "$CONTENTS" -mindepth 1 -maxdepth 1)
fi

printf '\n==> %s\n' "${OUT#"${REPO_ROOT}/"}"
(cd "$OUT" && find . -mindepth 1 -maxdepth 1 | sed 's|^\./|  |' | sort)
printf '  %s KB total\n' "$(( $(du -sk "$OUT" | cut -f1) ))"

# Named, not copied: the card needs one, and which one is the user's choice.
printf '\nDisk images left out — copy one to the card by hand:\n'
find "$CONTENTS" -mindepth 1 -maxdepth 1 \
     \( -name '*.image' -o -name '*.img' -o -name '*.toast' \) 2>/dev/null |
    grep -v '/kernel8\.img$' | sed "s|^${CONTENTS}/|  |" | sort || true
printf '\nThen: cp -R %s/ /Volumes/OKAPIA/ && diskutil eject /Volumes/OKAPIA\n' \
    "${OUT#"${REPO_ROOT}/"}"

# Said before the card is written, because the kernel can only log the cluster
# size once it is. FatFs keeps no map of a file's clusters: 4 KB clusters make a
# 500 MB image a chain of 128 000 links, 32 KB make it 16 000.
printf '\nFormat the card FAT32 with 32 KB clusters: every seek inside a disk image\n'
printf 'walks the FAT a cluster at a time. On macOS, after finding the card with\n'
printf 'diskutil list:\n'
printf '  sudo newfs_msdos -F 32 -c 64 -v OKAPIA /dev/rdiskNs1\n'
