# Okapia — agent instructions

Bare-metal classic Macintosh on Raspberry Pi: Basilisk II (68k) and SheepShaver (PowerPC) ported to Circle, both in
one kernel image.

**The documentation in [`docs/`](docs/README.md) is the single source of truth**: decisions, architecture, every
subsystem with its status, pitfalls and verified facts with `file:line` references. Read
[Architecture](docs/project/architecture.md) once, then the topic page before changing anything in its area — its
*Pitfalls* section is what previous sessions paid for. Keep the page true in the same change as the code
([how](docs/contributing/guide.md#documentation)).

## Token budget

- No preamble, no restating the request, no summary of what you just did. Write nothing that wasn't asked for.
- Search (`rg -n 'pattern' path/`) before reading. Never read more than 500 lines without `offset`/`limit`.
- **Never open whole**: `codegen_x86.h`, `compstbl*`, `comptbl*`, `cpuemu*.cpp`, `cpustbl*`, `gencomp*.c`, any
  `*_arm*.cpp` in the JIT forks. Generated tables of 100 KB to 1.5 MB: grep them.

## Resource budget

Quadra-class on a small board, not a workstation.

- **No allocation** in the emulation loop, the compositor, the audio path or the frame path. Allocate at startup.
- The Mac RAM block is allocated **first**, before drivers. Everything else fits a 1 GB board. Kernel under 4 MB.
- No virtuals, `std::function` or exceptions in hot paths; precomputed tables over per-pixel work.
- Measure before optimising, but don't write obvious waste while waiting.

## Data safety

Outranks performance and features: when in doubt, lose speed, never data. Details: [Storage](docs/topics/storage.md).

- **No write-back cache between the guest and the card.** A change that adds one owes a flush policy and a test.
- **Every path that writes guest data survives an abrupt stop at any instruction** — prove it with
  `scripts/run-test.sh [seconds] [image]` before it lands.
- **Never mark a volume clean ourselves**; the repair runs the HFS scavenge.
- **`run-live.sh` sessions end with Special → Shut Down.** Anything else is a pulled plug.
- **Never run two emulators on the same card.** `run-test.sh` and `screenshot.sh` work on copies.
- Before reading a boot failure as a regression, rebuild the card (`scripts/make-sd-image.sh`) and look at the screen.

## Work locally

- `external/macemu/` (Basilisk II + SheepShaver, `kanjitalk755`), `external/circle-stdlib/` (newlib, libstdc++;
  **Circle is in `libs/circle/`**, docs in `libs/circle/doc/`), `external/hfsutils/` (libhfs).
- `reference/` — study material cloned on demand: `scripts/fetch-reference.sh` lists what exists. **Ask before
  cloning** one; don't read it on the web instead.
- **Never modify `external/` or `reference/`.** Upstream changes go to `patches/<submodule>/`, minimal and documented
  ([Build](docs/contributing/build.md#patches)); prefer a `--wrap` hook when it suffices.
- For a question about the Macintosh itself, read its history and public documentation before analysing code, and take
  the user's hints seriously ([guide](docs/contributing/guide.md#rules-of-work)).

## Settled decisions

Don't reopen without new evidence ([Decisions](docs/project/decisions.md)): 256 MB Mac RAM · `DIRECT_ADDRESSING` ·
`uae_cpu_2021` · `fpu_ieee` in IEEE binary128 · circle-stdlib `STDLIB_SUPPORT=3` · the `F1ACAD13` Quadra ROM for the
68k engine · a fixed output frame buffer and a compositor · network by sharing the Pi's MAC · no JIT · GPLv3.
Multicore (S1) is a goal, not implemented: everything runs on core 0 today.

## Cross-cutting pitfalls

- **Circle tracks no headers**: every Makefile rule compiles with `-MMD -MP`; a rule without them is a bug.
  ([Build](docs/contributing/build.md#pitfalls))
- **Objects do not depend on the Makefile**: after changing a flag or a `#define`, `make -C src/kernel okapia-clean`.
- **`ld -r` kills `--wrap`**: wraps go on each engine's partial link (`ENGINE_WRAPS`). Circle calls `ld` directly:
  bare `--wrap=<mangled>`, never `-Wl,`.
- **The output frame buffer is Device memory**: every write to it must be alignment-safe; keep `-O2`.
  ([Display](docs/topics/display.md))
- **Once or twice**: a hardware claim belongs to `hal_circle.cpp` (shared half), because `PLATFORM_SRCS` is compiled per
  engine; a symbol the PowerPC half references cannot be defined in the shared half (`ppc__` rename).
  ([Architecture](docs/project/architecture.md#what-exists-once-and-what-exists-twice))
- **The Macintosh goes round more than once**: before adding a registration, claim or `push_back` to a start path, ask
  what the fifth restart — and the other engine — does to it. ([Startup and shutdown](docs/topics/startup-and-shutdown.md))
- **Serial first**: a failed assertion in a member constructor is a silent hang; anything that can fail is built with
  `new` after the log.
- **Judge the boot by the screen** (`scripts/screenshot.sh`), never by opcode rates or buffer contents.
- **QEMU lies**: no sound output, no RTC, no EDID, video modes a Pi refuses, a window that costs a third of guest speed.
  Validate on hardware. ([Testing](docs/contributing/testing-and-debugging.md))
- **`config.h` declares, it never includes.**
- **Test against `qemu/sd-contents/boot71.img`**, not the 500 MB System 7.6 volume.
- **Never claim a device the guest cannot use**, and never delete a Circle sound device that may be playing.
  ([Sound](docs/topics/sound.md))
- **Nothing touches `adb.cpp` or the SD card from an interrupt**: record in the handler, act in the engine's seam.
  ([Input](docs/topics/input.md))

## Code

- **English everywhere**: code, comments, logs, documentation, commits.
- Follow each layer's convention: `CClassName`/`m_member` in Circle-side code, `snake_case` in Basilisk and SheepShaver
  code. Don't unify. Adapted upstream files carry their origin and modification list in the header.
- **No silent stubs**: a stub logs its call and fails cleanly if it matters.
- A fixed reproducible bug comes with a test, on the host where possible. Keep QEMU working.

## Git

- One commit per coherent change, `area: what it does`. Commit or push **only when asked**.
- **No ROM, no Apple system, no disk image** in the repository.
- Submodules pinned to a tag or SHA, never `master`/`main`/`latest`.

## Commands

```
./scripts/install-tools.sh            # host tools and the ARM toolchain (macOS)
./scripts/bootstrap.sh                # submodules, pinned, and patches
./scripts/build-libs.sh qemu|pi4      # circle-stdlib for one target; switching is a rename
. scripts/env.sh && $MAKE -C src/kernel               # kernel8.img, both engines
$MAKE -C src/kernel OKAPIA_TRACE=1                    # trace build (okapia-clean first)
make -C tests/host                    # host tests and the firmware specimen
./scripts/make-sd-image.sh            # qemu/sd.img from qemu/sd-contents/
./scripts/run-live.sh [seconds]       # QEMU window, monitor socket; end with Shut Down
./scripts/screenshot.sh [seconds]     # throwaway copy, capture the Mac's screen
./scripts/run-test.sh [seconds] [image]  # SIGKILL, repair, durability verdict
./scripts/specimen.sh [page|live]     # boot menu specimen, no card
./scripts/run-qemu.sh [kernel] -s -S  # bare kernel, GDB on :1234
./scripts/make-pi-sd.sh               # stage a Pi 4 card in rpi4-sd-contents/
```
