# A PowerPC JIT for Okapia

> Study and plan of 2026-11-28, no code yet. Question: is there a PowerPC → AArch64 JIT for SheepShaver or
> elsewhere worth taking, or should Okapia do for the PowerPC what it did for the 68k? Sources: `kpx_cpu` in
> `external/macemu`, `rcarmo/macemu-jit` in `external/macemu-jit` (pinned `ba4a541`) and its published pages, the
> 68k study ([68k JIT](jit-68k.md)), OkapiaBench's runs of 2026-11-27
> ([Benchmarks](../../contributing/benchmarks.md#okapiabench-2-on-the-macbook-pro-m4-both-engines-2026-11-27)).
> Status boxes for this work live on the [PowerPC engine](../../topics/engine-powerpc.md#status) page.

## Verdict

- **One PowerPC → AArch64 JIT exists for SheepShaver, and it does not pay yet**: `macemu-jit`'s, the only figure
  it publishes is 1.889× *slower* than the interpreter, on a deliberately dispatch-heavy loop. Its design (short
  blocks, no block-to-block chaining yet, its register cache flushed at every memory access) is the profile of the
  same fork's 68k route, which gave parity on our Pi 4.
- **No other one can be grafted**: the PowerPC → AArch64 translators in production serve game consoles (Dolphin's
  for the GameCube and Wii) or a whole machine (QEMU's TCG); they are to be read, not taken.
- **The route that paid for the 68k is the plan here**: a translator of our own beside the interpreter, shaped
  like `patches/macemu/0012` — registers kept in host registers across blocks, long blocks chained directly,
  loads and stores inline, everything it does not know left to the interpreter. `kpx_cpu` already has the
  frame for it: a JIT path in its execute loop, a block cache and the cache invalidation of `icbi`, all
  compiled out today because its only code generator is dyngen, which has no AArch64 target.
- **First, measure**: build `macemu-jit`'s translator on our tree and time it with OkapiaBench (Phase 1), as the
  68k route A was. Build our own only if it does not pay.

## Why it is worth it

- **The interpreted PowerPC engine is the slowest thing Okapia runs.** On the MacBook Pro M4 (2026-11-27), the
  same System 7.6.1: CoreMark 16,174 iterations a second on the 68k engine translated, 1,483 interpreted, 1,465
  on the PowerPC engine; Embench 9,032, 585 and 744. On a Pi 4, Speedometer's CPU score is 2.96 for the PowerPC
  engine against 6.84 for the interpreted 68k one, and 35.01 for the 68k translator
  ([Benchmarks](../../contributing/benchmarks.md#summary)).
- **A PowerPC translator also speeds up the 68k code of Mac OS**: the ROM's 68k emulator is itself PowerPC code.
  OkapiaBench's `tb_traps` takes 7.7 s on the PowerPC engine against 0.19 s translated, and the 68k build of
  OkapiaBench under Mac OS 8.6 runs CoreMark ten times slower than its PowerPC build (147 against 1,461).
- **The PowerPC translates more simply than the 68k**: fixed 32-bit instructions, a load/store architecture,
  no condition codes set by every instruction, and a FPU in IEEE double that the host's FPU computes directly,
  where the 68881's extended precision forced softfloat calls (the 68k translator's FPU gain is 1.7×).

## What exists

| Project | Translates | Licence | For Okapia |
|---|---|---|---|
| `kpx_cpu` (kanjitalk755 SheepShaver) | PowerPC → host, through dyngen | GPL-2.0-or-later | dyngen for PowerPC, x86 and MIPS hosts only (`configure.ac:1595-1618`, [PowerPC engine](../../topics/engine-powerpc.md)); the JIT frame is reusable |
| [`rcarmo/macemu-jit`](https://github.com/rcarmo/macemu-jit) | PowerPC → AArch64, direct emission (`SheepShaver/src/kpx_cpu/src/cpu/jit/aarch64/ppc-jit.cpp`, 4,215 lines) | GPL-2.0-or-later | on by default there; boots the OldWorld desktop; 303 interpreter/JIT equivalence cases; one timing, 1.889× slower than the interpreter ([result of 2026-08-02](https://github.com/rcarmo/macemu-jit/blob/master/SheepShaver/docs/AARCH64_JIT_BENCHMARK_RESULT_20260802.md)); chaining listed as remaining work ([plan](https://github.com/rcarmo/macemu-jit/blob/master/SheepShaver/AARCH64_JIT_PLAN.md)). Last SheepShaver commit seen: 2026-08-03 |
| Dolphin `JitArm64` | Gekko/Broadway (GameCube, Wii) → AArch64 | not checked | in production for the GameCube and Wii, with fastmem through host faults ([issue 13444](https://bugs.dolphin-emu.org/issues/13444)); tied to its console's processor and memory model. A source of design, not of code |
| QEMU TCG (`qemu-system-ppc`) | the whole machine → any host | not checked | what UTM runs Mac OS 9 with on Apple silicon ([The Register](https://www.theregister.com/2023/09/29/utm_apple_hypervisor_foss_fest/)); a whole-machine emulator, far beyond a 4 MB kernel |
| MAME's PowerPC recompiler | 6xx through its UML → backends | — | MAME has an ARM64 backend (`drcbearm64.cpp`); whether its PowerPC cores use it in production was not established |
| RPCS3's PPU recompiler | the PS3's PowerPC, through LLVM | not checked | on arm64 since 2024 ([blog](https://rpcs3.net/blog/?p=2011)); another PowerPC (the Cell's), and LLVM |
| Cemu's recompiler | Espresso (Wii U) | not checked | x86-64; an AArch64 backend was envisaged in 2023 ([PR 641](https://www.github.com/cemu-project/Cemu/pull/641)), none found |

## What `kpx_cpu` already provides

- **A JIT path in the execute loop**, compiled when `PPC_ENABLE_JIT` (`ppc-config.hpp:92-93`, defined as
  `ENABLE_DYNGEN`): find or `compile_block(pc())`, `codegen.execute(bi->entry_point)`, check `spcflags()` on exit,
  `SPCFLAG_JIT_EXEC_RETURN` to force a new lookup after an invalidation (`ppc-cpu.cpp:587-620`). The reentrant
  variant (`PPC_REENTRANT_JIT`) lets an `EMUL_OP` that calls back into PowerPC code run translated code too.
- **The interpreter beside it**, with its decode cache (`ppc-cpu.cpp:622-`): blocks of pre-decoded instructions
  ending at `CFLOW_END_BLOCK`, each entry an `execute` member function — what a translator calls for an
  instruction it does not know.
- **Cache coherence**: `icbi` accumulates a range and `isync` invalidates it (`ppc-execute.cpp:1251-1281`);
  SheepShaver's `FlushCodeCache(start, end)` calls `invalidate_cache_range` (`sheepshaver_glue.cpp:729-732`).
  A stale decode cache already cost a boot once ([PowerPC engine](../../topics/engine-powerpc.md#pitfalls)).
- **SheepShaver's own opcodes** — `EMUL_OP`, `NATIVE_OP`, the return to emulation — reach
  `sheepshaver_cpu::execute_sheep` (`sheepshaver_glue.cpp:253-276`): exits of a block, as `EmulOp` is in the 68k
  translator.
- **The register file** (`ppc-registers.hpp:229-233`): 32 GPRs, 32 FPRs, 32 vector registers, CR, XER, then LR,
  CTR, FPSCR, VSCR, VRSAVE — the layout translated code reads and writes at fixed offsets.
- **The seam** of `patches/macemu/0004`: `PPC_CHECK_TICKS` instructions between two calls of
  `powerpc_check_ticks()`, the board's clock work. Translated code has no instruction count; it must spend a budget
  as the 68k translator does.
- **The processor it reports**: `PVR = 0x000c0000`, a 7400 with AltiVec (`src/common/sheepshaver/main_okapia.cpp:68`;
  OkapiaBench reads `cput 0000010c`). Applications that test for AltiVec take their vector paths.

## What the 68k translator leaves to reuse

From [the translator](../../topics/engine-68k.md#the-translator-okapia_jit_arm641-experimental):

- **Circle's executable memory**: `execmem_circle.cpp` (16 MB of heap made executable once, claimed after the Mac RAM
  block) is in the shared half; one engine runs at a time, so the arena can serve both — claimed **once**
  ([Architecture](../../project/architecture.md#what-exists-once-and-what-exists-twice)).
- **The block model**: direct branches between blocks, a return table for indirect transfers, a budget spent on
  backward transfers that brings the loop back for interrupts and the seam.
- **Coherence by epoch**: a flush raises an epoch; a block of an older epoch is compared with its source before
  it runs again. Nothing is rewritten in translated code.
- **What it does not know leaves the block**: the interpreter's handler, called from translated code.
- **The measurements**: the gate on the Pi 4, Speedometer and OkapiaBench, sessions ended with Shut Down.

## The plan

Each phase ends with a measurement, and a phase that does not pay stops the plan.

### Phase 0 — the baseline and the cheap gains

- OkapiaBench on the Pi 4, PowerPC engine interpreted, Systems 7.6.1 and 8.6, three runs each: the reference every
  later phase is compared with (the M4 runs exist).
- `VM_CAN_ACCESS_UNALIGNED` for AArch64 Normal memory (`vm.hpp:27`): every 32-bit guest read is four byte reads
  today, 24 % of an interpreted memory instruction's cost
  ([PowerPC engine](../../topics/engine-powerpc.md#cpu-kpx_cpu-interpreted)). An interpreter change, and the
  translator's loads rest on the same question.
- `PVR` of a 750 against a 7400, OkapiaBench and Speedometer: what the vector paths cost under the interpreter.
- Where Mac OS 8.6 spends its time: native PowerPC code, the ROM's 68k emulator, SheepShaver's own opcodes — a
  count by program-counter range in a trace build, so that the translator is aimed at what runs.

### Phase 1 — route A: `macemu-jit`'s translator, measured

- Build its `ppc-jit.cpp` against our `kpx_cpu` (`OKAPIA_JIT_PPC=macemu`, a build switch like `OKAPIA_JIT=1`), on the
  desktop first (`src/hosts/desktop`, Apple silicon), then the Pi 4.
- OkapiaBench and Speedometer, translator against interpreter; every OkapiaBench check must be the
  interpreter's.
- **Decision**: CoreMark or Embench at least 2× the interpreter on the Pi 4 → finish this route (chaining is its
  own remaining work); otherwise Phase 2. Its code remains a reference for the PowerPC forms it already translates
  and their helpers.

### Phase 2 — our own translator: the frame

- `OKAPIA_JIT_PPC=1` and a preference (`jitppc true`), off by default, beside the 68k translator's switches
  ([Decisions](../../project/decisions.md#no-jit-by-default-two-experimental-ones)).
- In `kpx_cpu`'s `PPC_ENABLE_JIT` path, our own code generator instead of dyngen: `compile_block` emits AArch64, the
  block cache and `spcflags()` stay theirs. A patch in `patches/macemu/`, as `0012` is.
- A block runs until a branch, or an instruction the translator does not know: that one is called through the
  interpreter's handler and ends the block.
- An equivalence harness on the host (`tests/host`): each translated instruction against the interpreter, on
  random registers and memory, before any boot. `macemu-jit`'s 303 cases are a starting list.
- First boot: System 7.6.1 then 8.6 under QEMU and the desktop, OkapiaBench's checks identical.

### Phase 3 — integer code at speed

- **Registers**: the 68k pins its 16; the PowerPC has 32 GPRs against the 31 the host has, the state pointer and
  temporaries included. Start with a block-local cache with write-back at exits, r1 (stack) kept in a register;
  measure pinning more (r2, r3 to r12).
- **Condition register**: CR0 computed lazily from the host's NZCV and XER's SO, the other fields packed in one host
  register; XER's CA in a register.
- **Loads and stores inline**: guest address plus the base, `LDR` then `REV` (big-endian, as in the 68k translator);
  the guest frame buffer's placement checked first, since any access to Device memory must be alignment-safe
  ([Display](../../topics/display.md)).
- **Chaining**: direct branches between blocks, a table for `bclr`/`bcctr` (returns and indirect calls, CFM's
  glue), the budget on backward branches and the seam reached through it.
- `lwarx`/`stwcx.` as `LDXR`/`STXR` or a reservation kept in a register; `rlwinm`/`rlwimi` as `UBFM`/`BFM` for
  contiguous masks; `isel` as `CSEL`; `dcbz` on 32 bytes.
- **Gate**: OkapiaBench's `cpu_` and `emb_` and CoreMark on the Pi 4; the 68k translator's 10 to 16× on integer
  tests is the order of magnitude to aim at.

### Phase 4 — the FPU

- `fadd`, `fsub`, `fmul`, `fdiv`, `fmadd`/`fmsub` (`FMADD`, one rounding as on the PowerPC), `fabs`, `fneg`, `fmr`,
  conversions, in the host's FPU when FPSCR selects round-to-nearest with no exception enabled; the interpreter's
  handler otherwise.
- FPSCR's sticky bits and `fres`/`frsqrte`: the interpreter's results are the reference (the guest saw `kpx_cpu`,
  not a particular chip), through calls.
- **Gate**: OkapiaBench's `fp_` and `app_raytrace`, checks equal to the interpreter's.

### Phase 5 — AltiVec, if Phase 0 says it matters

- NEON has 32 registers of 128 bits too; element order is the difficulty (big-endian lanes, `vperm` through `TBL`
  with indices corrected).
- Or report a 750 and leave AltiVec to the interpreter, if Phase 0 shows applications take a faster path for it.

### Phase 6 — the board

- Kernel size: the Pi 4 kernel with the 68k translator is 3,455 KB against a 4 MB budget (AGENTS.md); a second
  translator must fit, or the budget be revisited with its cost cited.
- No allocation in the emulation loop: block cache, code arena and tables carved at start.
- The restarts: the arena and the caches of both engines across the fifth restart and an engine switch
  ([Startup and shutdown](../../topics/startup-and-shutdown.md)).
- Data safety: `scripts/run-test.sh` with the translator on, sessions ended with Shut Down.

## Risks

- **Self-modifying code**: CFM loads code, the ROM's 68k emulator may generate PowerPC code (`jit68k`, off by
  default, [PowerPC engine](../../topics/engine-powerpc.md)); every write the guest does not announce with
  `icbi`/`isync` or `FlushCodeCache` must still be caught (the epoch and comparison of the 68k translator).
- **Reentrancy**: `EMUL_OP` handlers call back into PowerPC code (`execute_depth > 1`); translated code must be
  reentrant or step aside, as `PPC_REENTRANT_JIT` decides upstream.
- **Short blocks**: CFM's glue ends in `bctr` every few instructions; without the return table and chaining the
  translator spends its time in dispatch — the `macemu-jit` figure.
- **The time base**: read through `mftb`, it must keep the board's clock, never the instruction count.

## References

- `external/macemu/SheepShaver/src/kpx_cpu/src/cpu/ppc/` — `ppc-cpu.cpp`, `ppc-config.hpp`, `ppc-execute.cpp`,
  `ppc-registers.hpp`, `ppc-translate.cpp` (the dyngen translator, 1,618 lines)
- `external/macemu/SheepShaver/src/kpx_cpu/sheepshaver_glue.cpp`
- `external/macemu-jit/SheepShaver/src/kpx_cpu/src/cpu/jit/aarch64/`
- `patches/macemu/0004`, `patches/macemu/0012`, `src/hosts/circle/execmem_circle.cpp`, `jit_arm64_circle.cpp`
- [68k JIT](jit-68k.md), [Emu68](emu68.md), [PowerPC engine](../../topics/engine-powerpc.md),
  [Benchmarks](../../contributing/benchmarks.md)
