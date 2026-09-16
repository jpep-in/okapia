# Build

> From a fresh macOS machine to `kernel8.img`: tools, dependencies, targets, the merged image, and the patches
> Okapia applies to its upstreams. Main files: `scripts/install-tools.sh`, `scripts/bootstrap.sh`,
> `scripts/build-libs.sh`, `scripts/target.sh`, `scripts/env.sh`, `scripts/apply-patches.sh`, `src/kernel/Makefile`.

## Using it

```bash
./scripts/install-tools.sh      # Homebrew packages and the ARM toolchain
./scripts/bootstrap.sh          # submodules, pinned, and the patches
./scripts/build-libs.sh qemu    # or pi4 (pi3, pi5): circle-stdlib for one board
. scripts/env.sh && $MAKE -C src/kernel
```

The result is `src/kernel/kernel8.img`, about 2,840 KB, carrying both Macintosh. Then:

- **QEMU**: `./scripts/make-sd-image.sh` and `./scripts/run-live.sh` — see
  [Testing and debugging](testing-and-debugging.md).
- **A Pi 4**: `./scripts/build-libs.sh pi4`, build, then `./scripts/make-pi-sd.sh` stages the firmware, device tree,
  ARM stub, `config.txt` and the kernel in `rpi4-sd-contents/` — see [Getting started](../getting-started.md).
- **Host tests**: `make -C tests/host`.

## How it works

### Tools (macOS)

`install-tools.sh` is idempotent. It installs `qemu`, `make` 4.x (Apple's is 3.81; Homebrew's installs as `gmake`,
which `env.sh` exports as `$MAKE`), `dtc` and `gnu-getopt` with Homebrew, and the **ARM GNU 15.2.Rel1
`aarch64-none-elf`** toolchain from ARM — the version Circle and circle-stdlib are tested with, not Homebrew's
`aarch64-elf-gcc`. `ccache` is used when present: rebuilding the UAE core is long.

`scripts/env.sh` puts the toolchain and GNU getopt on `PATH` and sets `TOOLCHAIN_PREFIX`; source it before `make`.

### Dependencies

