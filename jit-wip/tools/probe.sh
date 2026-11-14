#!/usr/bin/env bash
# HISTORICAL (2026-11-13): the probe that sized route A before it was integrated. The real build is
# now `make -C src/kernel OKAPIA_JIT=1` (see jit-wip/04-howto.md). Kept because it needs no kernel,
# no Circle libraries beyond the include paths, and takes 17 s: a quick "does this core still
# compile with our flags" check. It reads reference/macemu-jit, which is at the same commit as
# external/macemu-jit but WITHOUT patches/macemu-jit applied. Output goes to jit-wip/tools/probe/
# (git-ignored by hand: delete it afterwards).
# Compile probe: does macemu-jit's uae_cpu_2026 with its AArch64 JIT compile
# against Okapia's bare-metal configuration, and how big is it?
# Nothing is linked and nothing in the repository is written.
set -uo pipefail

REPO=/Users/john/git-repositories/mac68k-rpi-circle
REF="${REPO}/reference/macemu-jit/BasiliskII/src"
UAE="${REF}/uae_cpu_2026"
P="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/probe"
TOOLS="${P}/host"; GEN="${P}/gen"; OBJ="${P}/obj"; LOG="${P}/log"
mkdir -p "$TOOLS" "$GEN" "$OBJ" "$LOG"

JITDEFS=(-DUSE_JIT -DJIT -DCPU_aarch64 -DCPU_AARCH64 -DUNALIGNED_PROFITABLE -DAARCH64_ASSEMBLY -DOPTIMIZED_FLAGS -DDIRECT_ADDRESSING -DFPU_IEEE)

stage="${1:-all}"

if [ "$stage" = host ] || [ "$stage" = all ]; then
  cp "${REPO}/build/host-tools/config.h" "${TOOLS}/config.h"
  HOST=(-O1 -w -I"$TOOLS" -I"$UAE" -I"${REF}/include" -I"${REF}/Unix" "${JITDEFS[@]}")
  cc "${HOST[@]}" -o "${TOOLS}/build68k" "${UAE}/build68k.c" 2> "${LOG}/build68k.log" || { echo "build68k FAILED"; tail -5 "${LOG}/build68k.log"; exit 1; }
  (cd "$GEN" && "${TOOLS}/build68k" < "${UAE}/table68k" > cpudefs.cpp)
  c++ "${HOST[@]}" -std=gnu++17 -o "${TOOLS}/gencpu" "${UAE}/gencpu.c" "${UAE}/readcpu.cpp" "${GEN}/cpudefs.cpp" 2> "${LOG}/gencpu.log" || { echo "gencpu FAILED"; grep -m8 error: "${LOG}/gencpu.log"; exit 1; }
  (cd "$GEN" && "${TOOLS}/gencpu") || { echo "gencpu run FAILED"; exit 1; }
  c++ "${HOST[@]}" -std=gnu++17 -o "${TOOLS}/gencomp" "${UAE}/compiler/gencomp.c" "${UAE}/readcpu.cpp" "${GEN}/cpudefs.cpp" 2> "${LOG}/gencomp.log" || { echo "gencomp FAILED"; grep -m8 error: "${LOG}/gencomp.log"; exit 1; }
  (cd "$GEN" && "${TOOLS}/gencomp") || { echo "gencomp run FAILED"; exit 1; }
  wc -l "$GEN"/*.cpp "$GEN"/*.h | sed "s#${GEN}/##"
fi

if [ "$stage" = cross ] || [ "$stage" = all ]; then
  . "${REPO}/scripts/env.sh"
  TC="${OKAPIA_TOOLCHAIN}"
  CS="${REPO}/external/circle-stdlib"
  CXXF=(-nostdinc
    -isystem "${TC}/aarch64-none-elf/include/c++/15.2.1"
    -isystem "${TC}/aarch64-none-elf/include/c++/15.2.1/aarch64-none-elf"
    -isystem "${TC}/aarch64-none-elf/include/c++/15.2.1/backward"
    -isystem "${CS}/install/aarch64-none-circle/include"
    -isystem "${TC}/lib/gcc/aarch64-none-elf/15.2.1/include"
    -DAARCH=64 -mcpu=cortex-a72 -mlittle-endian -fsigned-char
    -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -D__circle__=510101 -DRASPPI=4 -DSTDLIB_SUPPORT=3
    -U__unix__ -U__linux__ -DM68K_NO_FAULT_PC "${JITDEFS[@]}"
    -I"${P}/shim" -I"${REPO}/src/circle" -I"${REPO}/src/circle/compat" -I"$GEN"
    -I"${REPO}/src/firmware" -I"${REPO}/src/firmware/circle" -I"${REPO}/src/circle/circle"
    -I"${REF}/include" -I"$UAE" -I"${UAE}/compiler" -I"${REF}/Unix" -I"${REF}/CrossPlatform"
    -I"${CS}/include" -I"${CS}/libs/circle/include" -I"${CS}/libs/circle/addon"
    -O2 -std=c++17 -fexceptions -Wno-narrowing -Wno-write-strings -w -c)
  cc1() { # name, source, extra flags...
    local name="$1" src="$2"; shift 2
    if aarch64-none-elf-g++ "${CXXF[@]}" "$@" -o "${OBJ}/${name}.o" "$src" 2> "${LOG}/${name}.log"; then
      printf 'ok    %s\n' "$name"
    else
      printf 'FAIL  %-22s %s errors; first: %s\n' "$name" "$(grep -c 'error:' "${LOG}/${name}.log")" \
        "$(grep -m1 'error:' "${LOG}/${name}.log" | sed "s#${REPO}/##" | cut -c1-200)"
    fi
  }
  only="${2:-}"
  run() { if [ -z "$only" ] || [[ "$1" == $only ]]; then cc1 "$@"; fi; }
  run compemu_support "${UAE}/compiler/compemu_support.cpp"
  run compstbl "${GEN}/compstbl.cpp"
  for n in 1 2 3 4 5 6 7 8; do run "compemu$n" "${GEN}/compemu.cpp" "-DPART_$n"; done
  for n in 1 2 3 4 5 6 7 8; do run "cpuemu${n}_nf" "${GEN}/cpuemu.cpp" "-DPART_$n" -DNOFLAGS; done
  run cpustbl_nf "${GEN}/cpustbl.cpp" -DNOFLAGS
  run newcpu "${UAE}/newcpu.cpp"
  run basilisk_glue "${UAE}/basilisk_glue.cpp"
  run memory "${UAE}/memory.cpp"
  ls "$OBJ"/*.o >/dev/null 2>&1 && aarch64-none-elf-size -t "$OBJ"/*.o | sed "s#${OBJ}/##"
fi
