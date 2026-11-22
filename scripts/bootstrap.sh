#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
# Fetch and pin the build dependencies. Idempotent: safe to re-run after a
# network failure. Reference repos are NOT fetched here — see fetch-reference.sh.
set -euo pipefail

CIRCLE_STDLIB_URL="https://github.com/smuehlst/circle-stdlib.git"
CIRCLE_STDLIB_REF="v21"                                       # upstream tags releases
MACEMU_URL="https://github.com/kanjitalk755/macemu.git"
MACEMU_REF="474ea0ab55cac933de35fa349820c9dd0944bf77"         # no useful tags: pin a SHA
# The 68k core with an AArch64 JIT (uae_cpu_2026), built only with OKAPIA_JIT=1.
# Nothing else is taken from this tree: Basilisk itself stays kanjitalk755's.
MACEMU_JIT_URL="https://github.com/rcarmo/macemu-jit.git"
MACEMU_JIT_REF="ba4a541f7afb554069e7401841a48bbbf71004b2"     # no tags for the JIT work
# stb_truetype, the rasteriser of the firmware's outline fonts (one header).
# No tags: the last commit to it before the autumn 2026 rework, stable since.
STB_URL="https://github.com/nothings/stb.git"
STB_REF="52a4ba61ef8d75b4be0d24bc70c08fb02499d7b7"
# miniz (MIT), the desktop's reader of release zips (src/hosts/desktop/install*).
MINIZ_URL="https://github.com/richgel999/miniz.git"
MINIZ_REF="3.1.2"

# circle-stdlib also declares doctest, json, mbedtls and mongoose. Those serve
# its TLS support and its samples only: we never build them.
CIRCLE_STDLIB_SUBMODULES="libs/circle libs/circle-newlib"
# Circle's own submodules: hostap carries wpa_supplicant, needed for WPA2 on the
# built-in WiFi. Its headers are included unconditionally by circle-stdlib, so it
# is required to build at all. lvgl is a GUI toolkit we never use.
CIRCLE_SUBMODULES="addon/wlan/hostap"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

say()  { printf '\n==> %s\n' "$1"; }
fail() { printf 'FAILED: %s\n' "$1" >&2; exit 1; }

[ -f scripts/env.sh ] || fail "run scripts/install-tools.sh first"

add() {  # url path ref
    local url="$1" path="$2" ref="$3"
    if [ -e "${path}/.git" ]; then
        echo "  ${path} already cloned"
    else
        say "Cloning ${path}"
        git submodule add --force "$url" "$path"
    fi
    git -C "$path" fetch --quiet --tags origin
    git -C "$path" checkout --quiet "$ref"
}

say "Build dependencies"
add "$CIRCLE_STDLIB_URL" external/circle-stdlib "$CIRCLE_STDLIB_REF"
# --force re-checks-out worktrees left empty by an interrupted clone: git records
# the submodule as initialised even when the checkout never happened.
git -C external/circle-stdlib submodule update --init --force $CIRCLE_STDLIB_SUBMODULES
git -C external/circle-stdlib/libs/circle submodule update --init --force $CIRCLE_SUBMODULES

add "$MACEMU_URL" external/macemu "$MACEMU_REF"
add "$MACEMU_JIT_URL" external/macemu-jit "$MACEMU_JIT_REF"
add "$STB_URL" external/stb "$STB_REF"
add "$MINIZ_URL" external/miniz "$MINIZ_REF"

# The exceptions to "never edit external/", each explaining itself in its own
# header. Idempotent, so running bootstrap again is safe.
say "Patches"
"$(dirname "${BASH_SOURCE[0]}")/apply-patches.sh"

say "Checks"
check() { [ -e "$1" ] && printf '  ok    %s\n' "$2" || fail "missing $1 ($2)"; }
check external/circle-stdlib/libs/circle/lib                      "Circle sources"
check external/circle-stdlib/libs/circle/doc/multicore.txt        "Circle docs"
check external/circle-stdlib/libs/circle-newlib/newlib            "circle-newlib"
check external/circle-stdlib/configure                            "circle-stdlib configure"
check external/macemu/BasiliskII/src/uae_cpu_2021/newcpu.cpp      "macemu uae_cpu_2021"
check external/macemu/BasiliskII/src/dummy                        "macemu dummy stubs"
check external/macemu-jit/BasiliskII/src/uae_cpu_2026/newcpu.cpp  "macemu-jit uae_cpu_2026"
check external/circle-stdlib/libs/circle/addon/wlan/hostap/wpa_supplicant "wpa_supplicant (WiFi)"

say "Pinned"
printf '  circle-stdlib  %s  %s\n' \
    "$(git -C external/circle-stdlib rev-parse --short HEAD)" \
    "$(git -C external/circle-stdlib describe --tags --exact-match 2>/dev/null || echo -)"
printf '  circle         %s  %s\n' \
    "$(git -C external/circle-stdlib/libs/circle rev-parse --short HEAD)" \
    "$(git -C external/circle-stdlib/libs/circle describe --tags --exact-match 2>/dev/null || echo -)"
printf '  macemu         %s  %s\n' \
    "$(git -C external/macemu rev-parse --short HEAD)" \
    "$(git -C external/macemu log -1 --format=%cd --date=short)"

say "Done — commit the pins."
