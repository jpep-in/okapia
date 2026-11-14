# 05 — Next: open questions, routes, the cheapest experiments first

## Where the gap is

The interpreter already costs, in my estimate, **about 8 cycles per 68k instruction** (1.5 GHz Cortex-A72, Speedometer CPU
about 5× a Quadra 650 — *unmeasured*: no PMU counters have been read). A JIT shaped like Emu68 would cost 2 to 3, a gain of 3 to 4×;
the fork's JIT, as ported, costs about what the interpreter costs. Everything below is about finding out whether that gap is real
before building for it.

## Open questions (each has a cheap first step)

| Question | First step | Expected cost |
|---|---|---|
| What does the interpreter cost per 68k instruction on the Pi 4? | Read the PMU (cycles, instructions retired, branch misses, L1D/L1I misses) over a Speedometer integer loop; the PMU counters are listed as open in `docs/notes/research/bottlenecks.md` | a few hours; **the most useful missing number** |
| Why is K'Whet ten times slower under the JIT whatever is switched (FPU compiled or not)? | Count calls to the FPU semantic service (`op_fpu_semantic_comp_ff`, `compemu_support_arm.cpp:5239`) and the cycles around one; compare with the interpreter's `fpuop_arithmetic`; check whether `uae_cpu_2026/fpu/fpu_ieee.cpp` (rewritten: +12/−103 lines against 2021) is slower than 2021's for the *same* instruction | half a day |
| Where does the time go with the JIT: native code, dispatcher, compiler? | Sample the PC from the timer tick (needs the interrupted PC, which Circle's periodic handlers do not get) or count inside generated code | open; hard on Circle |
| Does lazy flush invalidate ROM blocks on every `CPUSHA`? (`03` finding 7) | Count blocks with `csi == NULL` that pass through `flush_icache_lazy` and then `block_check_checksum` | an hour with the probe style used before |
| Are the pauses compile bursts? | Log `compile_block` time (clock ticks) with the 5-second line; histogram the worst | an hour |
| Is constant-jump following safe enough to keep? | Boots of 7.1, 7.5, 7.6.1, 8.x under QEMU with `OKAPIA_JIT_CONSTJUMP`; the fork's divergence was in the ROM A-trap path at `0401bfcc` → `0401ba28` | half a day; the user does the hardware runs |
| Does a restart of the Macintosh work with the JIT? (`Exit680x0`, `compiler_exit`, `vm_release`, `tick_inhibit`) | Boot, Special → Restart five times under QEMU, then the Pi | an hour |

## Routes, ranked for the user's goal ("accelerate the 68k out of all proportion")

1. **Route G — an Emu68-shaped JIT** (full reasoning in `docs/notes/research/emu68.md`). Copy the *design*: registers pinned in
   AArch64 registers for good, 256-instruction traces along predicted branches with side exits, calls and returns inlined through a
   compile-time return stack, host flags, no chaining, a loop that checks one interrupt word. Port costs known: a `rev` per guest
   access, a base register, FPU as helper calls into binary128, `EMUL_OP` as a helper call, a `CPUSHA` policy (verify only pages
   written), epoch bump on host writes into guest RAM, MPL-2.0 files under a GPLv3 whole (§3.3).
   - **Experiment 0**: the PMU measurement above.
   - **Experiment 1**: a micro-translator of a few hundred lines (register ops, `MOVE` with memory, `Bcc`, `BRA`, `BSR`/`RTS`,
     one `EMUL_OP`), a synthetic 68k loop through it and through the interpreter on the same Pi, read the cycles. A "no" here costs a
     day, not a month. Build it as a Circle kernel of its own or inside the Okapia kernel behind a flag; it needs no Mac.
2. **Route D — the UAE4ARM/Amiberry glue on our core**: ARAnyM's generator (our core has its sockets) + the fork's/Amiberry's
   AArch64 emitter with Amiberry's lighter glue (`src/jit/arm/compemu_support_arm.cpp`, 119 KB, against the fork's 333 KB) instead
   of the fork's "contract-first" glue. Plausible gain: removing the fork's strictness; ceiling lower than route G because the
   per-block register reload remains. Cost: 21 midfuncs ARAnyM's ARM path lacks, 32-bit assumptions in it, validation ours.
3. **Route F — a small tiered JIT of our own** (PreviousTurbo is the worked example, no figures): blocks recorded as lists of
   interpreter handlers, hot ones compiled for the commonest integer instructions, the rest through the interpreter. Fits the
   budgets by construction; the gain is only what the design avoids in boundaries, so design it like route G or do not.
4. **Stop**: keep `OKAPIA_JIT=1` as a documented experiment (it is already).

## If you only have a day

Do **Experiment 0** (the PMU), then decide. If the interpreter is at ~8 cycles per instruction, start Experiment 1; if it is at
~4, a JIT is not worth its risk on this board and the work should go to the PowerPC engine (0.88× a Power Mac 6100/60) or elsewhere.

## Housekeeping owed

- Commit, **when the user asks**, in coherent pieces: (a) kernel reservation 16 MB (`build-libs.sh`, docs); (b) `external/macemu-jit`
  + `patches/macemu-jit` + `bootstrap.sh`; (c) `OKAPIA_JIT` build (`Makefile`, `gen-cpu.sh`, `jit_circle.*`, `execmem_circle.*`,
  `kernel.cpp`, `main_circle.cpp`, `tick_circle.cpp`); (d) scripts (`make-pi-sd.sh`, `run-test.sh`, `fetch-reference.sh`); (e) docs
  and `jit-wip/`. Much else in the working tree predates this work and belongs to other commits.
- Remove `scripts/jit-gate-pi.sh` (obsolete) or keep it as the Linux gate; the study says the gate was skipped by decision.
- Silence the fork's diagnostics (`RTE_BAD`, `M68K_SETPC_BAD`, `NOJIT_DIAG`) with a patch, or the serial port stays unreadable.
- Carry `patches/macemu` 0009 and 0010 to `uae_cpu_2026` if the JIT core is ever to be the interpreter's too.
- Update `docs/contributing/benchmarks.md` with a JIT section if the numbers are to be kept there (they are in `jit-68k.md` now).
- Move the volume backups out of `/private/tmp` (see `04-howto.md`).
