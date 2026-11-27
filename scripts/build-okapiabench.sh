#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
# Build OkapiaBench (tools/okapiabench) with Retro68 for the 68k and the
# PowerPC, join the two into one fat application, and optionally put it in a
# volume's Startup Items so the Macintosh runs it at the next start.
#   usage: build-okapiabench.sh [--68k-only|--ppc-only] [image] [-- cflags...]
#
#   build-okapiabench.sh                              build/okapiabench/OkapiaBench.bin (fat)
#   build-okapiabench.sh qemu/sd-contents/sys755.img  ...and copied into that volume
#   build-okapiabench.sh -- -DONLY=cpu_crc -DNO_RECAP
#   build-okapiabench.sh --68k-only                   build/okapiabench/68k/OkapiaBench.bin
#   REFERENCE=ref.h build-okapiabench.sh              the recap scores against ref.h (run.py --emit-header)
#   NO_EMBENCH=1 build-okapiabench.sh                 without Embench (GPL): MIT and Apache-2.0 code only
#
# Retro68 is not one of install-tools.sh's tools: it is built once, by hand
# (github.com/autc04/Retro68, ./build-toolchain.bash --no-carbon: with the
# PowerPC, which --no-ppc would leave out), and found through RETRO68, the
# directory that holds its toolchain/ (default ~/github-repositories/Retro68-build).
# The flags are OkapiaBench's compile-time switches, listed in
# docs/contributing/benchmarks.md#okapiabench; a change of flags rebuilds
# from scratch, since CMake would keep the old ones.
#
# Formerly build-macbench.sh (MacBench, renamed OkapiaBench on 2026-11-27).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RETRO68="${RETRO68:-$HOME/github-repositories/Retro68-build}"
TC="$RETRO68/toolchain"
OUT="$REPO_ROOT/build/okapiabench"

ARCHS="68k ppc"
case "${1:-}" in
    --68k-only) ARCHS="68k"; shift ;;
    --ppc-only) ARCHS="ppc"; shift ;;
esac
IMAGE=""
if [[ $# -gt 0 && "$1" != "--" ]]; then
    IMAGE="$1"; shift
fi
[[ $# -gt 0 && "$1" == "--" ]] && shift
CFLAGS="$*"

toolchain_file() {
    case "$1" in
        68k) echo "$TC/m68k-apple-macos/cmake/retro68.toolchain.cmake" ;;
        ppc) echo "$TC/powerpc-apple-macos/cmake/retroppc.toolchain.cmake" ;;
    esac
}

for arch in $ARCHS; do
    if [[ ! -f "$(toolchain_file $arch)" ]]; then
        echo "Retro68 for $arch not found: $(toolchain_file $arch) (set RETRO68; the PowerPC" >&2
        echo "needs a toolchain built without --no-ppc, or use --68k-only)" >&2
        exit 1
    fi
done

for arch in $ARCHS; do
    dir="$OUT/$arch"
    # The flags the last build used, so that a different set starts afresh.
    if [[ -f "$dir/.cflags" && "$(cat "$dir/.cflags")" != "$CFLAGS" ]]; then
        rm -rf "$dir"
    fi
    mkdir -p "$dir"
    printf '%s' "$CFLAGS" > "$dir/.cflags"
    cmake -S "$REPO_ROOT/tools/okapiabench" -B "$dir" -DCMAKE_TOOLCHAIN_FILE="$(toolchain_file $arch)" \
          -DCMAKE_C_FLAGS="$CFLAGS" -DREFERENCE="${REFERENCE:+$(cd "$(dirname "$REFERENCE")" && pwd)/$(basename "$REFERENCE")}" \
          -DNO_EMBENCH="${NO_EMBENCH:+ON}" > /dev/null
    cmake --build "$dir" -j "$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" > /dev/null
done

if [[ "$ARCHS" == "68k ppc" ]]; then
    # One application: the 68k CODE resources (Elf2Mac's output) and the
    # PowerPC fragment (MakePEF's) in the data fork, with a 'cfrg' to find it.
    "$TC/bin/Rez" "$REPO_ROOT/tools/okapiabench/fat.r" -I"$TC/RIncludes" -I"$REPO_ROOT/tools/okapiabench" \
        --copy "$OUT/68k/OkapiaBench.code.bin" --data "$OUT/ppc/OkapiaBench.pef" \
        -t APPL -c OKBN -o "$OUT/OkapiaBench.bin"
    BIN="$OUT/OkapiaBench.bin"
else
    BIN="$OUT/$ARCHS/OkapiaBench.bin"
fi
echo "$BIN${CFLAGS:+ ($CFLAGS)}"

if [[ -n "$IMAGE" ]]; then
    # Retro68's own hfsutils. Never on a card an emulator is using
    # (AGENTS.md, Data safety): this writes to the volume directly.
    BINDIR="$TC/bin"
    "$BINDIR/hmount" "$IMAGE" > /dev/null
    trap '"$BINDIR/humount" > /dev/null 2>&1 || true' EXIT
    "$BINDIR/hcopy" -m "$BIN" ":System Folder:Startup Items:"
    echo "copied into $IMAGE, System Folder:Startup Items"
fi
