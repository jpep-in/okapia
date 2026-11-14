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
#
#   usage: gen-cpu.sh [jit]
#
# With "jit" the tables come from external/macemu-jit's uae_cpu_2026 instead,
# into build/generated-jit, and gencomp adds the compile handlers of the AArch64
# JIT: compemu.cpp compstbl.cpp comptbl.h. The generators read the same defines
# as the kernel does (src/kernel/Makefile, OKAPIA_JIT=1), because what they
# write depends on them.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIT=no
JIT_DEFINES=()
if [ "${1:-}" = jit ]; then
    JIT=yes
    UAE="${REPO_ROOT}/external/macemu-jit/BasiliskII/src/uae_cpu_2026"
    TOOLS="${REPO_ROOT}/build/host-tools-jit"
    OUT="${REPO_ROOT}/build/generated-jit"
    JIT_DEFINES=(-DUSE_JIT -DJIT -DCPU_aarch64 -DCPU_AARCH64 -DUNALIGNED_PROFITABLE
                 -DAARCH64_ASSEMBLY -DOPTIMIZED_FLAGS -DDIRECT_ADDRESSING -DFPU_IEEE)
    # The JIT's own FPU path, which macemu-jit leaves off on AArch64.
    [ "${OKAPIA_JIT_FPU:-0}" = 1 ] && JIT_DEFINES+=(-DUSE_JIT_FPU)
else
    UAE="${REPO_ROOT}/external/macemu/BasiliskII/src/uae_cpu_2021"
    TOOLS="${REPO_ROOT}/build/host-tools"
    OUT="${REPO_ROOT}/build/generated"
fi
# The rest of Basilisk is kanjitalk755's in both cases.
SRC="${REPO_ROOT}/external/macemu/BasiliskII/src"

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

HOST_CFLAGS=(-O1 -w -I"$TOOLS" -I"$UAE" -I"${SRC}/include" -I"${SRC}/Unix" ${JIT_DEFINES[@]+"${JIT_DEFINES[@]}"})

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

GENERATED="cpudefs.cpp cpuemu.cpp cpustbl.cpp cpufunctbl.cpp cputbl.h"
if [ "$JIT" = yes ]; then
    printf '\n==> Building gencomp\n'
    c++ "${HOST_CFLAGS[@]}" -std=gnu++17 -o "${TOOLS}/gencomp" \
        "${UAE}/compiler/gencomp.c" "${UAE}/readcpu.cpp" "${OUT}/cpudefs.cpp"
    echo "  gencomp"
    printf '\n==> Generating the JIT compile handlers\n'
    (cd "$OUT" && "${TOOLS}/gencomp")
    GENERATED="$GENERATED compemu.cpp compstbl.cpp comptbl.h"
fi

printf '\n==> Generated\n'
for f in $GENERATED; do
    if [ -f "${OUT}/${f}" ]; then
        printf '  %-16s %8s lines\n' "$f" "$(wc -l < "${OUT}/${f}" | tr -d ' ')"
    else
        printf '  %-16s MISSING\n' "$f"
    fi
done
