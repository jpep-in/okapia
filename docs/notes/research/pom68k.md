# pom68k and Okapia

> Feasibility study, 2026-11-13. Question: thinking Okapia from scratch, would `habib256/pom68k` be a better 68k
> engine than Basilisk II, now that it has an AArch64 JIT? Source: pom68k at commit `17431b4` (main, 2026-11-10),
> read as an archive; `scripts/fetch-reference.sh pom68k` clones it into `reference/`. **Nothing decided**: the
> decisive number is a measurement on our Pi 4 that nobody has taken yet ([below](#the-measurement-that-decides)).

## Verdict

1. **It can be built for Circle.** Nothing blocks: Circle compiles with exceptions at our `STDLIB_SUPPORT=3`, the
   JIT's executable memory is a recipe we already have, and pom68k's emulation core is a library without its GUI.
   It is weeks of work on a project that changes every day.
2. **As the Quadra engine it would be slower than today, JIT included**: projected at 1.2–2.4× a Quadra 605 in real
   time on a Pi 4, against a measured 5.22× a Quadra 650 for Okapia. A projection from pom68k's own Apple M4 figures,
   not a measurement.
3. **What it brings is fidelity**, not speed: unpatched ROMs on emulated chips, the 68040 MMU, 58 machines.
4. **Where it would fit is beside Basilisk, not instead of it**: the compact 68000 Macintoshes, which no Basilisk
   configuration can start. A hypothesis to measure.

## What pom68k is

A hosted emulator (Linux, macOS, Windows, WebAssembly; GLFW, OpenGL, Dear ImGui), GPLv3, created 2026-07-17, about
980 commits by one author, heavily AI-assisted. **Low-level**: each machine runs its own unpatched ROM over emulated
chips (VIA, ASC, DAFB, 53C96, IWM/SWIM, Egret/Cuda firmware when supplied), 58 profiles from the 128K to the
Quadra 950 (`src/MachineCatalog.h`). CPU: Moira (MIT) with an 80-bit softfloat 68882 (GPLv2+), plus a JIT with
x86-64 and AArch64 code generators (`src/jit/`, `JitBackendA64.cpp` 5,153 lines). Our `F1ACAD13` ROM is recognised,
as the Centris 650 / Quadra 650 / 800 board (`src/MachineFactory.cpp:137`).

Basilisk II is the opposite design: one 32-bit-clean ROM, patched; Apple's drivers replaced by its own (disk,
video, network); no MMU (`DIRECT_ADDRESSING`); no cycle timing.

## Speed

### The figures pom68k publishes

Apple M4, Quadra 605, fixed budget of 3,000 frames = 1.25 G machine cycles, so 50 s of a 25 MHz machine
(`src/jit/POM68K_JIT.md:361-369`):

| Engine | Wall time | × real time |
|---|---:|---:|
| Moira interpreter | 29.24 s | 1.71 |
| Threaded (interpreter behind a fetch window) | 15.13 s | 3.30 |
| AArch64 code generator | 3.24 s | 15.43 |

pom68k itself warns that these August figures are no longer reproducible after later corrections
(`POM68K_JIT.md:12`), that they were taken without LTO, and that **pom68k has never been measured on a Pi**
(`docs/RASPBERRY_PI.md:125`).

### Projected on a Pi 4

The scale is ours: the same Basilisk interpreter runs 6.34× faster on the M4 than on our Pi 4 for Speedometer's
CPU score, 8.25× for the integer median, and Geekbench single-core gives 12.45×
([Benchmarks](../../contributing/benchmarks.md#raspberry-pi-4-against-the-macbook-pro-m4)).

| On a Pi 4 | × a Quadra 605 in real time |
|---|---:|
| pom68k, interpreter | 0.14–0.27 — slower than the machine it emulates |
| pom68k, AArch64 JIT | 1.2–2.4 |
| **Okapia today, interpreter, measured** | **5.22× a Quadra 650 on Speedometer CPU** — a 33 MHz 68040, faster than the 605 |

Different metrics (a fixed cycle budget against Speedometer), so an order of magnitude: Okapia stays at least about
twice as fast, probably more. PGO, which pom68k measures at −18 % on its JIT on x86-64 (`docs/RASPBERRY_PI.md:109`),
does not close it.

### Why its JIT is not a turbo for us

pom68k's JIT is fast because it buys back costs that Basilisk never pays. Its own table gives the 68040 gain as
"an ATC walk per fetch, replaced by a bounds check" (`POM68K_JIT.md:102`) — the MMU translation that
`DIRECT_ADDRESSING` does not have — and it refuses the usual JIT relaxations (coarse time and interrupts, a big
soft TLB, lazy flags) to stay cycle-exact: "their exactness is why the wins are bounded" (`POM68K_JIT.md:33`). On a
68020 or 68000 guest, with no MMU, it gains ×1.0–1.2 at most. Its IR is built on Moira's handlers and timing
contract, so it cannot be grafted onto `uae_cpu_2021`. For a JIT on Basilisk, [68k JIT](jit-68k.md) stands.

## Porting surface

Counted in the sources of `pom68k_core` (`CMakeLists.txt:391`, 92 `.cpp` files, about 36,000 lines plus headers,
Moira and the JIT):

| Need | In the core | On Circle |
|---|---|---|
| C++ exceptions | 56 `throw` | Available: Circle's `Rules.mk:188` adds `-fno-exceptions` only below `STDLIB_SUPPORT=3` |
| Threads | 1 `std::thread` (the machine thread, `MachineHost.h:378`), 18 mutexes | Replaced by our single loop |
| Files | 19 `std::filesystem`, 48 `fopen`/streams | A layer over FatFs, and the [Storage](../../topics/storage.md) durability work redone on its SCSI and ATA paths |
| JIT code memory | `src/jit/JitCodeBuffer.cpp:122-134` (`mmap`, `MAP_JIT`) | A 64 KB-aligned arena with `PXN` cleared ([68k JIT](jit-68k.md#executable-memory-is-reachable)) |
| Allocation | `std::vector`/`std::string`/maps 2,232 times in `src/` | To audit against the no-allocation rule in the loop |
| Size | 8.7 MB AArch64 binary, GUI included | `KERNEL_MAX_SIZE` is 4 MB for both engines; `kernel8.img` is 2.8 MB today |

## What it would bring, what it would cost

**Brings**: the real hardware under the real ROM — the 68040 MMU, absent from Basilisk's direct addressing, and the
software that needs one; the ASC sound chip, SCSI with Apple's own drivers, floppies including protected ones, and
software that touches the hardware directly; 58 machines; in-process AppleTalk.

**Costs**: speed (above); Basilisk's free resolutions — Okapia offers up to 2560×1440 ([Display](../../topics/display.md))
where a real card offers only its own modes; the durability work, which today rests on Basilisk's disk layer; a
moving upstream to follow through `patches/` (we never modify `external/`). The PowerPC half stays SheepShaver in
any case.

## The 68000 angle

Basilisk II accepts only a `$067C` ROM ([Decisions](../../project/decisions.md)), so no Basilisk configuration
starts a 128K, Plus, SE or Classic. [System 6 in 24-bit](system6-24bit.md) covers System 6 on a IIci; the compact machines stay
out of reach. pom68k emulates them cycle-exact, and there its own cost is lowest: a 68000 at 8 MHz with no MMU. Real
time on a Pi 4 is plausible but **unmeasured**. A third engine for the compacts is the one place where pom68k adds
something Okapia cannot get otherwise.

## The measurement that decides

Before any port: pom68k's AArch64 AppImage on our Pi 4 under Raspberry Pi OS.

1. Profile `q650`, ROM `F1ACAD13`, a System 7.5 volume, Speedometer 4.0.2, the method of
   [Benchmarks](../../contributing/benchmarks.md) — directly comparable with Okapia's 5.22×.
2. Same with `POM68K_CPU_ENGINE=interp`, to separate JIT and interpreter.
3. Profile `plus`, System 6, to check real time on a compact.

Reopen the 68k engine only if step 1 approaches Okapia; pursue the compact engine if step 3 holds real time.

## Reusable without a port

- `docs/BASILISK_ROM_NOTES.md`: what the `$067C` ROM expects from the machine, its § 8 checked on real ROMs, the
  rest a reading of Basilisk's sources.
- Its 80-bit softfloat FPU with the 68040 FPSP transcendentals, as a host-side differential oracle for our
  binary128 `fpu_ieee`.
- `oracle/`: differential fuzzing of the CPU against WinUAE, a method to copy.
- Its AppleTalk stack (netatalk2, tashrouter, macipgw), when [Network](../../topics/network.md) is written.

## References

- pom68k `17431b4`: `src/jit/POM68K_JIT.md:12`, `:33`, `:102`, `:361-369`; `docs/RASPBERRY_PI.md:109`, `:125`;
  `CMakeLists.txt:391`; `src/MachineFactory.cpp:137`; `src/MachineHost.h:378`; `src/jit/JitCodeBuffer.cpp:122-134`
- Circle `Rules.mk:188` (in `external/circle-stdlib/libs/circle/`)
- [Benchmarks](../../contributing/benchmarks.md), [68k JIT](jit-68k.md), [System 6 in 24-bit](system6-24bit.md)
