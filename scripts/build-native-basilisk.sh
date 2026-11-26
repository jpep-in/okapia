#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
# Build a desktop Basilisk II (SDL, macOS or Linux) from a kanjitalk755/macemu
# clone with Okapia's FPU and translator in it, to measure them on the host
# with tools/macbench/run.py before a card ever sees them.
#   usage: build-native-basilisk.sh <name> [configure options...]
#
#   build-native-basilisk.sh softfloat --enable-fpe=softfloat --enable-jit-arm64
#   build-native-basilisk.sh mpfr                                   # upstream's choice on ARM
#
# The clone (MACEMU_CLONE, default ~/github-repositories/macemu) is copied to
# build/native/<name>/, never built in place; Okapia's patches 0013 to 0015
# (the softfloat FPU) are applied to the copy, and uae_cpu_2021/jit_arm64/ is
# taken from external/macemu, so that the translator measured is the one the
# kernel builds. Dependencies (SDL2, GMP, MPFR, autotools) come from
# MACEMU_DEPS, default ~/github-repositories/macemu-deps, laid out as the
# macemu README's macOS instructions leave them. The binary is
# build/native/<name>/BasiliskII.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLONE="${MACEMU_CLONE:-$HOME/github-repositories/macemu}"
DEPS="${MACEMU_DEPS:-$HOME/github-repositories/macemu-deps}"

[[ $# -ge 1 ]] || { echo "usage: $(basename "$0") <name> [configure options...]" >&2; exit 1; }
NAME="$1"; shift
OUT="$REPO_ROOT/build/native/$NAME"
SRC="$OUT/macemu"

[[ -d "$CLONE/BasiliskII" ]] || { echo "No macemu clone at $CLONE (set MACEMU_CLONE)" >&2; exit 1; }

mkdir -p "$OUT"
rsync -a --delete --exclude .git --exclude SheepShaver --exclude 'BasiliskII/src/Unix/obj' \
      "$CLONE/" "$SRC/"
(
    cd "$SRC"
    git init -q 2>/dev/null || true
    for patch in "$REPO_ROOT"/patches/macemu/001[345]-*.patch; do
        git apply --whitespace=nowarn "$patch"
    done
)
rsync -a --delete "$REPO_ROOT/external/macemu/BasiliskII/src/uae_cpu_2021/jit_arm64/" \
      "$SRC/BasiliskII/src/uae_cpu_2021/jit_arm64/"

export PATH="$DEPS/bin:$PATH"
export CPPFLAGS="-I$DEPS/include -F$DEPS/Frameworks ${CPPFLAGS:-}"
# slirp is K&R C, which the C23 a recent autoconf asks for refuses.
export CFLAGS="${CFLAGS:--g -O2} -std=gnu99"
# The rpath lets configure's test programs, and the emulator, find SDL2.framework.
export LDFLAGS="-L$DEPS/lib -F$DEPS/Frameworks -Wl,-rpath,$DEPS/Frameworks ${LDFLAGS:-}"
cd "$SRC/BasiliskII/src/Unix"
NO_CONFIGURE=1 ./autogen.sh > "$OUT/autogen.log" 2>&1
./configure --enable-sdl-video --enable-sdl-audio --enable-sdl-framework \
            --enable-sdl-framework-prefix="$DEPS/Frameworks" --without-mon --disable-gtk "$@" > "$OUT/configure.log" 2>&1 \
    || { tail -20 "$OUT/configure.log"; exit 1; }
sed -n '/configuration summary/,/Configuration done/p' "$OUT/configure.log"
make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" > "$OUT/make.log" 2>&1 || { tail -30 "$OUT/make.log"; exit 1; }
cp BasiliskII "$OUT/BasiliskII"
echo "built $OUT/BasiliskII"
