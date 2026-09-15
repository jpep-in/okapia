# A 68k JIT for Okapia

> Feasibility study of 2026-08-29, revised on 2026-08-30 after two rounds of cross-checking between two independent
> studies, every objection replayed against the code. Sources: Circle, Basilisk II (`uae_cpu_2021`) and
> `reference/macemu-jit` (HEAD `3e79929`, 2026-08-03). **Decision: no JIT** — see
> [Decisions](../../project/decisions.md). Facts that changed since are marked *(since)*.

## Verdict

The JIT does not stumble on Circle: the non-executable heap can be lifted from our own code, and the feared platform
dependencies (`mprotect`, `SIGSEGV`, `BRK` single-stepping) do not exist in normal operation. Three real obstacles
remain:

1. **The backend.** No 68k→AArch64 backend exists upstream. The only one, `rcarmo/macemu-jit`, is correct, but on the
   only end-to-end measurement its authors publish it is **about twice as slow as the interpreter**.
2. **The kernel address budget.** The JIT adds ~4.5–5 MB of static tables in `.bss`, plus the code arena, against a
   4 MB `KERNEL_MAX_SIZE` checked by a silent halt at startup.
3. **Allocation in the loop.** The JIT runtime allocates during emulation, which the resource budget forbids, and no
   configuration changes that.

**Recommendation**: don't port a JIT. Measure the interpreter on hardware first *(since: done — rev2 gives 5.22× a
Quadra 650 on CPU, see [Benchmarks](../../contributing/benchmarks.md))*; reopen only if `macemu-jit` publishes an
end-to-end gain or a measured need appears.

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
| **A** — adopt `uae_cpu_2026` as a whole, through `patches/macemu/` | core and backend, plus Circle adaptation | the only credible port; inherits a 2× slower end to end, an instrumented core and their FPU configuration |
| **B** — graft the backend onto our core | backend plus generator, `compemu.h`, support and tables | **tried upstream and failed**: "generator/backend API mismatch … now the primary blocker" (`AMIBERRY_ARM_JIT_PORT_PLAN.md:35-45`) |
| **C** — don't port | executable-memory spot check and hardware measurements | chosen |

If reopened: (1) the executable-memory spot check — clear `PXN` on a page, write a few AArch64 instructions,
`__builtin___clear_cache`, jump — under QEMU then on hardware; (2) a **Linux AArch64 gate** before any Circle work —
build the reference on a Pi under Linux with `ramsize` 256 MB, `modelid 14`, `cpu 4`, `fpu true`, and pass 904/904,
33/33 and a timed Finder against the interpreter; a failure closes the subject; (3) never compiled in by default nor
enabled by default.

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
