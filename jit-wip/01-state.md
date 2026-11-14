# 01 — State: what exists, where, and what works

## Architecture in one paragraph

Okapia is Basilisk II (68k) and SheepShaver (PowerPC) on Circle (bare-metal Raspberry Pi), both in one kernel image
(`src/kernel/Makefile`, `docs/project/architecture.md`). The 68k CPU core is upstream `uae_cpu_2021` (kanjitalk755's
macemu, a pinned submodule `external/macemu`), an interpreter. With **`OKAPIA_JIT=1`** the Makefile builds
`external/macemu-jit/BasiliskII/src/uae_cpu_2026` instead (the core of `rcarmo/macemu-jit`, pinned at
`ba4a541f7afb554069e7401841a48bbbf71004b2`, 2026-09-06), with its AArch64 JIT, and only the core changes: Basilisk,
its ROM patches and the whole platform layer stay kanjitalk755's and Okapia's. The JIT's platform needs (memory, time,
interrupts) are provided by `src/circle/jit_circle.cpp`.

## What was added or changed, by file

JIT work only. `git status` also shows older uncommitted work (network, Wi-Fi, firmware, boot menu, clock) that is **not**
part of this: the docs `boot-menu`, `clock-and-pram`, `sound`, `guide`, `preferences`, `architecture` (partly), the
`src/firmware/*`, `src/circle/*net*`, `*wlan*`, `clock_circle.*`, `tests/host/*`, `scripts/gen-strings.py`, `netbench.py`.

### New files

| Path | Purpose |
|---|---|
| `external/macemu-jit` (submodule, `.gitmodules`) | the fork, pinned; cloned by `scripts/bootstrap.sh` |
| `patches/macemu-jit/0001-…fpu…` | our binary128 FPU patch (macemu `0008`) retargeted at `uae_cpu_2026` |
| `patches/macemu-jit/0002-…memory…` | our guest-address guard (macemu `0007`) retargeted |
| `patches/macemu-jit/0003-compemu_support_arm-warm-a-ram-block-up-once-then-compile-it.patch` | **the decisive fix**, see 03 |
| `patches/macemu-jit/0004-compemu_support-a-guest-flush-revalidates-only-its-range.patch` | range-checked guest flush; correct, no measurable effect |
| `src/circle/jit_circle.cpp`, `jit_circle.h` | the JIT's platform: arena (`vm_acquire`/`vm_release`/`vm_protect`), `GetTicks_usec`, `jit_one_tick`, `TimerPoll`, `usleep` for the 68k STOP path, `JitPrefsApply`, the 5-second log line |
| `src/circle/jit-compat/sys/mman.h`, `SDL2/SDL.h` | empty stand-ins for two headers the fork includes unguarded |
| `src/circle/execmem_circle.cpp/.h` | `ExecMemMakeExecutable()` (clears PXN on 64 KB pages, flushes the TLB) and the `EXECMEM_SELFTEST` self-test |
| `docs/notes/research/emu68.md` | the Emu68 reading |
| `scripts/jit-gate-pi.sh` | **obsolete**: a script to build and run macemu-jit on Raspberry Pi OS (Linux) for a side measurement; the user declined that path and the JIT was integrated into Okapia instead |
| `reference/emu68` (cloned, ignored by git as `reference/*`) | study material |

### Modified files

