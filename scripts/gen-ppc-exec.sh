#!/usr/bin/env bash
# Generate kpx_cpu's PowerPC execute table.
#
# ppc-execute.cpp is a few thousand lines of templates and almost no code: what
# actually instantiates them is a list produced from the decode table, by a Perl
# script upstream runs at build time.
#
#   ppc-decode.cpp --cpp -DGENEXEC--> a stream of markers
#                  --genexec.pl-----> ppc-execute-impl.cpp
#
# Preprocessed with the *target* compiler and the SheepShaver config, not the
# host's: the list has to match what ppc-execute.cpp will be compiled to need,
# and both sides read the same conditionals.
#
# Same family as gen-cpu.sh, which does the 68k tables for the other engine.
#
# Copyright (C) 2026  Okapia contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
source "${REPO_ROOT}/scripts/env.sh"

SS="${REPO_ROOT}/external/macemu/SheepShaver/src"
STDLIB="${REPO_ROOT}/external/circle-stdlib"
GEN="${REPO_ROOT}/build/generated"
OUT="${GEN}/ppc-execute-impl.cpp"
GXX="${OKAPIA_TOOLCHAIN}/bin/${TOOLCHAIN_PREFIX}g++"
CXXINC="${OKAPIA_TOOLCHAIN}/aarch64-none-elf/include/c++/15.2.1"

mkdir -p "${GEN}"

"${GXX}" -E -DGENEXEC -nostdinc \
    -isystem "${CXXINC}" \
    -isystem "${CXXINC}/aarch64-none-elf" \
    -isystem "${STDLIB}/install/aarch64-none-circle/include" \
    -isystem "${OKAPIA_TOOLCHAIN}/lib/gcc/aarch64-none-elf/15.2.1/include" \
    -DAARCH=64 -mcpu=cortex-a53 -fsigned-char \
    -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -DRASPPI=3 -DSTDLIB_SUPPORT=3 \
    -U__unix__ -U__linux__ -std=c++17 -w \
    -I"${REPO_ROOT}/src/circle/sheepshaver" \
    -I"${SS}/include" -I"${SS}/Unix" -I"${SS}/CrossPlatform" \
    -I"${SS}/kpx_cpu/include" -I"${SS}/kpx_cpu/src" \
    -I"${STDLIB}/include" -I"${STDLIB}/libs/circle/addon" \
    "${SS}/kpx_cpu/src/cpu/ppc/ppc-decode.cpp" \
  | perl "${SS}/kpx_cpu/src/cpu/ppc/genexec.pl" > "${OUT}"

lines="$(wc -l < "${OUT}" | tr -d ' ')"
if [ "${lines}" -lt 100 ]; then
    echo "ppc-execute-impl.cpp came out at ${lines} lines, which is not a table." >&2
    exit 1
fi
echo "  GEN   ppc-execute-impl.cpp (${lines} instantiations)"
