# A 68k JIT for Okapia

> Feasibility study of 2026-08-29, revised on 2026-08-30 after two rounds of cross-checking between two independent
> studies, every objection replayed against the code. Sources: Circle, Basilisk II (`uae_cpu_2021`) and
> `reference/macemu-jit` (HEAD `3e79929` then, `ba4a541` since 2026-11-13). The decision taken then was **no JIT**
> — see [Decisions](../../project/decisions.md). Facts that changed since are marked *(since)*.
>
> **Surveyed again on 2026-11-13** across the 68k emulators that have a JIT ([below](#survey-of-2026-11-13)): the
> question is **reopened for measurement**. The August verdict rested on one fork's strict-mode figure, which says
> nothing about the backend that fork imported. The decision page is unchanged until the
> [gate](#the-measurement-that-decides) is run.

## Verdict

The JIT does not stumble on Circle: the non-executable heap can be lifted from our own code, and the feared platform
dependencies (`mprotect`, `SIGSEGV`, `BRK` single-stepping) do not exist in normal operation. Three real obstacles
remain:

1. **The backend.** *(Revised 2026-11-13.)* kanjitalk755's tree has no AArch64 backend, but the UAE family has one,
   in production: TomB's UAE4ARM backend, in Amiberry and now in WinUAE's own tree. `rcarmo/macemu-jit` is the only
   port of it onto Basilisk; it is correct, and the only end-to-end timing its authors publish — **strict mode**,
   an instrumented validation mode — is about twice as slow as the interpreter. That measures the port's
   instrumentation, not the backend; no ordinary-mode figure exists.
2. **The kernel address budget.** The JIT adds ~4.5–5 MB of static tables in `.bss`, plus the code arena, against a
   4 MB `KERNEL_MAX_SIZE` checked by a silent halt at startup.
3. **Allocation in the loop.** The JIT runtime allocates during emulation, which the resource budget forbids, and no
   configuration changes that.

Obstacles 2 and 3 are engineering with a known shape (tables and pools carved from the first allocation). Obstacle 1
is a question nobody has measured comparably, in either direction.

**Recommendation** *(revised 2026-11-13)*: run the [gate](#the-measurement-that-decides) on our Pi 4 under Linux —
same ROM, System and Speedometer as [Benchmarks](../../contributing/benchmarks.md) — before any Circle work; do the
executable-memory spot check in parallel, since every route needs it. Then choose between routes A, D and F
([Routes](#routes)). The interpreter stays the default engine whatever the outcome: rev2 already gives 5.22× a
Quadra 650 on CPU. *(pom68k's AArch64 JIT is fast because it buys back an MMU and cycle exactness that Basilisk does
not pay for, and it cannot be grafted onto `uae_cpu_2021` — see [pom68k](pom68k.md).)*

## Survey of 2026-11-13

GitHub searched through its public API and the trees listed read by directory and by the files quoted; nothing was
cloned. Sizes are bytes of source. Deep reading needs a clone — **ask first**
(`scripts/fetch-reference.sh` knows `amiberry`, `macemu-jit` and `pom68k`).

### Who has a 68k JIT

| Project | Host code | Licence | State | For us |
|---|---|---|---|---|
| Basilisk II, `kanjitalk755/macemu` (`uae_cpu`, `uae_cpu_2021`) | x86, x86-64 | GPLv2+ | Maintained; README: arm64 non-JIT | The JIT "on other Basilisk ports" is this one, on PCs and Intel Macs |
| ARAnyM (`src/uae_cpu/compiler/`) | x86, x86-64, **ARM 32-bit** | GPLv2 | `--enable-jit2`, "experimental/ARM only" (`configure.ac:233`) | **The parent of `uae_cpu_2021`** — see below |
| `Chips-fr/uae4arm-rpi` (`src/jit/`) | ARM 32-bit, **AArch64** | GPL (UAE) | Maintained (pushed 2026-09-08) | The AArch64 backend closest to its origin |
| Amiberry (`src/jit/arm/`) | **AArch64**, x86-64 | GPLv3 | Production | Same backend, most exercised |
| WinUAE (`jit/arm/`), FS-UAE (`jit/arm/`) | **AArch64** (WinUAE: `codegen_arm64.cpp`, `compemu_midfunc_arm64*.cpp`; FS-UAE's directory not read) | GPLv2 | In tree | The backend is now the UAE family's, not one fork's |
| `rcarmo/macemu-jit` (`uae_cpu_2026`) | **AArch64** | GPLv2+ | HEAD `ba4a541`, 2026-09-06, one commit since August, none after | The only port onto Basilisk and the only backend with a published proof of correctness — [below](#macemu-jit-since-august) |
| Emu68 (`michalsc/Emu68`) | **AArch64, bare metal** | MPL-2.0 | 1.0 released, active | Proof and model, not a graft — see below |
| `MW0MWZ/PreviousTurbo` (`src/cpu/jit040*`) | x86-64, **AArch64** | GPLv2+ | Created 2026-10-22, about a dozen commits, no figures | A small tiered JIT on a UAE core — the shape of route F |
| pom68k | x86-64, **AArch64** | GPLv3 | Active | Bound to Moira and cycle exactness ([pom68k](pom68k.md)) |

No JIT: `jhonnaker1/bmmacintosh` (Basilisk II and SheepShaver on Circle, like us), `pokorino/BasiliskII-ESP32-S31`,
M5Tab-Macintosh, Infinite Mac, Mini vMac, Snow, Musashi, Moira. Not usable: QEMU's TCG (a whole framework, and
its licence is to be checked against GPLv3 before anything else); ARDI's syn68k (last pushed 2010).

### One family, and where our core sits in it

Every UAE-derived JIT descends from Bernd Meyer's: `gencomp` generates one compile handler per opcode, which calls
*midfuncs*, which call a raw emitter. Two generations of midfunc exist. The x86 one (`or_b`, `mov_b_rr`) mirrors
two-operand x86. The ARM one — ARAnyM's "JIT2" — is per 68k instruction, with flags (`jff_ADD_l`) and without
(`jnf_ADD`), and needs its own generator, `gencomp_arm.c`. UAE4ARM took that second generation and added AArch64.

Measured on the declared midfuncs (`compemu_midfunc_arm2.h`, `DECLARE_MIDFUNC`):

| | Declared | Shared |
|---|---:|---|
| ARAnyM | 190 | 169 with uae4arm-rpi |
| uae4arm-rpi | 297 | 297 with Amiberry |
| Amiberry | 298 | |

`uae_cpu_2021` is ARAnyM's core with the ARM files removed, and the sockets left in:

- our `compemu_support.cpp` differs from ARAnyM's in 126 lines out of some 5,000;
- it still tests `CPU_arm` in 13 places, and still says `#include "codegen_arm.cpp"` (`:1168`) and
  `#include "compemu_midfunc_arm2.cpp"` under `USE_JIT2` (`:2672`); `m68k.h:449` keeps the ARM flag layout;
- the files those lines name — `codegen_arm.*`, `compemu_midfunc_arm*.{cpp,h}`, `gencomp_arm.c`, `flags_arm.h` —
  are in ARAnyM today.

This explains route B's failure: `macemu-jit` imported Amiberry's second-generation midfuncs under the
first-generation generator ("calls legacy midfunc API (`start_needflags`, `or_b`, `mov_b_rr`, etc.) not provided by
imported Amiberry ARM midfunc layer", `AMIBERRY_ARM_JIT_PORT_PLAN.md`). The generator that speaks the second
generation and fits our core is ARAnyM's. Unknowns, all checkable by reading: ARAnyM's ARM path is 32-bit
(host pointers in 32-bit registers), marked experimental, and 21 of its midfuncs have no namesake in uae4arm-rpi.

### `macemu-jit` since August

One commit, `ba4a541` (2026-09-06), documented in `BasiliskII/docs/AARCH64_JIT_IMPLEMENTATION_REVIEW_20260906.md`.
It is correctness work — "No new boot, Finder responsiveness or performance claim is made" — and it matters more
than its size, because two of the defects it fixes are not the fork's own:

| Fixed in `macemu-jit` | Where else it still is |
|---|---|
| **Stale native code accepted after a permutation of instructions**: the block checksum is a sum and an XOR of words, both blind to order. Interpreter `D0=1`, cached native code `D0=2` | The same two lines, `k1 += *pos; k2 ^= *pos`, in our `uae_cpu_2021/compiler/compemu_support.cpp:3772-3773` and in Amiberry's `compemu_support_arm.cpp` |
| **No-flags constant folds wider than the guest operand** (`jnf_LSL_b_imm`, `jnf_LSL_w_imm`, `jnf_LSL_l_imm`, `jnf_ROL_l_imm`, `jnf_ROR_l_imm`): the folded value is host-width because `PC_P` holds a host pointer. 1,846 failures in 7,488 checks before the fix | Amiberry today: `uintptr val` (`compemu_arm.h:149`) and the unfixed bodies of `jnf_LSL_l_imm` and `jnf_ROL_l_imm`; uae4arm-rpi has the same bodies. Whether a guest reaches them there was not tested |
| A span of unchecked source counted as checked | Not shown reachable by a guest |
| The harness recorded the exit status of a negation, so a failed emulator read as 0 | Their own; fixed, and the gates rerun |

Gates after the fix: 904/904 vectors, 48 emitter and boundary suites, 33/33 allocator-pressure cells, 7,488/7,488
constant folds under UBSan, 8,237/8,237 checksum cases under ASan and UBSan, ordinary and strict.

What follows for us:

- **The audited backend is `macemu-jit`'s, not Amiberry's.** Taking the midfuncs from Amiberry or uae4arm-rpi means
  finding these defects again. Any route in the UAE family should take the AArch64 midfuncs and emitter from
  `macemu-jit`, whatever generator and core sit above them.
- **The checksum defect is the stale-code family** that [Data safety](../../topics/storage.md) cares about, and it
  bites only where a checksum decides to reuse a block: lazy invalidation. Basilisk's own default is
  `jitlazyflush` true (`prefs_items.cpp:123`); ours must be false.
- **Limits its authors state themselves**: all runs on one Orange Pi 6 Plus; the reviewer "did not build or run
  emulators" and was a session of "the same model family"; QA runs `fpu false`. No release carries this JIT — the
  last arm64 package is `v1.0.4` of 2025-12-30 — so the gate builds from source.
### Route A sized on Circle

`reference/macemu-jit` moved to `ba4a541`, and a **compile probe** run on it (2026-11-13, outside the repository,
nothing linked): its generators built for the host, then `uae_cpu_2026` and the generated JIT compiled with our
cross toolchain, our `sysdeps.h` and `config.h`, and the flags `src/kernel/Makefile` gives the 68k engine.

**It compiles as it is.** Twenty-two units, no error, after two additions:

- the defines its `configure.ac:1761-1773` and `:1809` would set: `-DUSE_JIT -DJIT -DCPU_aarch64 -DCPU_AARCH64
  -DUNALIGNED_PROFITABLE -DAARCH64_ASSEMBLY -DOPTIMIZED_FLAGS`;
- two stand-in headers, `sys/mman.h` and `SDL2/SDL.h`, included unguarded (`compemu_support_arm.cpp:35`, `:65`).
  Neither `mmap` nor `SDL_Quit` is referenced once `__linux__` is undefined, as it is in our build.

| Objects (`-O2`, Cortex-A72) | text + rodata | bss |
|---|---:|---:|
| `compemu_support.o` (support, emitter, midfuncs) | 315,990 | 4,856,944 |
| `compemu1.o` … `compemu8.o` (generated compile handlers) | 722,056 | 0 |
| `compstbl.o` | 59,780 | 0 |
| `cpuemu1_nf.o` … `cpuemu8_nf.o` (interpreter handlers without flags, which the JIT requires) | 462,773 | 448 |
| `cpustbl_nf.o` | 29,892 | 0 |
| **Added by the JIT** | **1,590,491** | **4.86 MB** |

The bss is `cache_tags` 2,097,152; `baseaddr`, `mem_banks`, `compfunctbl`, `nfcompfunctbl` 524,288 each; `prop` and
`raw_cputbl_count` 262,144 each; the strict-mode slots 131,072 and a trace ring 114,688. So `kernel8.img` goes
from 3.48 MB to about 5.1 MB and `_end` past 10 MB: **the 4 MB reservation cannot hold it**, tables moved or not.

**What the link would still ask for**, beyond the C library and what our 68k engine already defines — eight
functions:

| Symbol | What it is | Ours |
|---|---|---|
| `vm_acquire`, `vm_protect`, `vm_release` | code cache and block-descriptor pools (`LazyBlockAllocator`, `compemu_support_arm.cpp:3073`) | a bump allocator in an arena claimed at startup, with `PXN` cleared; exhaustion flushes |
| `jit_one_tick`, `GetTicks_usec`, `TimerPoll` | the 60 Hz tick and the Time Manager, driven **from the dispatcher** (`compemu_support.cpp:98-175`) | the seam `cpu_do_check_ticks()` already is |
| `ConsumeInterruptFlags`, `TimerRestoreAsyncOwnership` | interrupt flags read and cleared at once; the tick thread handed back | a read-and-clear; nothing — there is no thread |

The tick model suits us better than upstream's: "inhibit the async tick thread and drive `one_tick()` from this
dispatch loop", because generated code "holds live register/flag state". That is our single loop.

**What does not come over, and what that leaves unproven:**

- **The fork's `rom_patches.cpp`.** It adds patches that work around JIT divergences ("Under JIT, earlier init gaps
  cause the wrong selector"; "may return D0!=0 under JIT (trap table not fully initialized)"), and it *removes*
  upstream's patch that stops the ROM reading the model ID at `0x5ffffffc`, relying instead on a read-only page
  and a skipped `SIGSEGV` (`main_unix.cpp:992`). A skipped fault does not exist on Circle. We keep our own patch
  set — so the fork's boot evidence is evidence for another ROM patch set than ours. Its opcode-level proof
  (vectors, emitters, pressure cells) does not depend on this and carries over.
- **Its fixed Linux memory map** (RAM at host `0x10000000`, I/O and NuBus ranges mapped, ASLR switched off).
  Generated code assumes only that `MEMBaseDiff` is below 4 GB and not zero (`codegen_arm64.h:428`), which our
  first allocation satisfies on every board we target; and that the code cache is not inside the guest's reach
  (`compemu_support_arm.cpp:6233-6238`) — below `RAMBaseHost`, as planned.
- **`timer.cpp` and `emul_op.cpp`** changed with the tick model (+115/−152 and +230/−20); ours are the platform's.
- **Our patches on the same files**: `0007` (`memory.h`), `0008` (binary128 FPU: `fpu_ieee.cpp`, `mathlib.h`,
  `types.h`), `0009` (`newcpu.cpp`, `newcpu.h`, `spcflags.h`), `0010` (`newcpu.h`) are to be carried onto
  `uae_cpu_2026`, which changed every one of those files. With no JIT FPU on AArch64, floating point stays in the
  interpreter's core, so `0008` still decides the arithmetic; the hand-over of FP registers between JIT and core
  exists only under `FPU_MPFR` ([FPU](#fpu)) and is the open point.
- **`OPTIMIZED_FLAGS`** changes the interpreter too: flags live in the host's NZCV layout
  ([Bottlenecks](bottlenecks.md), rank 10). It has to be re-measured and re-proven on its own.
- **118 environment switches** (`getenv`) and their diagnostics, some of which allocate. Under newlib they all read
  as unset, which is ordinary mode: strict is opt-in (`B2_JIT_STRICT_FULL`, `compemu_support_arm.cpp:298`).

In short: the build is not the cost. The costs are the reservation, the arena and its exhaustion policy, the four
patches to carry, and proving on our own ROM patch set what the fork proved on its own.

### First boot, 2026-11-14

The decision of the day: the Pi 4 gate was skipped and route A built straight into Okapia, and the 4 MB kernel
reservation was dropped for 16 MB ([Decisions](../../project/decisions.md)). Built with
`OKAPIA_JIT=1` (`src/kernel/Makefile`), after `scripts/gen-cpu.sh jit`:

| | Default | `OKAPIA_JIT=1` |
|---|---:|---:|
| `kernel8.img`, QEMU build | 3,348 KB | 5,398 KB |
| `jit true` | not built | boots to the Finder |

What was added, all of it ours and none of it in `external/`:

- `external/macemu-jit`, pinned in `bootstrap.sh`; `patches/macemu-jit/` carries our patches 0007 (guard) and 0008
  (binary128 FPU) as they apply, unchanged. **0009 and 0010 (the interpreter's per-instruction savings) are not carried
  yet**: `0009` fails on `newcpu.cpp:1507`, and the JIT core rewrote that code.
- `src/hosts/circle/jit_circle.cpp`: the eight symbols of [Route A sized](#route-a-sized-on-circle) — a bump arena of 48 MB for
  everything `vm_acquire` hands out, made executable once by `ExecMemMakeExecutable()`; the tick and the seam driven
  from the dispatcher by the clock; `usleep()` for the STOP path, where the core now sleeps; `jitlazyflush` forced
  off and `jitfpu` off. Stand-in `sys/mman.h` and `SDL2/SDL.h` in `src/hosts/circle/jit-compat/`.
- `ConsumeInterruptFlags()` and the guest's cache-flush request in `main_okapia.cpp`; `TickFromEngine()` in
  `tick_okapia.cpp`.

**Proved, under QEMU's `raspi3b`, System 7.1, our own ROM patches and our binary128 FPU:**

- JIT off in a JIT build: Finder, idle at 8.7 s, 3,200 k opcodes/s — the new core's interpreter works.
- `jit true`: Finder with all three volumes, idle at 25 s, 760 to 930 k opcodes/s. That is QEMU's translation of
  generated code, so it says nothing about speed; it says the compiled code is right enough to boot.
- `run-test.sh 60`, hard kill then repair: identical verdict with and without the JIT, *OK*, and 36,915 against 36,905
  bytes written to the volume.

**Not proved:**

- **Speed**, on anything. This is the whole point and it needs the Pi 4 and Speedometer.
- The cache flush on hardware (the [spot check](#executable-memory-is-reachable) on a Pi) — QEMU runs re-emitted code
  without one.
- **Shut Down and a restart** with the JIT on: the Macintosh goes round more than once, and `compiler_exit()`, the
  arena's reset in `vm_release()` and `tick_inhibit` have not been exercised.
- Mac OS 7.6 and 8, floating point under the JIT (`jitfpu` off, so the binary128 core runs it, but no FPU test was
  run), SheepShaver (not involved), `jitcachesize` exhaustion.
- The fork prints diagnostics keyed to its own memory map (`RTE_BAD`, 64 lines, for returns above `0x08000000`, and
  `NOJIT_DIAG` every 5 M instructions with the JIT off): noise here, since Okapia's ROM is at `0x10000000`. A patch
  to silence them is owed, not a defect.

The Pi 4 run of `OKAPIA_JIT=1` is what settles it; the libraries for the Pi 4 need rebuilding first
(`scripts/build-libs.sh pi4`, new reservation).

### The Pi 4 run

Two kernels on one card, which is what makes the comparison one: the interpreter and the JIT differ by nothing but
the core, and the card, the ROM, the volume and the preferences are the same.

```bash
./scripts/build-libs.sh pi4                      # the 16 MB reservation: a full rebuild
make -C src/kernel okapia-clean && ./scripts/gen-cpu.sh jit
make -C src/kernel OKAPIA_JIT=1 && cp src/kernel/kernel8.img /tmp/kernel8-jit.img
make -C src/kernel okapia-clean && make -C src/kernel     # the interpreter, again
./scripts/make-pi-sd.sh /tmp/kernel8-jit.img
```

`config.txt` starts `kernel8-rpi4.img`, the interpreter; uncommenting `kernel=kernel8-jit.img` starts the JIT. Read
the boot on the serial port: the JIT kernel logs `jit: Arena: 48 MB at …, executable`, and `execmem` is not in a
normal build. Order of the run:

1. **The interpreter, five Speedometer runs** (method of [Benchmarks](../../contributing/benchmarks.md)): it must still give
   about 6.8 on CPU, or the card or the board changed.
2. **The JIT, five runs.** Record CPU, Graf, Math, the integer median and the FPU average: the JIT leaves floating
   point to the interpreter, so a FPU score that moves is a bug to look at, not a result.
3. **Reading the Finder**, not the numbers alone: System 7.1 booting, a window opening, the pointer following the
   mouse. A JIT that scores well and stutters is not a gain.
4. **End each session with Special → Shut Down**; the JIT's shutdown has not run on hardware.

Decision rule, unchanged: CPU at least twice the interpreter's makes the JIT worth keeping; between 1× and 2× it
stays experimental and the cost is weighed against the code it adds; under 1× it is closed, and route F or nothing.
First thing to look at if the JIT kernel hangs before the Mac idles: the serial log's last `jit:` line. If it is the
arena, the cache flush is the suspect (`execmem` self-test, trace build, on the board).

#### First result on the board, 2026-11-14

`kernel8-jit.img`, System 7.6.1, hard flush (`jitlazyflush false`, which that kernel forced):

- It boots: the arena is claimed and executable (`jit: Arena: 48 MB at 1ed0000`), so **the cache flush is
  sufficient on a Cortex-A72**; the Finder is up, idle after 49 s, Mac ticks near 60 per second.
- **It is slow**, far under the interpreter's three runs of the same day. Speedometer, JIT against interpreter: CPU
  0.93 against about 6.8, Math 30 against 93, K'Whet 2.5 against 25, FPU average 1.13 against 2.91. Under the 1×
  line of the decision rule, and the FPU, which the JIT does not compile, is as slow as the integers.

Where it could come from, and what has been looked at:

- **A flush storm.** `flush_icache_range(start, size)` of the core ignores its range (`compemu_support.cpp:52-57`) and
  with `jitlazyflush false` every guest cache flush discards *every* translation (`flush_icache_hard`,
  `compemu_support_arm.cpp:7506`). The guest asks for about 700 of them a second while booting (3,515 in 5 s, then
  1,517, then 818; `jit: guest cache flushes` in the log), so the compiler spends its time recompiling what it just
  threw away. The fork's strict mode takes the lazy path instead (`flush_icache_architectural`, `:8949`), which is
  consistent with its own account of a 10.9× gain from "a storm of invalidations removed".
- **Not shown**: that this is the whole of it. Under QEMU, hard and lazy flush boot in the same time (21.4 and
  21.8 s), but QEMU's own translation of the generated code dominates there and cannot arbitrate.
- Okapia's "k opcodes" counter counts visits of the seam with the JIT, not instructions: it means nothing here.

Next, in this order: (1) `jitlazyflush true` on the board, on a **copy** of the volume since a stale block is a stale
write; (2) if that moves the scores, a flush that honours its range — `patches/macemu-jit/`, invalidating only the
blocks that overlap `[start, start + size)`, which keeps the data-safety property of the hard flush for the code
that changed and spares the rest; (3) if neither does, the cost is in the generated code and the fork's
instrumentation, and route A is closed for speed.

#### Result of the Pi 4 runs, 2026-11-14 — route A does not pay as built

Speedometer, System 7.6.1, same card and ROM, JIT kernel (CPU / Graf / Math / K'Whet / FPU average), against the
interpreter's of the same day (about 6.8 / — / 93 / 25 / 2.91):

| JIT configuration | CPU | Graf | Math | K'Whet | FPU |
|---|---:|---:|---:|---:|---:|
| hard flush, cache 16 MB | 0.93 | | 30 | 2.5 | 1.13 |
| lazy flush, cache 16 MB | similar | | | | |
| lazy flush, cache 128 MB, `OKAPIA_JIT_DIAG` | 0.68 | 1.53 | 33.6 | 2.49 | 1.05 |

**Seven to ten times slower than the interpreter whatever the flush policy or the cache size, under the 1× line of
the decision rule.** The last row carries the cost of the core's counters, a few per cent, not seven times.

What the core's own counters (`OKAPIA_JIT_DIAG=1`, `JIT_DIAG` on the serial port) say, over 81 s of a run:

- about 58,000 `compile_block` calls a second with a 16 MB cache, 33,000 with 128 MB, **93 % of them recompilations of a
  block that already existed**, and only 12,000 to 68,000 blocks ever compiled above optimisation level 0;
- 75 % of the dispatcher's entries go through the interpreter inside the JIT (`exec_nostats`);
- the cache was full and flushed 28 times in 81 s at 16 MB, twice in 58 s at 128 MB: the cache size was a real
  defect of this setup, but not the main one;
- the guest asks for about 700 instruction-cache flushes a second while booting and 20 to 70 in steady state, so the
  flush storm is not the main one either.

**Cause found by reading the code, 2026-11-14.** (The hypothesis written here earlier, invalidation by guest writes,
was wrong: `jit_notify_guest_memory_write` returns at once outside strict mode, `compemu_support_arm.cpp:2985`.)
Over 187 s, 220,000 blocks were compiled for the first time and **6.67 million were recompiled when their countdown
expired**, 6.3 million of them again at optimisation level 0. In `compile_block()` a block in RAM gets an
interpreter-dispatch first generation with `bi->count = 9` "and will escalate on the next count expiry"
(`:7751-7756`); but that branch tests only `optlev == 0`, a block whose countdown ran out is still at level 0, and
the escalating branch (`optlev < max_optlev`) is never reached from there. So in ordinary mode **RAM code is never
compiled natively**: it runs through the interpreter inside the JIT, and every ten executions the block is rebuilt,
each rebuild emitting a new stub that fills the cache. Strict mode (`B2_JIT_STRICT_FULL`, opt-in) compiles RAM blocks
at the top level directly, which is the path the fork's gates and figures are about: it never published an ordinary-mode
number because there was nothing native in it to measure. The reuse of compiled blocks is wired the standard way
(cache tags by host PC, `cache_miss`, validation by checksum); what defeated it was this one policy.

Patch `patches/macemu-jit/0003`: warm a RAM block up once (`optlev == 0 && bi_was_invalid`), then take the escalation.
Under QEMU, with the counters, the same boot goes from millions of recompilations to 9,500, idle at 12.2 s instead
of 21–25 s, 16,000 native blocks against 28,000 interpreted, and the durability verdict is unchanged. The Pi 4 run
of this patch is what decides; nothing about speed is established until then.

#### After the escalation fix, 2026-11-14: parity, not a gain

Pi 4, System 7.6.1, cache 64 MB, lazy flush, no counters (Speedometer CPU / Graf / Math / K'Whet / FPU):

| | CPU | Graf | Math | K'Whet | FPU |
|---|---:|---:|---:|---:|---:|
| interpreter, same day | 6.8 | 3.18 | 93 | 25 | 2.91 |
| JIT, patch 0003, cache 128 MB | 6.40 | 2.83 | 92 | 2.43 | 2.37 |
| JIT, patch 0003, cache 64 MB | 6.17 | 2.58 | 75 | 2.35 | 2.34 |

The first run crashed after 230 s (`Could not allocate block pool!`: the 32 MB margin of the arena held the block
descriptors, which grow once blocks are kept); with 256 MB of margin the arena settles near 100 MB of 320. The second
felt laggy, with frame gaps up to 94 ms: the compiler works in bursts when new code runs.

Where the time is not, measured under QEMU on the boot (counters and probes, none kept):

- **Not the cache flush as a range.** Patch `0004` (a guest flush revalidates only the blocks overlapping its range,
  for `FlushCodeCache()` and the line and page forms of `CINV`/`CPUSH`) is correct and kept, but changes nothing
  here: Mac OS executes **only `CPUSHA`**, 8,000 of 8,000 (`F4F8`), a whole-cache flush that cannot be narrowed from the
  instruction. Each marks about 118 active blocks "needs checking", and 99.8 % of the `check_checksum` entries find a
  block in that state — but that is about 30,000 entries a second against some 870,000 dispatcher entries a second
  measured on the board before the fix: a share of the count, not of the time.
- **Not the reuse of blocks**, which is wired in the usual way and now works.

What is left, and is structural: blocks of **3.5 instructions on average**; successors reached through the validated
handler, never by a direct jump (`jit_prefer_validated_successor_handler`, default on, `compemu_support_arm.cpp:1746`,
"contract-first"), so a dispatch every three or four instructions; every FPU instruction ends its block and goes through
the interpreter (`jitfpu` off: K'Whet ten times slower, the FPU average lower); and a compiler that costs
microseconds per block (the frame gaps). Against that, the interpreter of `uae_cpu_2021` is already about as fast as
the generated code is. The fork's own figure for the Finder, 2× slower than its interpreter in strict mode, and its
SheepShaver JIT 1.9× slower, say the same thing from another side.

What has not been tried, each a switch or a small patch: `jitinline true` (the card's preferences say false, the fork's
default is true: longer blocks), direct successor chaining (`B2_JIT_PREFER_DIRECT_SUCCESSOR_HANDLER`, which the fork
marks as an experiment whose state transfer is "still being normalized": a correctness risk, on a copy of the
volume), and a JIT FPU (not built on AArch64: `compemu_fpp` is dropped there). Without them the JIT is at parity with
the interpreter and worse for latency, so `OKAPIA_JIT=1` stays off by default and nothing in the decision changes.

#### Questions of 2026-11-14: reuse, block size, FPU

- **Reuse.** After patch 0003 blocks are reused: one QEMU boot of System 7.1 compiles 37,000 blocks (35,000 of them new)
  against 516,000 dispatcher entries, about fourteen entries a block before counting the jumps that never reach the
  dispatcher. How much of the run time is spent in native code the core cannot say: nothing counts inside generated code.
- **Size.** 3.5 instructions a block on average, from a ceiling of 64 (`MAXRUN`, `compemu_arm.h:103`): a block is the trace of
  one pass, ended by the first branch, so that is the branchiness of the code, not a limit hit. It hurts because every
  transition between blocks is expensive (see the next paragraph); the default `jitinline` (follow constant jumps) did not
  lengthen it under QEMU.
- **What a block boundary costs** (read, not measured; corrects an earlier wording of this note that spoke of a handler
  call that "revalidates"). Chaining exists: a block ending in a known successor jumps to it directly
  (`compemu_raw_endblock_pc_isconst`, `codegen_arm64.cpp:885`, target patched by `write_jmp_target`). By default
  (`jit_prefer_validated_successor_handler`, `compemu_support_arm.cpp:1746`) that jump lands on the successor's
  *handler*, not its body (`get_handler_for_edge`, `:5939`): a compare of `regs.pc_p` with the block's own address and
  a branch to `popall_cache_miss` on a mismatch (`compemu_raw_cmp_pc`, `compemu_raw_maybe_cachemiss`, `:348`, `:788`),
  which only guards against a tag collision; no checksum is computed there, that happens only when the target is
  `NEED_CHECK` (`set_dhtu_validated`, `:2813`). The rest of the cost is the epilogue every block carries: the countdown
  in memory through an absolute 64-bit address, a full PC snapshot of three stores (`pc_p`, `pc_oldp`, `pc`,
  `compemu_raw_set_pc_full_from_reg`, `:366`, required before any exit to C), a read of `regs.spcflags`, and the guest
  registers reloaded from and written back to memory around the block (`flush(1)`). Some twenty instructions and several
  memory accesses a boundary, over 3.5 guest instructions.
- **FPU.** `jitfpu` is not a switch in the build of the fork on AArch64: `USE_JIT_FPU` is left out (`configure.ac:1811`,
  "not fully wired yet"). `OKAPIA_JIT_FPU=1` (`gen-cpu.sh jit` and the Makefile) turns it on and the core compiles and links
  (5,475 KB); the preference `jitfpu true` then compiles FP instructions. What it does with our core: the JIT keeps the
  eight FP registers as host **doubles** (`jit_fpregs`) and synchronises with the core by converting
  (`compemu_legacy_arm64_compat.cpp:2475-2480`, `:2531-2535`: `(double)fpu.registers[i]` in, `(fpu_register)` out for
  the dirty ones), so every value that passes through compiled FPU code is rounded to 53 bits of mantissa and an
  11-bit exponent: the loss [patch 0008](../../project/decisions.md) exists to avoid. Fit for finding out whether the FPU
  is what keeps K'Whet at a tenth of the interpreter, not for shipping. Untested: no vector, no boot, no run.

#### `jitfpu true` and `jitinline true` on the Pi 4, 2026-11-14: no effect

Same card, same ROM, JIT with patches 0003 and 0004 and the FPU path compiled (`OKAPIA_JIT_FPU=1`): CPU 6.17, Graf 2.64,
Math 83, K'Whet 2.39, FPU 2.29 — within the noise of the runs without either (6.17, 2.58, 75, 2.35, 2.34).

- **`jitinline` is inert on AArch64 by the fork's own decision.** `sync_jit_prefs` (`compemu_prefs.cpp:63-75`) overrides
  it: `comp_constjump` is 1 only if the environment says `B2_JIT_ENABLE_CONSTJUMP=1`, because following constant jumps
  "produced real opt2 divergence … and corrupted downstream heap history". Not enabled here.
- **`jitfpu` was applied**: the preferences are re-read right before the tables are built
  (`ensure_aarch64_jit_runtime_ready`, `compemu_support.cpp:84-91`) and `avoid_fpu` follows `currprefs.compfpu`
  (`compemu_support_arm.cpp:1902`). With it off, FPU opcodes are not a break in the block but a native call to a "semantic
  service" (`op_fpu_semantic_comp_ff`, `:7250`); with it on they are translated. K'Whet did not move, so the cost of the
  FPU instruction is not in how it is dispatched. **Not explained**: why K'Whet runs at a tenth of the interpreter's
  speed when the arithmetic is the same binary128 core. Needs a counter on the FPU service, not a guess.

An earlier statement of this note, that every FPU instruction ends its block, was wrong for the same reason.

#### With constant jumps followed (`OKAPIA_JIT_CONSTJUMP=1`), 2026-11-14

The fork switches `jitinline` off on AArch64 unless `B2_JIT_ENABLE_CONSTJUMP=1` (above), so every earlier run had it off.
Built with `OKAPIA_JIT_CONSTJUMP=1`, which sets that variable at start-up, without the FPU path, with 0003 and 0004:

- **QEMU**: blocks of 4.5 instructions on average (3.5 before); System 7.1 reaches a normal Finder; the durability verdict is the
  same with the same 36,915 bytes written; twice as many blocks compiled during the boot (99,000 against 37,000).
- **Pi 4**, System 7.6.1, cache 64 MB, lazy flush: CPU **6.42**, Graf **3.08**, Math **86**, K'Whet **2.56**, FPU **2.24**,
  with nothing reported wrong and no check beyond that.

| (CPU / Graf / Math / K'Whet / FPU) | CPU | Graf | Math | K'Whet | FPU |
|---|---:|---:|---:|---:|---:|
| interpreter | 6.8 | 3.18 | 93 | 25 | 2.91 |
| JIT 0003, no inlining | 6.17 | 2.58 | 75 | 2.35 | 2.34 |
| JIT 0003 + constant jumps | 6.42 | 3.08 | 86 | 2.56 | 2.24 |

A few per cent over the same JIT without the inlining, and still below the interpreter on every line. **The JIT of
macemu-jit, ported as it is, does not beat the interpreter of `uae_cpu_2021` on this board.** What is left unexplained is
K'Whet, a tenth of the interpreter whatever is switched, FPU path included; the hot paths read (`memory.h`'s
per-access checks are cached and cheap) do not account for it.

### Emu68

*(Read in detail on 2026-11-14: see [Emu68](emu68.md).)* The only bare-metal one, and by reputation the fastest. It cannot be an engine of ours:

- it is built **big-endian** (`CMakeLists.txt:350-355`, `-mbig-endian`), so guest memory needs no byte swap — the
  translator has no little-endian path (`src/M68k_EA.c`, no byte-order test);
- **a guest address is a host address**: effective addresses are used as base registers directly
  (`src/M68k_EA.c:41-45`), which takes an address space of its own, where Circle maps memory one to one;
- its FPU registers are **doubles** — "Extended is 'emulated' in load/store only" (`include/M68k.h:84`) — against our
  binary128;
- it is a CPU and nothing else by rule (`AGENTS.md`), with no notion of Basilisk's `EMUL_OP`.

What it gives: the proof that a 68k JIT runs on a bare Raspberry Pi with no operating system under it, and a design
to read before route F (register mapping in `docs/internals/RegisterMapping.md`, its block cache in `src/cache.c`).

### Figures, and why none of them decides

| Figure | Source | Why it does not transfer |
|---|---|---|
| Finder 2.614 s interpreted, 5.210 s JIT | `macemu-jit`, Orange Pi 6 Plus | Strict mode; another machine; a fork that instruments every entry |
| 1,226–2,100 MIPS, 68040, SysInfo 4.4 | Amiberry issue 2315, Raspberry Pi 500+ (Cortex-A76) | JIT only, no interpreter run beside it; AmigaOS; a Dhrystone-style loop |
| About 800 MIPS | A 2021 PiStorm report for Emu68 | Big-endian, no byte swap, another design |
| 15.43× real time | pom68k, Apple M4 | Buys back an MMU we do not emulate ([pom68k](pom68k.md)) |

Two things do follow. The UAE4ARM backend is fast enough that Amiberry ships it in every release and tracks a 28%
regression in it as a bug (issue 2315) — so it is not "slower than an interpreter" by nature. And a JIT's gain is
largest on hot loops and smallest on Toolbox-heavy work, where Basilisk already runs native code for disk, video
and network; Speedometer's CPU and Graf lines, on our own board, are the comparison that counts.

### The measurement that decides

On our Pi 4, Raspberry Pi OS 64-bit, the ROM, System volume and Speedometer 4.0.2 of
[Benchmarks](../../contributing/benchmarks.md), `ramsize` 256 MB, `modelid 14`, `cpu 4`, `fpu true`:

1. `kanjitalk755/macemu` at our pinned SHA, interpreter — the baseline, and a check that Linux and Okapia agree.
2. `macemu-jit` with `--enable-aarch64-jit-experimental`, **ordinary mode**, then strict mode.
3. Its own 904 vectors and 33 pressure cells with our preferences (its QA runs `fpu false`).
   Then step 2 again with kanjitalk755's `rom_patches.cpp` in place of the fork's: does the JIT still reach the
   Finder on the ROM patches we ship ([Route A sized on Circle](#route-a-sized-on-circle))?
4. Amiberry on the same board, SysInfo with the JIT on and off, AROS ROM: the backend's own ratio over a sibling
   interpreter, free of the Basilisk port.

`scripts/jit-gate-pi.sh` runs **on the Pi** and does steps 1 and 2: `setup <rom> <disk image>` installs, clones
both trees at their pinned commits and builds them; `run jit|nojit|upstream` starts one variant on its own copy of
the disk. Written on 2026-11-13 and not yet run on a board.

Reading: step 2 at twice step 1 or better on CPU makes route A the one to take. Step 2 slower with step 4 fast
says the backend is sound and the port's instrumentation is the cost: route D, or A with that instrumentation
compiled out. Both slow closes the UAE family and leaves F.

## Circle side

## Circle side

### Executable memory is reachable

| Fact | Source |
|---|---|
| Every page at or above `_etext` gets `PXN = 1` | `lib/translationtable64.cpp:135` |
| Default before that test: `PXN = 0`, `UXN = 1`; `AP = RW_EL1` everywhere, text included | `:128-129`, `:120` |
| **`SCTLR_EL1.WXN` is explicitly cleared** | `lib/memory64.cpp:220` |
| 64 KB granule, L3 descriptors in ordinary memory | `translationtable64.cpp:105` |
| One table for all four cores | `memory64.cpp:155`, `:189` |

With `WXN` clear, RWX is permitted as soon as `PXN = 0`: no protection flip per compiled block. Recipe (one file of
ours): an arena aligned to 64 KB; read `TTBR0_EL1`; L2 index `addr >> 29`, L3 index `(addr >> 16) & 8191`; clear `PXN`;
`dsb ish`, `tlbi vaae1is` per page, `dsb ish`, `isb` (whether a permission change needs no break-before-make is to be
reconfirmed in the ARM ARM). **Circle need not be patched.** Putting the buffer in `.text` would write megabytes of
zeros into `kernel8.img`.

**Proved under QEMU on 2026-11-13** by `ExecMemSelfTest()` (`src/hosts/circle/execmem_circle.cpp`, built with
`EXECMEM_SELFTEST` in `OKAPIA_TRACE=1`, called from `kernel.cpp` right after the Mac RAM is claimed):

```
execmem: region 12020000, level 3 descriptor 3b019010 = 60000012020703
execmem: descriptor after clearing PXN: 40000012020703
execmem: PASS: first call 1234 (want 1234), re-emitted call 5678 (want 5678), PXN before 1 after 0, next page descriptor 60000012030703 PXN 1
```

The recipe held as written: level 2 entries are table descriptors (`translationtable64.cpp:80-87`), PXN is bit 53
(`armv8mmu.h:100-130`), and the neighbouring page kept its PXN. One correction: Circle's `memalign` refuses an
alignment above its heap block alignment (`alloc.cpp:33`), so a 64 KB-aligned arena is carved from a larger
`malloc`, not asked for. **Not proved**: that `__builtin___clear_cache` suffices on hardware — QEMU runs
re-emitted code without any flush. The same trace build on the Pi 4 settles it.

- **Caches**: `__builtin___clear_cache` (GCC emits `dc cvau`/`ic ivau` sized by `CTR_EL0`, allowed at EL1) — what the
  AArch64 backend uses. Never `SyncDataAndInstructionCache()` (`synchronize64.cpp:380`) per block: it flushes
  everything.
- **Arena placement**: the x86 JIT requires code under 4 GB (`compemu_support.cpp:3706`); the AArch64 backend lifts that.
  An arena **below `RAMBaseHost`** is unreachable by the guest (`do_get_real_address` adds an unsigned 32-bit address
  to `MEMBaseDiff = RAMBaseHost`). It must be **contiguous**, `popallspace` included (`create_popalls`, "B/BL branch
  range ±128 MB"); calls to helpers go through `BLR`, so distance to the kernel text does not matter. Both studies
  converged on taking it in the first allocation, before the Mac RAM.
- **`vm_acquire`/`vm_protect`**: `vm_alloc.cpp` is not built here and its `calloc` fallback rejects `vm_protect`; about
  60 lines of our own (linear arena, `vm_protect` a no-op returning 0).
- **Resumable faults: not needed.** Circle's synchronous vector never returns (`exceptionstub64.S:51`), and the
  `SIGSEGV`/`BRK` machinery in `macemu-jit` is a disabled debugging aid aimed at the Amiga blitter.
- **Exceptions**: all three `BUS_ERROR` definitions call `THROW(2)`, but the whole block is inside `#if 0`
  (`cpu_emulation.h:85-167`, identical in 2021 and 2026) and no live `THROW` call site exists, so no C++ exception can
  cross a generated frame. `uae_cpu_2026/memory.h:93-129` switches to `EXCEPTIONS_VIA_LONGJMP` under `-fno-exceptions`
  and defines `JMP_BUF`/`SETJMP`/`LONGJMP` on `sigsetjmp`, which circle-stdlib's newlib does not expose: predefine them
  on `setjmp`/`longjmp`.

## Basilisk side

### Budget

At the time: `kernel8.img` 1.85 MB, `.text` 1,063,764 bytes, interpreter handlers 303 KB, `cpufunctbl` 512 KB of data.
`gencomp` (x86 variant, run for scale): `compemu.cpp` 82,049 lines — estimated +0.9 to 1.2 MB of text. **Text is not the
main item**; the static tables are:

| Table | AArch64 size |
|---|---|
| `cache_tags[TAGSIZE]` (`TAGMASK 0x3ffff`) | 2,048 KB |
| `baseaddr[65536]`, `mem_banks[65536]` | 512 + 512 KB |
| `compfunctbl` + `nfcompfunctbl` | 1,024 KB |
| `nfcpufunctbl[65536]` (only with `NOFLAGS_SUPPORT_GENCOMP`) | 512 KB |
| `raw_cputbl_count[65536]`, `opcode_nums[65536]` (the latter only with `PROFILE_UNTRANSLATED_INSNS`) | 256 + 128 KB |

About 4.5 MB unconditional, over 5 MB with the options, plus ~128 KB of `hot_slot` in strict mode. They live in `.bss`,
so they are absent from `kernel8.img` but count in `_end`, and `sysinit.cpp:340` halts when `_end > MEM_KERNEL_END` —
"cannot inform the user here", before any log exists. Image size and address reservation are two budgets, and only the
second is checked. The way out without raising the reservation is to turn these file-scope statics into pointers into
the first allocation, a change for `patches/macemu/`. *(Since: the merged two-engine image is 2.84 MB, so the headroom
is smaller still.)*

### FPU

`configure.ac:1712` forces the IEEE core with the JIT; their build selects MPFR on ARM (`:1996-2001`), yet
`compemu_fpp.cpp` is dropped on AArch64 ("AArch64 JIT FPU path is not fully wired yet"), and FP register synchronisation
between JIT and FPU core exists only under `FPU_MPFR` (`compemu_legacy_arm64_compat.cpp:2450`, `:2506`). Their acceptance
QA runs `fpu false` (`qa/scripts/run-matrix.sh:180-190`) while Okapia forces an FPU. A JIT with no JIT FPU is the right
first target, but unproven. *(Since: Okapia's FPU is binary128 `FPU_IEEE`, not `fpu_uae`.)*

### Shutdown and data safety

`m68k_compile_execute` has `m68k_execute`'s structure, so the Shut Down chain holds in theory; it must be re-proven with
`run-test.sh`, JIT on and off. A stale compiled block runs **old code**, possibly old disk writes; translation
invalidation must follow the guest's writes to its own RAM.

### `FlushCodeCache()`

Three distinct duties must not be confused: publish freshly emitted AArch64 code (`__builtin___clear_cache` on the
range), invalidate **translations** when 68k source changes, honour the guest's own flush request. *(Since: for the
PowerPC interpreter's decode cache this hook already matters — see [PowerPC engine](../../topics/engine-powerpc.md).)*

### Allocation during the loop

`LazyBlockAllocator<T>::acquire()` grabs a 16 KB pool through `vm_acquire` when its free list is empty
(`compemu_support.cpp:900`, `:862`), called from `compile_block` during 68k execution — in both 2021 and 2026. A port must
preallocate fixed `blockinfo`/`checksum_info` pools, track high water, flush at a safe boundary before exhaustion, and
fall back to the interpreter when recovery is unsafe.

## The `macemu-jit` backend (2026-08-03)

- **Contents**: `uae_cpu_2026/compiler/` — `codegen_arm64.cpp` (1,688 lines), `compemu_midfunc_arm64*.cpp` (10,611),
  `compemu_support_arm.cpp` (8,943), `gencomp_arm.c` (5,192), pregenerated `compstbl_arm.cpp`/`comptbl_arm.h`. Lineage
  UAE4ARM (2019) → Amiberry → the fork, GPL v2 or later.
- **Platform surface**: `mprotect`/`SIGSEGV`/`BRK` only under `JIT_DEBUG_MEM_CORRUPTION`, defined nowhere; 64-bit
  pointer clean; no pthreads; 93 `getenv` tunables (NULL under newlib, so defaults); an unguarded
  `#include <SDL2/SDL.h>` and `SDL_Quit()` to cut out.
- **Correctness, serious**: 904/904 active vectors, 33/33 allocation-pressure cells, 48,282 legal 68040 encodings
  classified (46,087 native, 2,127 semantic services, 68 architectural traps), reproducible generation.
- **Performance** (fixed 2.6 GHz, Orange Pi 6 Plus, medians, bootstrap 95% CI), **all in strict mode** — the upstream
  column is literally "Strict JIT median"; no ordinary-mode measurement is published either way:

  | Workload | Result |
  |---|---|
  | Startup | 1.004× |
  | `ADDQ.L`/`DBRA` branch kernel | 2.632× faster |
  | Hot-block and 16-op `DBRA` kernels | 3.873×, 3.881× |
  | **Responsive Finder, end to end** | interpreter 2.614 s, JIT 5.210 s — **≈ 2× slower** |
  | SheepShaver's PowerPC JIT (other engine) | 1.889× slower than its interpreter |

  The Finder figure came after a 10.908× improvement internal to the JIT (a storm of invalidations removed), not a gain
  over the interpreter. Older "VNC desktop" figures were retracted upstream. The build flag is
  `--enable-aarch64-jit-experimental`, off, "unsupported". Reading: the JIT wins on hot loops and loses on real work.
- **Their two bugs, which aim at us**: lazy invalidation reusing stale blocks (a wrong driver list sent the JIT into
  the NuBus scanner; `jitlazyflush` now defaults to false) — the family of bugs that writes to the wrong place on a card;
  and cache tag aliasing with `TAGMASK 0xffff` at 64 MB of guest RAM, fixed to `0x3ffff` — Okapia runs 256 MB.
- **Porting cost**: against `uae_cpu_2021`, 40 files (23 added, 17 changed), 51,165 insertions; the generator itself
  changes (`gencomp.c` +1,412 lines, 72 AArch64 mentions against 0 in ours), with `newcpu.cpp`, `basilisk_glue.cpp`,
  `compemu_support.cpp`, `m68k.h`, `compemu.h`, `memory.h`. Not transplantable by copying `*_arm*` files.
- **Test corpus**: `jit-test/` (178 files) is mostly AArch64 emitter conformance, and its harness is **differential**
  (JIT against interpreter `REGDUMP`s); without reference dumps it does not judge an interpreter alone. Reusable: the 910
  risky vectors list, the harness shape, `BasiliskII/qa/` as a scenario model.

## Routes

| Route | Content | Verdict |
|---|---|---|
| **A** — adopt `uae_cpu_2026` as a whole, through `patches/macemu/` | core and backend, plus Circle adaptation | **the default route if the gate passes** *(2026-11-13)*: the only assembly whose correctness is demonstrated, and its proof comes with it. Inherits an instrumented core, their FPU configuration (QA runs `fpu false`), 51,165 lines, and a speed nobody has measured in ordinary mode |
| **B** — graft Amiberry's backend under our x86-generation generator | backend, `compemu.h`, support and tables | **tried upstream and failed**: "generator/backend API mismatch … now the primary blocker" (`AMIBERRY_ARM_JIT_PORT_PLAN.md:35-45`) |
| **C** — don't port | executable-memory spot check and hardware measurements | chosen in August; under review |
| **D** — rebuild the family tree on our core *(2026-11-13)* | ARAnyM's JIT2 generator and glue, which `uae_cpu_2021` still has the sockets for, over **`macemu-jit`'s audited** AArch64 midfuncs and emitter | the smallest diff to a core we already ship, no strict-mode machinery; **unproven** — 32-bit assumptions in ARAnyM's ARM path, 21 midfuncs to write, and the proof does not carry over: `macemu-jit`'s gates would have to be rerun on our assembly |
| **F** — a small tiered JIT of our own *(2026-11-13)* | blocks recorded as lists of interpreter handlers, hot ones compiled for the commonest integer instructions, the rest — FPU included — still calling the interpreter's handlers | fits the budgets by construction (tables sized at startup in the first allocation, no allocation in the loop, binary128 FPU untouched); gain unknown; PreviousTurbo is the worked example, Emu68 the reading |

Before any route: (1) the executable-memory spot check — clear `PXN` on a page, write a few AArch64 instructions,
`__builtin___clear_cache`, jump — passed under QEMU ([above](#executable-memory-is-reachable)), still owed on
hardware, where it counts: QEMU does not need the cache flush a Pi needs; (2) [the gate](#the-measurement-that-decides); (3) never compiled in by default nor enabled by
default.

What a JIT costs Okapia whatever the route:

- **`KERNEL_MAX_SIZE`**: `kernel8.img` is 3.48 MB today; route A adds 1.59 MB of text and 4.86 MB of bss, measured
  ([Route A sized on Circle](#route-a-sized-on-circle)). The 4 MB reservation — a settled decision — would have to
  move; it is a flag and a library rebuild ([Build](../../contributing/build.md#pitfalls)).
- **Restart**: the Macintosh goes round more than once, so compiler initialisation and teardown must be re-entrant
  and reuse one arena ([Startup and shutdown](../../topics/startup-and-shutdown.md)).
- **Data safety**: eager invalidation only (`jitlazyflush` false), and `run-test.sh` with the JIT on and off.
- **Seams**: interrupts are recorded in handlers and acted on in the engine's seam ([Input](../../topics/input.md));
  chained blocks must still reach it.

## Fallout usable without a JIT

- **Lock-free `spcflags`**: `uae_cpu_2026/spcflags.h:78-87` uses `__atomic_fetch_or/and` where 2021 took a mutex on every
  `SPCFLAGS_SET`. *(Since: `patches/macemu/0009` removed the lock with plain OR/AND on the single core; atomics are
  what S1 will need.)*
- **`FlushCodeCache()`**'s comment already anticipated a JIT.

## References

- Circle `lib/translationtable64.cpp:120-135`, `lib/memory64.cpp:155-220`, `lib/sysinit.cpp:340`, `lib/synchronize64.cpp:380`,
  `lib/exceptionstub64.S:51`
- `BasiliskII/src/uae_cpu_2021/compiler/compemu_support.cpp:862-972`, `:3706`; `cpu_emulation.h:85-167`
- `reference/macemu-jit`: `JIT-STATUS.md`, `PERFORMANCE_AUDIT.md`, `AMIBERRY_ARM_JIT_PORT_PLAN.md`,
  `BasiliskII/docs/AARCH64_JIT_STRICT_ENTRY_COUNTER_OPTIMISATION.md:64-73`,
  `AARCH64_JIT_STRICT_ARCHITECTURAL_CACHE_VALIDATION.md:14-36`
- Survey of 2026-11-13, default branches as of that day: <https://github.com/aranym/aranym>
  (`configure.ac:233`, `src/uae_cpu/compiler/`), <https://github.com/Chips-fr/uae4arm-rpi> (`src/jit/`),
  <https://github.com/BlitterStudio/amiberry> (`src/jit/arm/`, issue 2315), <https://github.com/tonioni/WinUAE>
  (`jit/arm/`), <https://github.com/michalsc/Emu68> (`CMakeLists.txt:350-355`, `src/M68k_EA.c:41-45`,
  `include/M68k.h:84`, `AGENTS.md`), <https://github.com/MW0MWZ/PreviousTurbo> (`src/cpu/jit040.h`),
  <https://github.com/rcarmo/macemu-jit> (`JIT-STATUS.md`, "Implementation review — 2026-09-06";
  `BasiliskII/docs/AARCH64_JIT_IMPLEMENTATION_REVIEW_20260906.md`; `compemu_midfunc_arm64_2.cpp` against Amiberry's
  `src/jit/arm/compemu_midfunc_arm64_2.cpp` and `compemu_arm.h:149`),
  <https://github.com/jhonnaker1/bmmacintosh>; the 2021 Emu68 report:
  <https://linuxjedi.co.uk/800mips-amiga-with-emu68-and-pistorm/>
- `BasiliskII/src/uae_cpu_2021/compiler/compemu_support.cpp:53`, `:533`, `:1167-1168`, `:2668-2672`; `m68k.h:449`
- [pom68k](pom68k.md), [Bottlenecks](bottlenecks.md) rank 21, [Benchmarks](../../contributing/benchmarks.md)
