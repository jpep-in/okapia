# Okapia — agent instructions

Bare-metal classic Macintosh on Raspberry Pi: Basilisk II ported to Circle.

Single source of truth: **`planification.md`** — decisions, architecture, phases, verified facts with
`file:line` references. Use it instead of re-deriving or re-searching.

## Token budget

- No preamble, no restating the request, no summary of what you just did.
- Write nothing that wasn't asked for: no extra docs, no comments restating the code.
- `rg -n 'pattern' path/` before any `Read`. Never read >500 lines without `offset`/`limit`.
- **Never open whole**: `codegen_x86.h`, `compstbl*`, `comptbl*`, `cpuemu*.cpp`, `cpustbl*`, `gencomp*.c`,
  any `*_arm*.cpp` in the JIT forks. Generated tables, 100 KB to 1.5 MB. Grep them, never read them.

## Resource budget

Target is Quadra-class, not a workstation.

- **No allocation** (`new`, `malloc`, growing container) in the emulation loop, video compositor, audio
  callback or frame path. Allocate at startup.
- The 257 MB Mac RAM block is allocated **first**, before drivers: Circle's heap serves large blocks
  linearly from remaining space.
- No virtuals, no `std::function`, no exceptions in hot paths.
- Precomputed tables over per-pixel work (`ExpandMap[256]` is the model). Process video by row or tile.
- Everything outside the 257 MB must fit a 1 GB board. Kernel < 4 MB.
- Measure before optimising — but don't write obvious waste while waiting.

## Work locally

After `scripts/bootstrap.sh`, no network needed:

- `external/macemu/` — Basilisk II + SheepShaver (`kanjitalk755` upstream)
- `external/circle-stdlib/` — newlib + libstdc++; **Circle lives in `libs/circle/`**, docs in
  `libs/circle/doc/` (`qemu.txt`, `multicore.txt`, `memorymap.txt`, `stdlib-support.txt`, `issues.txt`)
- `reference/` — study material, **cloned on demand, not by default**. `scripts/fetch-reference.sh` with no
  argument lists what exists and what is local. If you need one that isn't there, **ask before cloning** —
  don't go read it on the web instead.

**Never modify `external/` or `reference/`.** Upstream changes go to `patches/macemu/`, minimal, documented.

## Invariants

Settled (`planification.md` §2). Don't reopen without new evidence: 256 MB Mac RAM · `DIRECT_ADDRESSING` ·
`uae_cpu_2021` · `fpu_uae` · circle-stdlib `STDLIB_SUPPORT=3` · Quadra 650 ROM · fixed output framebuffer +
compositor · multicore S1 · network by sharing the Pi's MAC · no JIT · GPLv3.

## Pitfalls

- **IRQs run on core 0** only, and so does the cooperative scheduler. Hence S1: emulation on a secondary core.
- **Heap is not executable**: Circle sets `PXN=1` past `_etext`. Blocks any JIT.
- **Kernel size**: 2 MB default in Circle, 4 MB via circle-stdlib `--kernel-max-size`. Overflow shows as an
  obscure link error.
- **Pi 5**: display resolution **cannot be set by the application**, and `config.txt` won't configure it.
  Never assume a mode was granted — print what the firmware actually returned.
- **Pi 1/2/3**: Ethernet is on USB and starves keyboard/mouse under sustained traffic.
- **`uae_cpu_2021`, not `uae_cpu`** — that's what macemu builds on AArch64.
- **One MAC address**: `CNetDevice` has no promiscuous mode.
- **QEMU lies**: `raspi3b` accepts 8 bpp + palette that the Pi 5 refuses. Validate on hardware.
- **`gencpu`/`gencomp`** are built **for the host** and run during the build.

## Code

- French for planning docs; **English for code, comments, log messages and the public README**.
- Follow each layer's convention: `CClassName`/`m_member` in Circle code, `snake_case` in Basilisk code.
  Don't unify.
- Adapted upstream files carry their origin and modification list in the header.
- **No silent stubs**: a stub logs its call and fails cleanly if it matters.
- A fixed reproducible bug comes with a test, on the host where possible. Keep QEMU working.

## Git

- One commit per coherent change. Commit or push **only when asked**.
- **No ROM, no Apple system, no disk image** in the repo. `roms/` is ignored.
- Submodules pinned to a SHA. Never `master`/`main`/`latest`.

## Commands

<!-- filled in at bootstrap; don't invent commands that don't exist yet -->

```
./scripts/bootstrap.sh      # tools, submodules, references
./scripts/build-qemu.sh     # AArch64 kernel for QEMU raspi3b
./scripts/run-qemu.sh       # serial on stdout, GDB on :1234
./scripts/build-pi.sh 4     # kernel for Pi 3, 4 or 5
```
