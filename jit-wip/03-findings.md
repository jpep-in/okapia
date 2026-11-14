# 03 — Findings: causes, fixes, and what was retracted

`file:line` are in `external/macemu-jit/BasiliskII/src/uae_cpu_2026/` unless stated, **as read at the pinned commit `ba4a541`,
before** patches 0003/0004: in the patched tree `compemu_support_arm.cpp` lines after 2219 move by +1 and after 4641 by about
+15 (0004 adds a forward declaration, the `CINV`/`CPUSH` scope code and `flush_icache_range_checked`); always `grep` the symbol. The fork's own docs live in `reference/macemu-jit/` (`JIT-STATUS.md`,
`BasiliskII/docs/AARCH64_JIT_*.md`, 160-odd audit files).

## Findings about the platform (Circle) — solved

- **Circle marks the heap non-executable** (`lib/translationtable64.cpp:135`, PXN=1 past `_etext`) but **`SCTLR_EL1.WXN` is
  clear** (`lib/memory64.cpp:220`): clearing PXN on 64 KB pages of an allocation makes it RWX. Recipe, in
  `src/circle/execmem_circle.cpp`: level-2 entries are table descriptors (`translationtable64.cpp:80-87`), PXN is bit 53
  (`armv8mmu.h:100-130`), index `addr >> 29` then `(addr >> 16) & 8191`, then `dsb ish; tlbi vaae1is; dsb ish; isb`. Circle's
  `memalign` refuses alignments above its block size (`alloc.cpp:33`): `malloc` more and align inside.
- The JIT needs no `mmap`/`SIGSEGV`/`BRK` on Circle (those are debug aids behind `JIT_DEBUG_MEM_CORRUPTION`, defined nowhere).
- The fork includes `<sys/mman.h>` and `<SDL2/SDL.h>` unguarded (`compemu_support_arm.cpp:35`, `:65`): empty stand-ins
  suffice. `jit_prefs_bootstrap` fills `currprefs` in a **static constructor**, before our prefs file is read;
  `check_prefs_changed_comp(false)` re-reads it before the tables are built (`compemu_support.cpp:84-91`), so `jitfpu`,
  `jitlazyflush`, `jitcachesize` do take effect.
- The core sleeps in the 68k STOP path (`SleepAndWait` = `usleep(1000)`, `newcpu.cpp:2083`; the old core had it `#if 0`):
  with the JIT's tick taken from the timer interrupt, a STOPped Mac would wait for a tick nobody sends. `jit_circle.cpp`
  defines `usleep` to run the tick by the clock.
- **Arena exhaustion kills the Mac**: `LazyBlockAllocator` takes 16 KB pools through `vm_acquire` while the Mac runs; when
  the arena is full the core aborts with `Could not allocate block pool!`. With the cache working (patch 0003) the live
  blocks' descriptors outgrow a 32 MB margin in ~230 s; 256 MB of margin settles near 100 MB used.

## Findings about the fork's JIT (the decisive ones)

1. **Ordinary mode never compiled RAM code natively** (patch **0003**). `compile_block()` (`compemu_support_arm.cpp:7751-7756`)
   gives a RAM block an interpreter-dispatch first generation (`bi->count = 9`, "will escalate on the next count expiry");
   the branch tests `optlev == 0`, a block whose countdown ran out is still at level 0, so the same branch runs again, for
   ever; the escalating branch (`optlev < max_optlev`) is unreachable from level 0. Measured: 187 s, **220,509** first
   compilations, **6,685,749** recompilations, 6.3 million at level 0 and 30,220 above. Each rebuild emits a new stub, so the
   cache filled and was flushed every few seconds. **Strict mode** (`B2_JIT_STRICT_FULL`, opt-in) compiles RAM at the top
   level directly: that is the path the fork's gates and published figures are about, which is why it never published an
   ordinary-mode number. Fix: `else if (optlev == 0 && bi_was_invalid)`. CPU 0.68 → 6.40.
2. **A guest cache flush ignores its range** (patch **0004**): `flush_icache_range(start, size)` called the whole-cache flush
   (`compemu_support.cpp:52-57`). Now only blocks whose source spans overlap the range go back to "needs checking". Correct,
   kept, **no measurable effect**, because Mac OS issues only `CPUSHA` (whole cache) and `FlushCodeCache` only accompanies it.
3. **Constant-jump following is forced off on AArch64** (`compemu_prefs.cpp:63-75`): `comp_constjump` is 1 only with
   `B2_JIT_ENABLE_CONSTJUMP=1`, because inlining "produced real opt2 divergence … and corrupted downstream heap history" in the
   ROM A-trap path. **The `jitinline` preference is inert**, whatever its value; every run before `OKAPIA_JIT_CONSTJUMP` had it
   off. Enabled (QEMU System 7.1, Pi 7.6.1): blocks 3.5 → 4.5 instructions, Finder normal, durability unchanged, a few percent
   faster; nothing wrong seen, which proves little (one boot, one Speedometer run).
4. **The JIT FPU is off on AArch64** by the fork (`configure.ac:1811-1814`, "not fully wired yet"; its QA runs `fpu false`).
   `OKAPIA_JIT_FPU=1` compiles and links, and the JIT then keeps the eight FP registers as **host doubles** (`jit_fpregs`) and
   synchronises by conversion (`compemu_legacy_arm64_compat.cpp:2475-2480`, `:2531-2535`), so values through compiled FPU code
   are rounded to 53 bits of mantissa — the loss `patches/macemu/0008` exists to avoid. **It changed nothing in K'Whet.**
