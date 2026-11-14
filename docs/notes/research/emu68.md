# Emu68, and what it would take to copy its design

> Reading study, 2026-11-14. Question: after the JIT of macemu-jit turned out to match the interpreter and no more
> ([68k JIT](jit-68k.md)), is there a 68k→AArch64 design that could beat it by a wide margin on a Pi 4, and what would it
> ask of Okapia? Source: `michalsc/Emu68` at `61a61f7` (2026-11-10), read as a clone (`scripts/fetch-reference.sh emu68`).
> **Read, not built, not run, not measured.** Every figure below that is not a line reference is a hypothesis, said so.

## Verdict

1. **Emu68 is built on four decisions that macemu-jit's glue contradicts**, and they are the ones our measurements
   pointed at: the guest's registers live in AArch64 registers *for good*, not per block; a block is a long trace that
   follows the predicted path of every branch, call and return; the condition codes are the host's NZCV flags; and
   nothing runs between two blocks but a loop that checks one flag word.
2. **It cannot be linked as it is** and it is not a drop-in engine: big-endian build, a guest address that *is* the host
   address, a FPU in doubles, no notion of Basilisk's `EMUL_OP`, and a 14,000-line translator written for all of that.
3. **Its design can be carried over**, and the part that is not carried over is small and known (a base register and a
   byte swap on every guest memory access, helper calls for what is exact in binary128, a policy for the 68040 cache
   flushes Mac OS issues hundreds of times a second). Whether that beats the interpreter by a lot is the one thing
   nobody has measured, and [a cheap experiment](#route-g-an-emu68-shaped-jit-for-okapia) would say before the work.

## What Emu68 is

A CPU, not an emulator of a machine: a 68040 (and a PowerPC) that runs bare metal on a Raspberry Pi, with the
instruction set as its only interface, MPL-2.0 (`LICENSE`), about 15,000 lines of translator in `src/M68k_*.c`, 5,300 of
them the FPU (`M68k_LINEF.c`), plus a PowerPC translator (`src/PPC_*.cpp`) not read here. Its own rules (`AGENTS.md`) say
what it will not do: no hardware emulation, no native-code hook, no MMU yet ("Not now": it needs to leave a block midway
with the registers unwound).

## The four decisions

### 1. The registers are pinned, for good

`docs/internals/RegisterMapping.md`: `D0-D7` in `W19-W26`, `A0-A4` in `W13-W17`, `A5-A7` in `W27-W29`, `PC` in `W18`,
`FP0-FP7` in `D8-D15`, `SR`, the instruction counter, the context pointer and the cache and stack registers in the upper
lanes of `V19-V21`. The register allocator of the AArch64 build does not allocate them: `RA_MapM68kRegister` returns the
fixed register, and `RA_StoreDirtyM68kRegs`, `RA_FlushM68kRegs`, `RA_SetDirtyM68kRegister` are empty
(`src/aarch64/RegisterAllocator64.c:60-143`). A block has **no load of its registers on entry and no store on exit**;
`EMIT_LocalExit` (`src/M68k_Translator.c:234-276`) writes back only the PC offset, the flags and the optional
instruction counter, then `bx lr`.

Against macemu-jit, whose blocks load the guest registers they use from memory and write the changed ones back around
every block (`flush(1)`, `compemu_support_arm.cpp:8848`), with a full PC snapshot of three stores and the countdown in
memory at each boundary ([68k JIT](jit-68k.md), "What a block boundary costs").

### 2. A block is a trace along the predicted path

- `EMU68_M68K_INSN_DEPTH 256` guest instructions at most, `EMU68_MAX_LOOP_COUNT 8` (`include/config.h:46`, `:53`); the
  macemu-jit block is 3.5 instructions on average (a ceiling of 64).
- **`BRA` and `BSR` within 8191 bytes are followed** instead of ending the block (`src/M68k_LINE6.c:95-101`,
  `EMU68_BRANCH_INLINE_DISTANCE`, `config.h:54`); a `BSR` pushes its return address on a *compile-time return stack*
  (`M68K_PushReturnAddress`, `M68k_Translator.c:182-214`), so that the matching `RTS` is translated as a check against
  the predicted address and the trace goes on through the call and back.
- **A conditional branch is predicted** (`M68k_LINE6.c:145-160`: backward within 256 bytes is taken, forward is not), the
  translator carries on along the predicted side and emits the other side as an exit stub at the end of the block
  (`MARKER_EXIT_BLOCK`, `M68k_Translator.c:416-470`).
- `PC` is not updated per instruction: an offset is accumulated (`EMIT_GetOffsetPC`/`EMIT_ResetOffsetPC`) and applied
  once.

### 3. The flags are the host's flags

`RA_GetCC` brings the 68k condition codes into `NZCV` and `Bcc` is a host conditional branch on them
(`M68k_LINE6.c:128-170`); the swap of the V and C bits between the two layouts happens only when `SR` is read or written
(`swapVC`, `ExecutionLoop.c:96`, `:454`). macemu-jit also keeps flags in `NZCV` (`OPTIMIZED_FLAGS`), so this is not what
separates them; what separates them is that the flags survive a block boundary here at the cost of `RA_StoreCC` once.

### 4. Between blocks there is a loop, and it is short

`JITLoop` (`src/ExecutionLoop.c:628-683`) does, per block: find the block, `ARMCode()` (a call through `x12`), test
`ctx->INT64` for a pending interrupt, test whether the block cleared `x12`, repeat. The find is a 4-way, 512-set LRU keyed
by the guest PC (`LRU_FindBlock`, `ExecutionLoop.c:168-190`, `config.h:42-43`) and, on a miss, a 65,536-bucket hash with a
global **epoch** in the key (`FindUnitQuick`, `:258-340`). There is **no chaining**: every block returns through this
loop. With blocks of a few dozen instructions the loop's price is spread thin; with 3.5 it would not be, which is the
macemu-jit situation again.

Interrupts are not polled by generated code at all: another core sets a flag word (`M68kReportInterrupt`,
`ExecutionLoop.c:780`), the loop reads it after each block, `ProcessIRQ` (`:401-511`) builds the exception frame. The
latency is bounded by a block's length, itself bounded by the 256 and the 8 loop iterations.

## Coherence of the translated code

`invalidate_instruction_cache` (`src/M68k_LINEF.c:1857-1897`) is every `CINV`/`CPUSH`: it empties the LRU and does
`EPOCH++`. A unit found under an older epoch is **verified when it is next found** (`M68K_VerifyUnit`,
`M68k_Translator.c:769-860`: its recorded JIT settings, then a comparison of its source, with a quick path that ROM is
always valid) and only then re-enters the LRU with the new epoch. A flush is O(1) and the verification is paid per block
used, once per epoch. The source says it itself: "Could be more precise (but most of the time everything is probably
flushed anyway)" (`:1873`). When the 68k caches are off, `JITLoopWithCRC32` verifies by CRC32 at every entry
(`ExecutionLoop.c:685-735`).

