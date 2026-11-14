#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
# The JIT gate: Basilisk II on a Raspberry Pi 4 under Linux, with and without
# the AArch64 JIT, so that Speedometer says whether the JIT is worth porting.
# See docs/notes/research/jit-68k.md, "The measurement that decides".
#
# THIS SCRIPT RUNS ON THE PI, not on the build machine: Raspberry Pi OS Lite
# 64-bit, copied over with the ROM and one disk image.
#
#   jit-gate-pi.sh setup <rom> <disk image>   install, clone, build, copy the files
#   jit-gate-pi.sh run jit|nojit|upstream     start one variant
#
#   jit       rcarmo/macemu-jit, JIT on, ordinary mode
#   nojit     the same binary, "jit false": the fork's own interpreter
#   upstream  kanjitalk755/macemu at Okapia's pinned commit, interpreter: the
#             sibling of what Okapia runs
#
# Each variant works on its own copy of the disk image; the one you gave is
# never written. End every run with Special > Shut Down.
set -euo pipefail

GATE="${OKAPIA_GATE_DIR:-$HOME/okapia-gate}"
FORK_URL=https://github.com/rcarmo/macemu-jit.git
FORK_SHA=ba4a541f7afb554069e7401841a48bbbf71004b2
UP_URL=https://github.com/kanjitalk755/macemu.git
UP_SHA=474ea0ab55cac933de35fa349820c9dd0944bf77
# A Pi 4 with 1 GB cannot hold four compilers of the generated JIT at once.
JOBS="${OKAPIA_GATE_JOBS:-2}"

die() { printf 'jit-gate: %s\n' "$*" >&2; exit 1; }

fetch_at() { # url sha dir
    if [ ! -d "$3/.git" ]; then
        git init -q "$3"
        git -C "$3" remote add origin "$1"
    fi
    git -C "$3" fetch -q --depth 1 origin "$2"
    git -C "$3" checkout -q FETCH_HEAD
}

build() { # dir, then configure arguments
    local dir="$1/BasiliskII/src/Unix"; shift
    ( cd "$dir"
      NO_CONFIGURE=1 ./autogen.sh
      # Direct addressing is what Okapia uses and what the JIT requires.
      ac_cv_have_asm_extended_signals=yes ./configure \
          --enable-sdl-video --enable-sdl-audio --enable-addressing=direct \
          --without-mon --without-esd --without-gtk --disable-nls "$@"
      make -j"$JOBS" )
}

setup() {
    [ $# -eq 2 ] || die "usage: $0 setup <rom> <disk image>"
    [ -f "$1" ] || die "no ROM at $1"
    [ -f "$2" ] || die "no disk image at $2"
    [ "$(uname -m)" = aarch64 ] || die "this needs a 64-bit system (uname -m says $(uname -m))"

    sudo apt-get update
    sudo apt-get install -y git build-essential autoconf automake libtool pkg-config \
        libsdl2-dev libmpfr-dev libgmp-dev libvncserver-dev libpng-dev

    mkdir -p "$GATE/files"
    cp "$1" "$GATE/files/rom"
    for v in jit nojit upstream; do cp "$2" "$GATE/files/disk-$v.img"; done

    fetch_at "$FORK_URL" "$FORK_SHA" "$GATE/macemu-jit"
    fetch_at "$UP_URL" "$UP_SHA" "$GATE/macemu-upstream"

    build "$GATE/macemu-jit" --enable-jit-compiler --enable-aarch64-jit-experimental
    build "$GATE/macemu-upstream" --disable-jit-compiler

    printf '\nBuilt. Start one with: %s run jit|nojit|upstream\n' "$0"
}

run() {
    local variant="${1:-}" bin jit
    case "$variant" in
        jit)      bin="$GATE/macemu-jit/BasiliskII/src/Unix/BasiliskII";      jit=true ;;
        nojit)    bin="$GATE/macemu-jit/BasiliskII/src/Unix/BasiliskII";      jit=false ;;
        upstream) bin="$GATE/macemu-upstream/BasiliskII/src/Unix/BasiliskII"; jit=false ;;
        *) die "usage: $0 run jit|nojit|upstream" ;;
    esac
    [ -x "$bin" ] || die "not built: $bin (run setup first)"

    # Okapia's machine: 256 MB, a Quadra, a 68040 with its FPU. No sound and no
    # network, so the three runs differ by the CPU engine and nothing else.
    local prefs="$GATE/prefs-$variant"
    cat > "$prefs" <<EOF
rom $GATE/files/rom
disk $GATE/files/disk-$variant.img
ramsize 268435456
modelid 14
cpu 4
fpu true
screen win/640/480
displaycolordepth 8
frameskip 1
nosound true
nogui true
jit $jit
jitfpu false
jitcachesize 131072
jitlazyflush false
EOF
    printf 'jit-gate: %s (jit %s), log in %s\n' "$variant" "$jit" "$GATE/log-$variant.txt"
    "$bin" --config "$prefs" 2> "$GATE/log-$variant.txt"
}

case "${1:-}" in
    setup) shift; setup "$@" ;;
    run)   shift; run "$@" ;;
    *)     sed -n '4,21p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