| Path | Change |
|---|---|
| `scripts/build-libs.sh` | `KERNEL_MAX_SIZE_MB=16` (was circle-stdlib's 4), included in the library stamp |
| `scripts/bootstrap.sh` | pins and clones `external/macemu-jit`, checks it |
| `scripts/gen-cpu.sh` | `gen-cpu.sh jit` generates `build/generated-jit` from `uae_cpu_2026` and runs `gencomp` (compile handlers); `OKAPIA_JIT_FPU=1` adds `-DUSE_JIT_FPU` |
| `scripts/make-pi-sd.sh` | optional argument: a JIT kernel to stage next to the interpreter one, `kernel=` line commented in `config.txt`, `jit true` added to the staged prefs |
| `scripts/run-test.sh` | `OKAPIA_EXTRA_PREFS='jit true\n…'` appends preferences to the throwaway card |
| `scripts/fetch-reference.sh` | `emu68` in the catalogue |
| `src/kernel/Makefile` | `OKAPIA_JIT=1` and `OKAPIA_JIT_DIAG`, `OKAPIA_JIT_FPU`, `OKAPIA_JIT_CONSTJUMP`; `UAE`/`CPUGEN` variables; named CPU sources; `_nf` and `compemu` objects as static pattern rules |
| `src/kernel/kernel.cpp` | calls `ExecMemSelfTest()` (trace builds), `JitPrefsApply()` before `InitAll`, `extern spcflags_lock` under `OKAPIA_JIT` |
| `src/circle/main_circle.cpp` | `ConsumeInterruptFlags()`; `FlushCodeCache()` forwards to the JIT and counts requests (`g_nJitFlushRequests`) |
| `src/circle/tick_circle.cpp` | `TickFromEngine()` (the 60 Hz tick asked for by the JIT's dispatch loop); `tick_inhibit` cleared at `TickStart` |
| docs | `decisions.md` (JIT and 16 MB), `build.md`, `engine-68k.md`, `vision.md`, `network.md` (4→16 MB), `jit-68k.md`, `docs/README.md` |

### Build options (all off by default; objects do not know them, `make -C src/kernel okapia-clean` after any change)

| Make variable | Effect |
|---|---|
| `OKAPIA_JIT=1` | builds `uae_cpu_2026` + JIT, defines `USE_JIT JIT CPU_AARCH64 AARCH64_ASSEMBLY OPTIMIZED_FLAGS …`; needs `scripts/gen-cpu.sh jit` first |
| `OKAPIA_JIT_DIAG=1` | `setenv("B2_JIT_DIAG","1")`: the core prints its counters (`JIT_DIAG …`) on stderr = the serial port |
| `OKAPIA_JIT_FPU=1` | compiles `USE_JIT_FPU` and `compemu_fpp.cpp` (run `OKAPIA_JIT_FPU=1 scripts/gen-cpu.sh jit` too). **Unvalidated**: the JIT keeps FP registers as host doubles |
| `OKAPIA_JIT_CONSTJUMP=1` | `setenv("B2_JIT_ENABLE_CONSTJUMP","1")`: follow constant jumps (blocks 3.5 → 4.5 insns); the fork disables it on AArch64 for a known divergence |

Preferences read by the JIT kernel (in `BasiliskII_Prefs`): `jit true`, `jitcachesize <KB>`, `jitlazyflush`, `jitfpu`,
`jitinline` (**inert on AArch64**, see 03). The arena is `jitcachesize` + 256 MB of margin for block descriptors.

## What works (verified)

- Compile and link for QEMU (`raspi3b`) and Pi 4; `kernel8.img` 3.4 MB (interpreter) and 5.4–5.6 MB (JIT).
- System 7.1 under QEMU: Finder, three volumes, idle at 11–14 s with the fixes (interpreter 8.7 s).
- System 7.6.1 on the Pi 4: Finder, Speedometer, about 5 minutes of use, clean **Shut Down** (`68k execution ended`,
  `Stopped: the power can be switched off`). The cache flush after writing generated code is sufficient on a Cortex-A72
  (`ExecMemSelfTest` PASS under QEMU; the real proof is the JIT running on the board).
- `scripts/run-test.sh 60` (SIGKILL, scavenge, fsck): **same verdict with and without the JIT**, same bytes written
  (36,905 interpreter, 36,915 JIT).
- Host tests: `make -C tests/host` green (checked once at the start; not rerun after every change).

## What does not, or is not known

- **Speed**: no gain (see 02). K'Whet ten times slower, **unexplained**.
- **Pauses**: the user feels momentary freezes with the JIT (frame gaps up to 94 ms); consistent with the compiler working
  in bursts when new code runs; not measured further.
- **Not tested**: Mac OS 8, applications beyond Speedometer and the Finder, restarts (the Macintosh going round more than
  once with the JIT: `compiler_exit`, `vm_release`'s arena reset, `tick_inhibit`), SheepShaver (untouched), the
  `OKAPIA_JIT_FPU` path for correctness, a boot with `OKAPIA_JIT_CONSTJUMP` beyond System 7.1 and 7.6.1.
- **Noise**: the fork prints `RTE_BAD[n]` (first 64 returns above `0x08000000`), `M68K_SETPC_BAD[n]` (64 lines) and, with the
  JIT off, `NOJIT_DIAG` every 5 M instructions, keyed to *its* memory map (ROM at `0x01000000`); Okapia's ROM is at
  `0x10000000`. Harmless, interleaved with the logger on the serial port. A patch to silence them is owed.
- **Patches 0009 and 0010** of `patches/macemu` (the interpreter's per-instruction savings in `uae_cpu_2021`) are **not**
  carried to `uae_cpu_2026`: 0009 fails at `newcpu.cpp:1507`, the JIT core rewrote that code.
- **The 5-second line "k opcodes"** of `cpu_ticks_circle.cpp` counts seam visits × quantum with the JIT, not instructions: meaningless there.
- The tree is currently on the **Pi 4 target** with the **interpreter** built; switching back to QEMU is
  `./scripts/build-libs.sh qemu` (a rename if that target was built once).