That fits an Amiga, where flushes follow a load. **It does not fit a Mac as it stands**: Mac OS on a 68040 issues
`CPUSHA` hundreds of times a second ([68k JIT](jit-68k.md): all 8,000 of 8,000 in a boot are `F4F8`), each of which would
put every hot block back to "verify before use".

## What it asks of a port to Okapia

| Emu68 | Okapia | What it means |
|---|---|---|
| Big-endian AArch64 (`-mbig-endian`, `SCTLR_EL1.EE` set in `start.c:83`): a guest load is a plain `ldr` | Circle is little-endian, Mac memory is big-endian in RAM | One `rev` after every load and before every store (`rev`/`rev16`), a fixed cost of one instruction per access. `SCTLR_EL1.EE` cannot be used: it flips every EL1 data access, Circle's included |
| A guest address is a host address (`M68k_EA.c:41-45`) | The Mac's RAM is at `RAMBaseHost` | A base register, and a guest access as `ldr w, [xbase, w_addr, uxtw]`, one instruction as well; costs a register Emu68 does not spare |
| FP registers are doubles; "extended is emulated in load/store only" (`include/M68k.h:84`) | binary128, [patch 0008](../../project/decisions.md) | Either doubles in the JIT (a precision loss the project refused), or FPU instructions as helper calls into the binary128 core, correct and slow. K'Whet would stay slow until a JIT FPU exact enough exists |
| Illegal and F-line opcodes raise 68k exceptions | Basilisk's ROM patches use `0x71xx` (`EMUL_OP`) and A-line traps all the time | A translated helper call per `EMUL_OP` (state out, `EmulOp(&regs)`, state back: cheap with `D0-D7` in callee-saved registers) and an exit for every A-line trap |
| A flush = epoch bump, lazy verify | ~600 `CPUSHA` a second in a boot | A policy is needed: verify by CRC32 (a single instruction on AArch64, 8 bytes per step) only the blocks whose source *pages were written*, which needs write tracking, or accept the cost. Hypothesis: affordable; to measure |
| Guest memory written by the host never | Disk and network buffers are written into guest RAM by Okapia | Every such write must bump the epoch or be a range invalidation |
| No MMU, no fault inside a block | `DIRECT_ADDRESSING`, the same | Same, which is why this is the right comparison and pom68k was not |
| MPL-2.0, per file | GPLv3 | MPL-2.0 §3.3 lets MPL code be combined into a larger work under the GPL (these files carry no "incompatible with secondary licenses" notice); the MPL files keep their licence. Not legal advice |
| 4 cores with roles | everything on core 0 today | Interrupts as a flag word fit "record in the handler, act in the seam" ([Input](../../topics/input.md)) exactly |