| Submodule | Upstream | Pinned to | Notes |
|---|---|---|---|
| `external/circle-stdlib` | `codeberg.org/larchcone/circle-stdlib` (GitHub is a read-only mirror) | a **tag** (`v20`) | newlib and libstdc++; **Circle lives in `libs/circle/`**, pinned by circle-stdlib, with its docs in `libs/circle/doc/` |
| `external/macemu` | `github.com/kanjitalk755/macemu` | a **SHA** (upstream's last tags date from 2017) | Basilisk II and SheepShaver. Not `emaculation/macemu`, dormant since April 2022 |
| `external/hfsutils` | `github.com/JotaRandom/hfsutils` | a SHA | libhfs, GPL v2 or later |

Never `git submodule update --remote`: it moves pins blindly. `scripts/check-upstreams.sh` (the one script that needs
the network, read-only) shows each pin, its date and tag, and the upstream commits not taken.

**Moving a pin** — never together with another change:

1. read the upstream commits (`check-upstreams.sh` lists them);
2. check out the tag or SHA in the submodule;
3. re-apply the patches — **one that no longer applies is the most useful information of the operation**; settle it
   first;
4. build for QEMU, run the host tests, boot Mac OS under QEMU, validate on hardware, compare with the
   [benchmarks](benchmarks.md);
5. a dedicated commit whose message says **why**.

Cadence: on need, or just before a milestone; never a comfort upgrade mid-feature.

**References** (`reference/`, ignored by git) are cloned on demand, shallow, by `scripts/fetch-reference.sh`; with no
argument it lists what exists and what is local. Ask before cloning one. Never build from them, never modify them.

### One tree, one target

circle-stdlib configures newlib and Circle for a single board: newlib records its CFLAGS in an autoconf cache,
Circle's objects carry `-mcpu` and `-DRASPPI`, and so do ours. Reconfiguring over the top fails halfway ("changes in
the environment can compromise the build"). So `build-libs.sh <target>` **moves the current target's build aside**
into `build/libs/<target>/` and brings the requested one back: the first build of a target costs what it costs,
every later switch is a rename. `scripts/target.sh` reads `libs/circle/Config.mk` to say which target the tree is
built for, and every script that runs a kernel checks it — a Pi 4 kernel addresses peripherals at `0xFE000000`, QEMU's
`raspi3b` puts them at `0x3F000000`, so the wrong build does not fail, it goes quiet. A QEMU build also carries
`NO_SDHOST` and dies silently on a board.

circle-stdlib's `configure` is run with `--kernel-max-size` 4 MB (Circle alone reserves 2 MB); newlib is built with
`-O2` thanks to `patches/circle-stdlib/0001`. A target's libraries carry the digest of the circle-stdlib patches they
were built with (`install/.okapia-patches`); a target brought back with another digest is built again.

### The merged image

`make -C src/kernel` (goal `okapia`) builds **one image carrying both engines**:

1. `patches-check` — `apply-patches.sh --check`, silent when the patches are in. A fresh clone without them builds
   and misbehaves far from the cause (no `HasIdleTime()`, a `double` FPU register, no PowerPC seam).
2. `ENGINE=basilisk objects` then `ENGINE=sheepshaver objects` into `emu-basilisk/` and `emu-sheepshaver/`; objects
   whose source left the build are removed, since the link takes a glob.
3. `shared` — `SHARED_SRCS` into `obj-shared/`: the board (`hal_circle.cpp`), its sound, chime and power-off
   (`board_sound_circle.cpp`), the Mac RAM block, the entry point (`okapia_boot.cpp`), `rom_chime.cpp` and the
   firmware. Only these exist once
   ([Architecture](../project/architecture.md#what-exists-once-and-what-exists-twice)).
4. `merge` — each engine is partially linked (`ld -r`, with `ENGINE_WRAPS`) into `engine-68k.o` and `engine-ppc.o`;
   every symbol the PowerPC half **defines**, plus the `__real_` references the linker fabricates, is listed into
   `engine-ppc.rename` with the prefix `ppc__` (the list is generated at each build) and renamed with
   `objcopy --redefine-syms`; `OkapiaRunPowerPC` is left out. Then the final link and `objcopy` to `kernel8.img`.

Both cores export `InitAll`, `ExitAll`, `PatchROM`, `Execute68k`…, hence the rename; nothing in `external/` is touched.
The seam between the halves is `OkapiaRun68k()` and `OkapiaRunPowerPC()` (`okapia_boot.h`), with C linkage.

`ENGINE=basilisk|sheepshaver` still selects one tree for `objects`. `make okapia-clean` removes both engines' objects,
the shared ones and the merge intermediates.

### Build options

| Option | Effect |
|---|---|
| `OKAPIA_TRACE=1` | trace build: disk read/write wraps with short-read detection, exception and trap traces, FPU instruction counts, `DIRECT_ADDRESSING_GUARD`, `FPU_SELFTEST`, `SOUND_SELFTEST` |
| `OPTIMIZE` | `-O2` by default. `-O3` measured no faster, and vectorised `GfxBlit()` into an alignment fault on Device memory ([Display](../topics/display.md)) |
| `OKAPIA_BUILD_TIME` | the clock's fallback, `date +%s` by default ([Clock and PRAM](../topics/clock-and-pram.md)) |

Generated at build time, never typed: the 68k opcode tables (`scripts/gen-cpu.sh`, host-built `build68k` and `gencpu`,
about 195,000 lines), kpx_cpu's execute table (`scripts/gen-ppc-exec.sh`), the key table (`gen-keycodes.py`), the
MacRoman table (`gen-macroman.py`), the firmware's strings (`gen-strings.py` from `assets/strings.tsv`), fonts
(`gen-font.py`) and icons (`gen-icons.py`).

### Patches

Upstream changes live in `patches/<submodule>/` as minimal patches, each explaining itself in its header, each a
candidate to send upstream — never a fork, because a patch that stops applying is an immediate and readable signal
where a fork silently accumulates rebase debt. `apply-patches.sh` is idempotent (bootstrap runs it); a patch already
applied is skipped.

| Patch | What it does |
|---|---|
| macemu `0001` vm: let a port place the guest itself | lets a bare-metal port choose where SheepShaver's Mac memory, Low Memory and Kernel Data live ([PowerPC engine](../topics/engine-powerpc.md#memory-layout)) |
| macemu `0002` glue: a port that does not trap can still compile | `sigsegv_handler()` no longer `#error`s without `HAVE_SIGSEGV_SKIP_INSTRUCTION` |
| macemu `0003` video_blit: the 1-bit expanders ignore the palette | 1-bit screens use `ExpandMap` like the other depths |
| macemu `0004` ppc-cpu: a seam for a port with no thread to spare | `powerpc_check_ticks()` every `ppc_check_ticks_quantum` instructions |
| macemu `0005` rsrc_patches: say whether the idle patch went in | `HasIdleTime()` |
| macemu `0006` disk_generic: let a backend say its medium can be taken out | removable media and file size asked of the backend |
| macemu `0007` memory: name the guest address before the host fault | `DIRECT_ADDRESSING_GUARD` |
| macemu `0008` fpu: give the 68881 a register that can hold it | the C99 core in IEEE binary128 ([Decisions](../project/decisions.md#fpu-fpu_ieee-in-ieee-binary128)) |
| macemu `0009` uae_cpu_2021: stop paying per instruction for what nothing reads | `M68K_NO_FAULT_PC`; `SPCFLAGS` without a lock |
| macemu `0010` uae_cpu_2021: fetch through pc_p, as uae_cpu did | instruction fetch through `regs.pc_p` |
| macemu `0011` video: let a port offer the sizes it can show | SheepShaver resolutions past `APPLE_ID_MAX` |
| circle-stdlib `0001` configure: optimise newlib outside debug builds | newlib at `-O2` (libm was `-O0`) |
| circle-stdlib `0002` libgloss/circle: seek with an off_t | files read past 2 GiB ([Storage](../topics/storage.md#volumes-past-2-gib)) |

## Status

- [x] Toolchain, bootstrap and pinned submodules (2026-08-25)
- [x] Header dependencies tracked in every Makefile (`-MMD -MP`)
- [x] Target switching without rebuilds; target checked by every run script (2026-09-12)
- [x] Merged image with both engines (2026-09-07)
- [x] `patches-check` on every build
- [ ] Continuous integration: build, boot Circle under QEMU, read a file from the FAT — no Apple ROM in public CI
- [ ] Offer the generally useful patches upstream (`0003`, `0005`, `0006`, `0008`–`0011`)
- [ ] `NOTICE.md` listing the origin, authors and copyright of every reused piece

## Pitfalls

- **Circle tracks no headers.** `Rules.mk:271` computes `DEPS` and never includes it, and its `%.o: %.cpp` has no
  `-MMD`. An object built by a rule without them is rebuilt when its `.cpp` changes and never when a header does; the
  link then joins objects built against two layouts of one struct and succeeds. It cost a jump into the font tables
  (instruction abort past `_etext`) and a down arrow arriving as Escape. **Every rule here compiles with `-MMD -MP`
  and reads the `.d` files back; a rule without them is a bug.** After adding the flags, `make okapia-clean` once.
- **Objects do not depend on the Makefile.** Changing a `-D`, a flag or a `#define` in `external/` rebuilds nothing.
  Objects compiled with Basilisk's `D(bug())` tracing flooded the serial port (20 MB per run) and cost 4.8× guest
  speed. After any flag change: `make okapia-clean`. `strings kernel8.img | grep 'EmulOp %04x'` tells in a second
  whether debug tracing is linked in.
- **`ld -r` kills `--wrap`, silently**: a partial link resolves everything inside its set, so a wrap on the final link
  redirects nothing. The wraps go on each engine's partial link (`ENGINE_WRAPS`). `objdump -d engine-68k.o | grep
  __wrap_` says whether a hook is alive.
- **`--wrap` does not survive a blanket prefix**: the prefix goes *inside* `__wrap_`, and `__real_<name>` is never
  defined, so it must be caught among the undefined symbols.
- **A symbol the PowerPC half references cannot be defined in the shared half**: the reference is renamed, the
  definition is not, and the link fails.
- **Circle invokes `ld` directly**: `--wrap=<mangled name>`, never `-Wl,--wrap`. Only calls crossing a translation unit
  are wrapped.
- **Kernel size**: Circle reserves `KERNEL_MAX_SIZE` (`sysconfig.h:38`) between the load address and the stacks
  (`memorymap.h:47`); circle-stdlib's `configure` passes 4 MB. It is a flag and a library rebuild, not a hardware
  limit. Overflow shows as an obscure link error.
- **A library patch is in the sources before it is in the libraries.** `apply-patches.sh --check` and the kernel's
  `patches-check` look at `external/`, not at `libcirclenewlib.a`: a target restored from `build/libs/` was built
  before the patch. `build-libs.sh` compares the patch digest for that reason; on the active target, rebuild with
  `make -C external/circle-stdlib newlib`, then relink the kernel.
- **`DEPTH` is compiled into `libcircle.a`**: `-DDEPTH=8` in an application Makefile does nothing.
- **`config.h` declares, it never includes**; it comes before everything else.
- **C++ exceptions**: Circle builds with `-fno-exceptions` (`Rules.mk:188`); the core adds `-fexceptions` per file.
- **macOS frictions**, all handled by `install-tools.sh` and the scripts: BSD `getopt` ignores `--long`, Bash 3.2 has no
  `mapfile`, BSD `sed` has no `\b` (and `sed -i` needs `''`), zsh aborts a command when a glob matches nothing.

## Development notes

- **2026-08-25** — Workstation and bootstrap; references cloned on demand only.
- **2026-09-05** — Two kernel images and a chain boot to switch engines.
- **2026-09-07** — One merged image; the `ld -r` / `--wrap` trap found when both hooks linked and were never called.
- **2026-09-12** — `build-libs.sh` target switching and `target.sh`, after a QEMU kernel was put on a Pi card.
- **2026-09-13** — newlib at `-O2`, libhfs's byte-by-byte `memcmp` removed from the link.

## References

- Circle `Rules.mk:188`, `:271`; `include/circle/sysconfig.h:38`; `memorymap.h:47`
- circle-stdlib `configure`
