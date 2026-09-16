#!/usr/bin/env bash
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
# Generate the UAE 68k opcode tables.
#
# Two-stage cross build: build68k and gencpu are compiled for THIS machine
# (macOS), run here, and emit C++ that is then compiled for AArch64. They are
# host tools, never part of the kernel.
#
#   table68k --build68k--> cpudefs.cpp --gencpu--> cpuemu.cpp cpustbl.cpp
#                                                  cpufunctbl.cpp cputbl.h
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UAE="${REPO_ROOT}/external/macemu/BasiliskII/src/uae_cpu_2021"
SRC="${REPO_ROOT}/external/macemu/BasiliskII/src"
TOOLS="${REPO_ROOT}/build/host-tools"
OUT="${REPO_ROOT}/build/generated"

mkdir -p "$TOOLS" "$OUT"

# The host tools need a config.h describing THIS machine, not the Pi.
cat > "${TOOLS}/config.h" <<'ENDCONFIG'
/* Host-side config.h for build68k and gencpu only. Describes the build machine
   (macOS on arm64), never the target. */
#ifndef OKAPIA_HOST_CONFIG_H
#define OKAPIA_HOST_CONFIG_H
#define SIZEOF_SHORT       2
#define SIZEOF_INT         4
#define SIZEOF_LONG        8
#define SIZEOF_LONG_LONG   8
#define SIZEOF_VOID_P      8
#define SIZEOF_FLOAT       4
#define SIZEOF_DOUBLE      8
#define HAVE_UNISTD_H      1
#define HAVE_FCNTL_H       1
#define HAVE_SYS_STAT_H    1
#define HAVE_SYS_TIME_H    1
#define HAVE_SYS_TYPES_H   1
#define HAVE_STDINT_H      1
#define HAVE_INTTYPES_H    1
#define HAVE_PTHREADS      1
#define VERSION            "0.1"
#endif
ENDCONFIG

HOST_CFLAGS=(-O1 -w -I"$TOOLS" -I"$UAE" -I"${SRC}/include" -I"${SRC}/Unix")

printf '\n==> Building host tools\n'
cc "${HOST_CFLAGS[@]}" -o "${TOOLS}/build68k" "${UAE}/build68k.c"
echo "  build68k"

# gencpu links against readcpu and the generated cpudefs, so cpudefs comes first.
printf '\n==> Generating cpudefs.cpp\n'
(cd "$OUT" && "${TOOLS}/build68k" < "${UAE}/table68k" > cpudefs.cpp)
printf '  %s lines\n' "$(wc -l < "${OUT}/cpudefs.cpp" | tr -d ' ')"

printf '\n==> Building gencpu\n'
c++ "${HOST_CFLAGS[@]}" -std=gnu++17 -o "${TOOLS}/gencpu" \
    "${UAE}/gencpu.c" "${UAE}/readcpu.cpp" "${OUT}/cpudefs.cpp"
echo "  gencpu"

# gencpu writes into the current directory, with fixed file names.
printf '\n==> Generating opcode tables\n'
(cd "$OUT" && "${TOOLS}/gencpu")

printf '\n==> Generated\n'
for f in cpudefs.cpp cpuemu.cpp cpustbl.cpp cpufunctbl.cpp cputbl.h; do
    if [ -f "${OUT}/${f}" ]; then
        printf '  %-16s %8s lines\n' "$f" "$(wc -l < "${OUT}/${f}" | tr -d ' ')"
    else
        printf '  %-16s MISSING\n' "$f"
    fi
done
