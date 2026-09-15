# Bottlenecks on the Raspberry Pi 4

> Study of 2026-09-12 after the first Pi 4 Speedometer run (68k CPU 2.25, Bench. Ave. 2.38 — only 1.72× and 1.26×
> a Quadra 650), then a counter-review the same day that checked every claim against the sources and the
> disassembled Pi 4 kernel (byte-identical to the card's). Gains below are **estimates** unless marked measured.
> The first seven changes it ranked were applied on 2026-09-13: see [Benchmarks](../../contributing/benchmarks.md).

## Verdict

- **The ARM frequency was neither set nor logged.** No `CCPUThrottle`, `initial_turbo=0` in `config.txt`; Circle
  leaves a Pi 2/3/4 at its low speed by default. Plausibly the benchmark ran near 600 MHz. *Fixed.*
- **The planned multicore split (S1) did not exist.** `Start680x0()` ran on core 0, `ARM_ALLOW_MULTI_CORE` was
  commented out; cores 1–3 were parked. Comments describing an "emulation core" described the target, not the
  binary. *Still true — see [Architecture](../../project/architecture.md#multicore).*
- **Four toolchain bottlenecks** had escaped the first study, all trivial to fix: newlib unoptimised, libhfs's
  byte-by-byte `memcmp` linked kernel-wide, FatFs without fast seek on 4 KB clusters, and periodic reports on a
  polled serial port. *Three fixed, fast seek open.*
- **Frequency did not explain everything**: the same SheepShaver fork, interpreted, on a Pi 4 under Linux, published
  Speedometer CPU 5.44 and Graf 3.22 against Okapia's 1.24 and 0.46 — 4.4× and 7×, beyond the 2.5× a low frequency can
  explain. Candidates: compositor and log on the emulation core, compiler flags, guest System version.
- **The compositor weighs more than its nominal budget in graphics tests**: Graf/CPU and Color/CPU ratios bound the
  platform's own loss at up to 41% and 26% against the native M4 run.

## Where the time goes in the 68k interpreter

Checked in the Pi 4 binary:

- **Dispatch**: each instruction fetches and swaps the opcode, loads a target from `cpufunctbl` (524,288 bytes, 65,536
  64-bit pointers), `blr`s to the handler and returns (`newcpu.cpp:1510-1545`). **1,551** distinct `op_xxxx` handlers,
  205,904 bytes (the first study's "201 handlers, 309,928 bytes" was wrong). A table far larger than L1, stressing
  the indirect branch predictor and caches — where the Cortex-A72 is weakest against an M4.
- **Tick counter per opcode**: `ldrh`, `add`, `and`, `strh`, `cbnz` (`newcpu.h:329-333`). Estimated 2–5%, not 5–15%;
  native Basilisk pays it too, so it does not explain the M4/Pi gap.
- **Generic flags**: `regflags` is 20 bytes; about half of `ADD.W`'s 33 instructions compute flags. The cost is
  computing five conditions, not storing them, so a compact word would not help; host flags (`adds`/`subs` then
  `mrs nzcv`) would. `reference/macemu-jit` (`uae_cpu_2026/m68k.h:742`) already has them for AArch64.
- **Globals**: `adrp`+`add` are constants, not loads; only `MEMBaseDiff` and `regs.pc_p` are read. Passing a context in
  a register: ≤ 5%.
- **Dead stores**: `regs.fault_pc` written twice per instruction plus a PC recomputation; its only reader is the 68040
  bus error frame, reached only through `BUS_ERROR`, which nothing invokes in this configuration. *Removed by
  `patches/macemu/0009` (`M68K_NO_FAULT_PC`).*
- **A lock per SR write**: `SPCFLAGS_SET`/`CLEAR` went through `B2_lock_mutex` → `EnterCritical`, and `MakeFromSR()`
  does one of each on every SR write. *Plain OR/AND in `0009`; atomics will be needed for S1.*
- **Fetch**: `get_iword()` recomputed the PC and re-translated it for every opcode and extension word, when the
  pointer already sits in `regs.pc_p`. *`patches/macemu/0010`.*
- **Not bottlenecks**: unaligned access (GCC 15 already emits one `ldr` + `rev`); `DIRECT_ADDRESSING` (one add).

## FPU

binary128 is exact and software: elementary operations call libgcc (`__addtf3`, `__multf3`…, optimised), but the
transcendental functions (`sinl`, `logl`, `expl`, `sqrtl`, `powl`… 20 imports from `fpu_ieee.o`) came from **newlib
built at `-O0`** — `configure` gave the release branch no `-O` (`configure:356-361`); `sinl` stored and reloaded its
argument on the stack. *Fixed by `patches/circle-stdlib/0001`; `fpu_selftest_extended()` must stay bit-identical.* The
PowerPC half imports nothing from libm. A faster exact core (softfloat `floatx80`, which would also round at 64 bits
between operations as a real 68881 does) remains a 5/5 project, only if real applications ask. Note the M4 native
Basilisk computes its FPU with MPFR (`MacOSX/config.h:834`), so the FPU line of the M4/Pi comparison compares
libraries, not processors.

## Compositor

- libhfs's `memcmp.c`, a stand-in for 1996 Unixes, was compiled with libhfs and, being an object, **won over newlib's
  for the whole kernel**: one byte per iteration against 16. The tile comparison paid about 4 cycles per byte instead
  of 0.3–0.5 — an estimated 6.5 ms per changed frame at 1280×960 16-bit and 1.5 GHz. *Removed from the build.*
- The composite runs synchronously in the Mac's VBL, so its cost comes straight out of guest time. Dynamic frameskip
  caps at one refresh per 12 VBLs, so a composite over ~25 ms exceeds the nominal eighth.
- Measure `composite … % of wall` during `Graf` on the Pi before deciding S2. A smaller output mode is a cheap
  experiment separating HDMI presentation from QuickDraw in the guest.

## Disk

The first study blamed redundant `lseek`s; on Circle `lseek` is `f_lseek`, and seeking to the same place is free. The
real cost is the **FAT chain walk**: with `FF_USE_FASTSEEK 0`, `f_lseek` follows the cluster chain (`ff.c:4665`) —
forwards from the current cluster, **backwards from the file's first cluster** — one `get_fat` per cluster, a sector
read per 128 clusters. And `f_read` stops at every cluster boundary: a 1 MB Mac read becomes 256 SD commands at 4 KB
clusters. A 500 MB image on 4 KB clusters spans 128,000 clusters over 1,000 FAT sectors. Unmeasured hypothesis: part of
why 7.6 boots so much slower than 7.1 under QEMU.

1. **32 KB clusters** (`mformat -F -c 64`): jumps and commands divided by 8. *Done, and in
   [Getting started](../../getting-started.md).*
2. **FatFs fast seek**: a cluster link map built when an image opens (`f_lseek(fp, CREATE_LINKMAP)`). No cache: it
   describes where a fixed-size file's clusters are, and FatFs refuses to grow a file in fast-seek mode. *Open.*
3. **`SD_HIGH_SPEED`** (the bus runs at 25 MHz, data in PIO): only after the SD write-error path is tested. *Open.*

Also found: `_lseek(int, int, int)` in circle-newlib (`io.cpp:377`, `:688`) refuses offsets ≥ 2 GiB, so a volume of
2 GB or more is unreadable past that point (the failure is clean, `Sys_read` returns 0) — a limit written nowhere
until now. *Fixed by `patches/circle-stdlib/0002` (2026-09-15); FAT32 still ends a file at 4 GiB − 1
([Storage](../../topics/storage.md#volumes-past-2-gib)).* ExtFS lists directories in O(N²) (`extfs.cpp:1403-1421` reopens and rereads up to the index on every call).

## Serial reports

`CSerialDevice` is polled (`serial.cpp:865`): 87 µs per character at 115,200 baud. About 530 characters every five
seconds made ~46 ms blocked per window, in four stops of 8–15 ms — a lost frame and a pointer hitch every five seconds.
Polling is kept on purpose (the last words before a hang must get out). *Periodic reports now behind `perfreport`;
after S1, core 0 could drain a ring.*

## Four cores

| Core | Responsibility | Condition |
|---|---|---|
| 0 | IRQs, USB with hot-plug, timers, `CCPUThrottle`, log draining | the tick only raises atomic flags |
| 1 | the emulation loop (68k or PowerPC), input drain, PRAM, **disk I/O** (synchronous) | atomic `SPCFLAGS`/`InterruptFlags`; the five-restart test; FatFs is declared safe across cores (`doc/multicore.txt:33`), one core touching the card at a time |
| 2 | compositor, sole writer of the HDMI buffer | the engine publishes the VBL and never waits; mode and palette changes need a handshake; watch L2 refills (1 MB shared) |
| 3 | audio, network — later | leaving a core free is a good split, not waste |

S1 itself is worth 0–5% directly; its value is isolation, hot-plug, an asynchronous log and being the prerequisite
for S2. `spcflags` sharing a cache line with the PC is negligible at ≤ 1 kHz.

## Ranking (counter-review, with state)

| # | Change | Gain (estimate) | Complexity | State |
|---:|---|---|---:|---|
| 1 | Try `force_turbo=1` with the log captured | ×1–2.5 all | 1 | superseded by 2 |
| 2 | Log frequency, temperature, throttling; `CCPUThrottle` at maximum **without** `SetOnTemperature` (its `socmaxtemp` default is 60 °C, which an unheatsinked Pi reaches in minutes) | same, durable | 2 | **done** |
| 3 | Drop libhfs `memcmp.c` | compare ×5–10 | 1 | **done** |
| 4 | newlib `-O2` | transcendentals ×2–4 | 1 | **done** |
| 5 | 32 KB clusters | seeks ÷8 | 1 | **done** |
| 6 | Serial reports behind a switch | ~1%, no 15 ms stops | 1 | **done** |
| 7 | Dead `fault_pc`, lock-free `SPCFLAGS` | 3–8% | 1 | **done** (`0009`), plus `pc_p` fetch (`0010`) |
| 8 | FatFs fast seek | O(1) seeks | 2–3 | open |
| 9 | PMU counters in RAM (`PMCR_EL0`, events `0x08`, `0x10`, `0x03`, `0x17`, `0x05`; check `PMCR_EL0.N` = 6, Circle does not set `MDCR_EL2`) | instrument | 2 | open |
| 10 | AArch64 host flags from `macemu-jit`, validated on its `jit-test` and `qa/tests` corpus | 5–15% | 3 | open |
| 11 | S1: engine on core 1 | 0–5% direct | 4 | open |
| 12 | S2: compositor on core 2 | measured compositor share | 4 | open |
| 13 | Event-driven 1 kHz service; opcode counter in trace builds only | 2–5% | 2–3 | open |
| 14 | `SD_HIGH_SPEED` | sequential ×1.5–2 | 1 code, 3 validation | open |
| 15 | Tail-call dispatch (`[[gnu::musttail]]`) | 5–25% | 4 | only if branch mispredictions exceed ~10% of cycles |
| 16 | ExtFS listing cache | Finder O(N) | 3 | open |
| 17 | Exact `floatx80` FPU core | FPU ×1.5–3 | 5 | only on demand; check the source's licence |
| 18 | 2 MB blocks for Mac RAM (64 KB pages today) | 0–5% | 3 | only if TLB refills are high |
| 19 | Handler layout, compact table, `-O3` on the engine only | ±5–10% | 2–3 | never `-O3` on frame buffer writers |
| 20 | Pre-decoded blocks | ×1.2–1.8 | 5 | after 10 and 15; must hook `FlushCodeCache()` |
| 21 | JIT (the `macemu-jit` backend) | ×2–8 | 5 | reopens "no JIT" and the non-executable heap — see [68k JIT](jit-68k.md) |

## Measurement protocol

1. Record git revision, kernel MD5, card and cluster size, Mac resolution and depth, sound off, guest System and
   Speedometer versions, board revision and cooling.
2. Five runs per configuration: median, minimum, maximum.
3. Capture the whole log: `k opcodes/s`, `composite … % of wall`, full scans, frequency, temperature, throttling.
4. Check the chart scale: Speedometer's axis follows the longest bar (always `Math`: 0:30 for 28.76, 0:480 for 468.29),
   so the old "8× between a 0:60 and a 0:480 run" reservation most likely compared two different machines. One run
   on two scales settles it.
5. Measure the disk elsewhere too: Speedometer's `Disk` does not exercise long seeks. Time the 7.6 boot and opening an
   application placed at the end of the volume, on a copy of the image.