## Route G: an Emu68-shaped JIT for Okapia

Not the translator copied, **the design**, in the order that settles each risk before the next costs anything:

0. **Measure what there is to beat.** Cycles per 68k instruction of the interpreter on the Pi 4 with the PMU
   ([Bottlenecks](bottlenecks.md) lists PMU counters as open), on the Speedometer integer loops. A ceiling check:
   if the interpreter already costs 8 cycles an instruction, a design at 2 to 3 is a gain of 3 to 4; if it costs 4,
   it is not worth the work. *Hypothesis: about 8; unmeasured.*
1. **A micro-translator in a few hundred lines**: register-to-register integer ops, `MOVE` with memory, `Bcc`, `BRA`,
   `BSR`/`RTS` with the compile-time return stack, one `EMUL_OP`; registers pinned as in Emu68; loop of block, lookup,
   call, flag check. Run **one synthetic loop** through it and through the interpreter, same Pi, and read the cycles.
   This is the experiment that says "worth it" or not, without a Mac in sight.
2. **Only then** the rest of the integer set, the `CPUSHA` policy, `EMUL_OP` and the A-line traps, and Basilisk's boot
   under QEMU with the same durability tests as every other change.
3. FPU last, as helper calls.

What is **not** tried here and would sit beside it: direct chaining between blocks (Emu68 has none, and its long traces
make it unnecessary), a PowerPC side (Emu68 has a translator; out of scope for now).

## Risks, in the order they would bite

- The `CPUSHA` rate: a correct answer exists (verify the written pages) but needs write tracking that no engine here has.
- Every guest memory access pays an extra `rev`; Emu68's big-endian trick is a real part of its speed that cannot be
  had on Circle.
- 14,000 lines of translator to read before trusting a copy of any of it, and an upstream that changes weekly and
  forbids, by its own rules, a JIT-core contribution without asking first: this would be a fork, not a patch.
- The FPU: K'Whet will not move until the FPU is solved, and the FPU is where a precision decision already cost a patch.

## References

- Emu68 `61a61f7`: `docs/internals/RegisterMapping.md`; `include/config.h:41-65`; `include/M68k.h:76-86`;
  `src/ExecutionLoop.c:96`, `:168-190`, `:258-340`, `:401-511`, `:628-735`, `:780`;
  `src/aarch64/RegisterAllocator64.c:60-143`; `src/aarch64/start.c:83`;
  `src/M68k_Translator.c:182-214`, `:234-276`, `:361-470`, `:769-860`; `src/M68k_LINE6.c:15-170`;
  `src/M68k_LINEF.c:1857-1897`; `src/M68k_EA.c:41-45`; `AGENTS.md`, `LICENSE`
- [68k JIT](jit-68k.md) (the measurements this is a reply to), [Bottlenecks](bottlenecks.md), [pom68k](pom68k.md),
  [Benchmarks](../../contributing/benchmarks.md)
