#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
# Clone one reference repository on demand, shallow, into reference/.
# References are read-only study material: never build from them, never modify them.
# Run without arguments to list what is known and what is already local.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REF_DIR="${REPO_ROOT}/reference"

# name | url | sparse-exclude | why you would want it
CATALOG='
bmc64|https://github.com/randyrossi/bmc64.git||Full emulator on bare-metal Circle. The closest architectural model: emulation loop, timing, audio, framebuffer, USB, SD config.
M5Tab-Macintosh|https://github.com/amcchord/M5Tab-Macintosh.git||Basilisk II on ESP32-P4. Dirty-tile video and SD flush strategy. READ ONLY: no declared licence, do not copy code.
macemu-jit|https://github.com/rcarmo/macemu-jit.git||68k->AArch64 JIT backend, and a test corpus usable without the JIT.
snow|https://github.com/twvd/snow.git||Macintosh II emulator and possible System 6 engine; compare its machine model and platform boundary with Mini vMac.
minivmac|https://github.com/minivmac/minivmac.git|extras|Compact Macintosh emulator and candidate System 6 engine; study its generated core and platform interface for a Circle port. Cloned without extras/ (ROMs and Apple disk images).
amiberry|https://github.com/BlitterStudio/amiberry.git||Upstream of the ARM/ARM64 JIT backend that macemu-jit imported.
infinite-mac|https://github.com/mihaip/infinite-mac.git|Images|Basilisk II and SheepShaver ported to WebAssembly, plus disk-image provisioning and a system-version catalogue. The other port facing the same constraints as ours (no threads, no SIGSEGV, no fixed mmap, no JIT): see docs/notes/research/infinite-mac.md. Its macemu fork is a submodule, cloned separately. Cloned without Images/ (hundreds of MB).
BlueSCSI-v2|https://github.com/BlueSCSI/BlueSCSI-v2.git||SCSI device emulation from disk images on an RP2040. The durability model Okapia targets, and a Macintosh image sanity check worth copying. GPLv3, same as us.
hfsutils|https://github.com/JotaRandom/hfsutils.git||HFS volume access (libhfs) and repair (hfsck), Robert Leslie 1996-1998, fork maintained by Pablo Lezaeta. GPLv2-or-later, so usable. libhfs scavenges a volume that was not unmounted cleanly, which is exactly the repair Okapia needs.
supermario|https://github.com/elliotnunn/supermario.git||Macintosh ROM sources (the SuperMario dump of 1994-02-09) plus Elliot Nunn'"'"'s buildable patchsets. What the Mac actually does, in StartMgr, HFS, ADB, ShutDownMgr and Color QuickDraw. NO LICENCE DECLARED, Apple code: read it to understand behaviour, never copy a line of it into Okapia.
executor|https://github.com/autc04/executor.git||Executor (ARDI), a clean-room reimplementation of the Macintosh Toolbox in C++, MIT licensed and so GPLv3-compatible. src/quickdraw/ is where code may legitimately come from when a routine is moved to native ARM.
'

list() {
    printf 'Reference repositories (cloned on demand)\n\n'
    while IFS='|' read -r name url exclude why; do
        [ -n "${name:-}" ] || continue
        if [ -d "${REF_DIR}/${name}/.git" ]; then status="local"; else status="-"; fi
        printf '  %-18s %-7s %s\n' "$name" "$status" "$why"
    done <<< "$(printf '%s' "$CATALOG" | grep -v '^$')"
    printf '\nUsage: %s <name>\n' "$(basename "$0")"
}

[ $# -eq 1 ] || { list; exit 0; }

want="$1"
entry="$(printf '%s' "$CATALOG" | grep "^${want}|" || true)"
[ -n "$entry" ] || { printf 'Unknown reference: %s\n\n' "$want" >&2; list >&2; exit 1; }

IFS='|' read -r name url exclude why <<< "$entry"
dest="${REF_DIR}/${name}"

if [ -d "${dest}/.git" ]; then
    printf '%s already present at %s\n' "$name" "$dest"
    exit 0
fi

mkdir -p "$REF_DIR"
printf 'Cloning %s\n  %s\n' "$name" "$why"

if [ -n "$exclude" ]; then
    git clone --depth 1 --filter=blob:none --sparse "$url" "$dest"
    git -C "$dest" sparse-checkout set --no-cone '/*' "!/${exclude}/"
else
    git clone --depth 1 "$url" "$dest"
fi

# infinite-mac keeps the interesting half — the emulators themselves — in a
# submodule, and it is a fork of the same upstream as external/macemu, so the
# whole port reads as one diff against the common base. Cloning the site without
# it would leave nothing to study.
if [ "$name" = "infinite-mac" ]; then
    printf 'Cloning its macemu fork (the port itself)\n'
    git clone --depth 50 -b infinite-mac-kanjitalk755 \
        https://github.com/mihaip/macemu "${dest}/macemu"
    git -C "${dest}/macemu" remote add ours "${REPO_ROOT}/external/macemu"
    git -C "${dest}/macemu" fetch --quiet ours --depth 200
    printf 'Common base with ours: %s\n' \
        "$(git -C "${dest}/macemu" merge-base HEAD ours/master)"
fi

printf 'Done: %s (%s)\n' "$dest" "$(du -sh "$dest" | cut -f1)"
