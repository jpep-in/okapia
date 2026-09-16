#!/usr/bin/env bash
# Does an emulator core compile for the board, before anything is built around it?
#   usage: check-engine.sh [sheepshaver]
#
# Phase 20 starts by adding a second engine, and the first question is whether
# its core survives AArch64 and circle-stdlib at all. Answering it needs no
# kernel, no platform layer and no Makefile — only the compiler and a config.h
# — so it is answered here, and kept answered.
#
# The sweep found no portability problem whatsoever on 2026-09-05: all 29 files
# compile unchanged. Two things had to be said in src/circle/sheepshaver/config.h
# and nothing else. If that ever stops being true, this says which file.
#
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
source "${REPO_ROOT}/scripts/env.sh"

# The core reads vm.hpp, and our two conditions are in it. Compiling against an
# unpatched tree would succeed and mean nothing: VMBaseDiff would silently
# become a constant again.
if ! "${REPO_ROOT}/scripts/apply-patches.sh" --check >/dev/null 2>&1; then
    echo "Patches are not applied; run scripts/apply-patches.sh first." >&2
    exit 1
fi

ENGINE="${1:-sheepshaver}"
if [ "${ENGINE}" != "sheepshaver" ]; then
    echo "usage: check-engine.sh [sheepshaver]" >&2
    echo "Basilisk is checked by building the kernel; it has one." >&2
    exit 2
fi

SS="${REPO_ROOT}/external/macemu/SheepShaver/src"
STDLIB="${REPO_ROOT}/external/circle-stdlib"
GXX="${OKAPIA_TOOLCHAIN}/bin/${TOOLCHAIN_PREFIX}g++"
CXXINC="${OKAPIA_TOOLCHAIN}/aarch64-none-elf/include/c++/15.2.1"

# The same flags the kernel is built with, minus what only linking needs.
# -nostdinc and the -isystem list are Circle's: the toolchain's own headers are
# not the ones this target has.
FLAGS=(
    -fsyntax-only -nostdinc
    -isystem "${CXXINC}"
    -isystem "${CXXINC}/aarch64-none-elf"
    -isystem "${STDLIB}/install/aarch64-none-circle/include"
    -isystem "${OKAPIA_TOOLCHAIN}/lib/gcc/aarch64-none-elf/15.2.1/include"
    -DAARCH=64 -mcpu=cortex-a53 -fsigned-char
    -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -DRASPPI=3 -DSTDLIB_SUPPORT=3
    -U__unix__ -U__linux__ -O2 -std=c++17 -fexceptions -w
    # This engine's config.h first, so it is found instead of Basilisk's.
    -I"${REPO_ROOT}/src/circle/sheepshaver"
    -I"${SS}/include" -I"${SS}/Unix" -I"${SS}/CrossPlatform"
    -I"${SS}/kpx_cpu/include" -I"${SS}/kpx_cpu/src"
    -I"${STDLIB}/include" -I"${STDLIB}/libs/circle/addon"
)

# The core, as Unix/Makefile.in lists it, minus the files that are a host's
# rather than the emulator's — and kpx_cpu in interpreted form, which is the
# list configure.ac:1580-1587 builds when there is no JIT for the host.
SOURCES=(
    main prefs prefs_items rom_patches rsrc_patches emul_op name_registry
    macos_util thunks user_strings video gfxaccel serial ether
    adb audio cdrom disk scsi sony timer xpram extfs
    kpx_cpu/src/mathlib/ieeefp kpx_cpu/src/mathlib/mathlib
    kpx_cpu/src/cpu/ppc/ppc-cpu kpx_cpu/src/cpu/ppc/ppc-decode
    kpx_cpu/src/cpu/ppc/ppc-execute kpx_cpu/src/cpu/ppc/ppc-translate
    kpx_cpu/src/utils/utils-cpuinfo kpx_cpu/sheepshaver_glue
)

# ppc-execute.cpp includes a table generated from the decode file, so it has to
# exist before anything asks whether that file compiles.
if [ ! -s "${REPO_ROOT}/build/generated/ppc-execute-impl.cpp" ]; then
    "${REPO_ROOT}/scripts/gen-ppc-exec.sh"
fi
FLAGS+=(-I"${REPO_ROOT}/build/generated")

# And our own half, which is the part that will actually be wrong. It needs the
# platform's include paths on top of the engine's.
OURS=(
    "${REPO_ROOT}/src/circle/sheepshaver/mac_layout.cpp"
    "${REPO_ROOT}/src/circle/sheepshaver/main_circle.cpp"
    "${REPO_ROOT}/src/circle/sheepshaver/video_circle.cpp"
)
OUR_FLAGS=(
    -I"${REPO_ROOT}/src/circle" -I"${REPO_ROOT}/src/circle/compat"
    -I"${REPO_ROOT}/src/firmware/circle"
    -isystem "${STDLIB}/libs/circle/include"
    -isystem "${STDLIB}/libs/circle/addon"
    -DAARCH=64 -DRASPPI=3 -D__circle__=510000 -DSTDLIB_SUPPORT=3
)

echo "Compiling the ${ENGINE} core for AArch64, ${#SOURCES[@]} files."
failed=0
for name in "${SOURCES[@]}"; do
    if ! err="$("${GXX}" "${FLAGS[@]}" "${SS}/${name}.cpp" 2>&1)"; then
        printf '  FAIL  %-28s %s\n' "${name}" \
               "$(printf '%s' "${err}" | grep -m1 'error:' | sed 's|.*/||')"
        failed=$((failed + 1))
    fi
done

for path in "${OURS[@]}"; do
    if ! err="$("${GXX}" "${FLAGS[@]}" "${OUR_FLAGS[@]}" "${path}" 2>&1)"; then
        printf '  FAIL  %-28s %s\n' "$(basename "${path}")" \
               "$(printf '%s' "${err}" | grep -m1 'error:' | sed 's|.*/||')"
        failed=$((failed + 1))
    fi
done

if [ "${failed}" -eq 0 ]; then
    echo "All $(( ${#SOURCES[@]} + ${#OURS[@]} )) compile, ours included."
else
    echo "${failed} of $(( ${#SOURCES[@]} + ${#OURS[@]} )) do not compile."
fi
exit $(( failed == 0 ? 0 : 1 ))