5. **With the FPU off, FPU opcodes are not a block break and not an interpreter fallback**: they compile to a native call of a
   "semantic service" (`op_fpu_semantic_comp_ff`, `compemu_support_arm.cpp:7250`).
6. **What a block boundary costs** (read from the emitters, not measured). Chaining exists: a block with a known successor jumps
   to it (`compemu_raw_endblock_pc_isconst`, `codegen_arm64.cpp:885`, target patched by `write_jmp_target`). By default
   (`jit_prefer_validated_successor_handler`, `compemu_support_arm.cpp:1746`) the jump lands on the successor's *handler*
   (`get_handler_for_edge`, `:5939`): compare `regs.pc_p` with the block's address and branch to `popall_cache_miss` on a
   mismatch (`compemu_raw_cmp_pc`, `compemu_raw_maybe_cachemiss`, `codegen_arm64.cpp:348`, `:788`); a tag-collision guard, no
   checksum. The checksum runs only when the target is `BI_NEED_CHECK` (`set_dhtu_validated`, `:2813`). Each block's epilogue:
   the countdown in memory through an absolute 64-bit address, a full PC snapshot of three stores (`pc_p`, `pc_oldp`, `pc`,
   `compemu_raw_set_pc_full_from_reg`, `codegen_arm64.cpp:366`, "required before any exit to C"), a read of `regs.spcflags`, and
   the guest registers reloaded from and written back to memory around the block (`flush(1)`, `compemu_support_arm.cpp:8848`).
   Some twenty instructions and several memory accesses a boundary, over 3.5 guest instructions on average.
7. **Lazy flush and ROM blocks (suspected, not checked)**: `block_check_checksum` sets `isgood = bi->csi != NULL`
   (`compemu_support_arm.cpp:6573` in the patched tree, about 15 lines earlier at the pinned commit) and ROM blocks "deliberately have no chain" (the comment right above it). If
   `flush_icache_lazy` marks ROM blocks `BI_NEED_CHECK`, each `CPUSHA` would invalidate them and force recompilation. Counters
   show `bad` small (30,495 in 187 s), which argues against a large effect, but nobody read it through. Worth a look before
   concluding anything about lazy flush.
8. **The fork's two bugs that aim at us** (its own docs): lazy invalidation reusing stale blocks (`jitlazyflush` default false
   for that reason; the 2026-09-06 review fixed an order-blind checksum and five width bugs in no-flags constant folds, and the
   **same two bugs are still in `uae_cpu_2021` and in Amiberry**), and cache-tag aliasing at 64 MB of guest RAM (`TAGMASK 0x3ffff`
   now).

## Findings about the guest

- Mac OS 7.1/7.6.1 on the 68040 ROM executes **only `CPUSHA`** (`F4F8`) for cache control; ~600–1,250 a second while booting,
  20–70 in steady state.
- Average **3.5 guest instructions per translated block** (4.5 with constant jumps).

## Statements I made and later withdrew — do not rely on them

The conversation is the source of these; they are the traps for a reader who sees older notes or messages.

| Said | Wrong because |
|---|---|
| "The flush storm is the main cost" (hard flush destroys all translations at ~700 flushes/s) | Real but not decisive: lazy flush and a bigger cache changed nothing; the decisive defect was finding 1 |
| "Writes by the guest invalidate blocks and recompile them" (written into `jit-68k.md`, since corrected) | `jit_notify_guest_memory_write` returns at once outside strict mode (`compemu_support_arm.cpp:2985`) |
| "Without direct chaining, every transition calls a handler that revalidates" | Chaining exists; the cost is the epilogue and the handler's PC compare (finding 6); no checksum on the hot path |
| "Every FPU instruction ends its block and goes through the interpreter" | With the FPU off it is a native semantic-service call inside the block (finding 5); K'Whet is slow for a reason **not found** |
| "`jitinline true` lengthens blocks" / "does not lengthen them under QEMU" | The preference is inert on AArch64 (finding 3); both statements were about an inert switch |
| "The cache size was the main problem" | A real defect of the setup (28 flushes in 81 s at 16 MB) but not the main one; arena sizing mattered for the crash, not the speed |
| "The 4 MB kernel reservation is a settled decision" | Dropped by the user: no cost was ever stated; now 16 MB |

## Facts about the other candidates (from the survey; see `docs/notes/research/jit-68k.md`)

- **Amiberry / uae4arm-rpi**: the origin of the fork's AArch64 emitter and midfuncs (298 `DECLARE_MIDFUNC`, identical sets); its
  glue file is 119 KB against the fork's 333 KB. A public issue (Amiberry #2315) measures 1,230–2,100 SysInfo MIPS in 68040 on a
  Pi 500+ (Cortex-A76) — Amiga workloads, JIT only, not comparable to Speedometer.
- **ARAnyM**: the ancestor of `uae_cpu_2021`'s compiler (126 lines of difference in `compemu_support.cpp`); its ARM JIT2 is
  32-bit and experimental; our core still has its `CPU_arm` sockets. **Not tried.**
- **Emu68**: see `docs/notes/research/emu68.md`. **PreviousTurbo** (a NeXT emulator, a small tiered JIT on a UAE core,
  x86-64 and AArch64, created 2026-10-22): a model for a tiered design, no figures.
- **pom68k**'s JIT is bound to the Moira core and cycle exactness; not a candidate for Basilisk.
