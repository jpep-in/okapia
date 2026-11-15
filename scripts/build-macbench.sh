#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
# Build MacBench (tools/macbench) with Retro68, and optionally put it in a
# volume's Startup Items so the Macintosh runs it at the next start.
#   usage: build-macbench.sh [image] [-- cflags...]
#
#   build-macbench.sh                              build/macbench/MacBench.bin
#   build-macbench.sh qemu/sd-contents/sys755.img  ...and copied into that volume
#   build-macbench.sh -- -DDEPTH=16 -DAPPEND_RESULTS
#
# Retro68 is not one of install-tools.sh's tools: it is built once, by hand
# (github.com/autc04/Retro68, ./build-toolchain.bash), and found through
# RETRO68, the directory that holds its toolchain/ (default
# ~/github-repositories/Retro68-build). The flags are MacBench's compile-time
# switches, listed in docs/contributing/benchmarks.md#macbench; a change of
# flags rebuilds from scratch, since CMake would keep the old ones.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RETRO68="${RETRO68:-$HOME/github-repositories/Retro68-build}"
TOOLCHAIN="$RETRO68/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake"
OUT="$REPO_ROOT/build/macbench"

IMAGE=""
if [[ $# -gt 0 && "$1" != "--" ]]; then
    IMAGE="$1"; shift
fi
[[ $# -gt 0 && "$1" == "--" ]] && shift
CFLAGS="$*"

if [[ ! -f "$TOOLCHAIN" ]]; then
    echo "Retro68 not found: $TOOLCHAIN (set RETRO68)" >&2
    exit 1
fi

# The flags the last build used, so that a different set starts afresh.
if [[ -f "$OUT/.cflags" && "$(cat "$OUT/.cflags")" != "$CFLAGS" ]]; then
    rm -rf "$OUT"
fi
mkdir -p "$OUT"
printf '%s' "$CFLAGS" > "$OUT/.cflags"

cmake -S "$REPO_ROOT/tools/macbench" -B "$OUT" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
      -DCMAKE_C_FLAGS="$CFLAGS" > /dev/null
cmake --build "$OUT" > /dev/null
echo "$OUT/MacBench.bin${CFLAGS:+ ($CFLAGS)}"

if [[ -n "$IMAGE" ]]; then
    # Retro68's own hfsutils. Never on a card an emulator is using
    # (AGENTS.md, Data safety): this writes to the volume directly.
    BIN="$RETRO68/toolchain/bin"
    "$BIN/hmount" "$IMAGE" > /dev/null
    trap '"$BIN/humount" > /dev/null 2>&1 || true' EXIT
    "$BIN/hcopy" -m "$OUT/MacBench.bin" ":System Folder:Startup Items:"
    echo "copied into $IMAGE, System Folder:Startup Items"
fi
