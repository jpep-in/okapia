# Infinite Mac and Okapia

> Comparative study, September 2026. Two ports of the same base for opposite machines: `mihaip/macemu` (branch
> `infinite-mac-kanjitalk755`) takes Basilisk II and SheepShaver to WebAssembly, Okapia takes them to Circle. A
> browser and bare metal share almost every constraint — **no useful threads, no `SIGSEGV`, no fixed-address `mmap`,
> no JIT, one loop that has to do everything** — so what one had to solve, the other meets.

## Method

`reference/infinite-mac/` (`scripts/fetch-reference.sh infinite-mac`) and its `macemu/` submodule on the port's
branch. The common base with our `external/macemu` is commit `d7c0303`, which gives the whole port as one diff:

```bash
git -C reference/infinite-mac/macemu diff --stat d7c0303 HEAD
```

3,131 lines added, 225 removed, 76 files — the same order as `src/circle/` at the time (5,272 lines), so a
line-by-line comparison was fair. `reference/` is read-only; nothing is copied without being rewritten to our
conventions. The emulators are GPL on both sides (the Infinite Mac site is Apache-2.0).

## Layer by layer

| Layer | Infinite Mac | Okapia at the time | Outcome |
|---|---|---|---|
| Video | `JS/video_js.cpp`, **one file for both engines** | two `video_circle.cpp` | unified differently (below) |
| Sound | `JS/audio_js.cpp`, pull model | `audio_circle.cpp`, pull model | converged; their close grace period taken |
| Disk | `JS/disk_js.cpp` through `disk_generic` | `sys_unix.cpp` reused as is | removable media patch taken |
| Input | drained in the emulation thread | pushed from core 0 | fixed |
| PRAM | compared once a second and only printed | written the moment it changes | **Okapia ahead** |
| Clock | `PRECISE_TIMING_EMSCRIPTEN`, polling | Circle timer IRQ | deliberate divergence |
| 68k periodic seam | `cpu_do_check_ticks()` | counter only | put to use |
| PowerPC periodic seam | `CheckTicks()` in the interpreter | **none** | added |
| PowerPC low memory | static `gZeroPage`/`gKernelData` | the same, generalised (`patches/macemu/0001`) | converged |
| ExtFS name encoding | `mac_encodings.cpp` (MacRoman, MacJapanese) | identity | added |
| Durability | OPFS overlay, no flush policy | write-through, tested | **Okapia ahead** |

## Taken

1. **The 1-bit expanders ignore the palette.** `video_blit.cpp:426` wrote `-(bit)` instead of reading `ExpandMap`, so
   a Mac in black and white — or a System 6 with an inverted palette — got hard-wired black and white. Infinite Mac
   fixed the 32-bit version; `patches/macemu/0003` fixes `Blit_Expand_1_To_16` as well, with `check_blit`.
2. **The key ring is not safe between cores.** `ADBKeyDown()` (`adb.cpp:302`) writes `key_buffer[]` then
   `key_write_ptr` with no lock, and was called from Circle's USB handlers. x86's strong memory model makes such a
   ring correct by accident; AArch64 does not, and the consumer can read a key not yet written — a phantom key, never
   a memory error. Infinite Mac never touches `adb.cpp` from its producer: `input_js.cpp:15` drains a shared buffer
   from the emulation thread. Okapia now does the same ([Input](../../topics/input.md)).
3. **Shared folder names are not converted.** `extfs_unix.cpp`'s conversions are identities. `mac_encodings.cpp` asks
   the guest its script with an 18-byte 68k stub calling `ScriptUtil()` (line 316). Okapia took the technique, cached
   the answer instead of asking twice per name, and allocates nothing ([Shared folder](../../topics/shared-folder.md)).
4. **Removable media belong to `disk_generic`.** `disk_unix.h:43` gains `is_media_present()`, `is_fixed_disk()` and
   `eject()`, and `sys_unix.cpp` asks them instead of answering `true`; `SysGetFileSize()` asks the disk. Small, generic,
   upstreamable: `patches/macemu/0006`.
5. **A PowerPC periodic seam.** SheepShaver's interpreter never yields; Infinite Mac calls `CheckTicks()` every 50,000
   instructions (`ppc-cpu.cpp:583`, `:705`, `:755`). Okapia's version (`patches/macemu/0004`) carries only what needs
   the PowerPC thread and the right to block — PRAM writes, the input drain, log flushing — the 60 Hz tick staying a
   Circle IRQ.
6. **The Mac says when it is idle.** `rsrc_patches.cpp:57` exposes `HasIdleTime()`; Infinite Mac uses the first idle
   to mark the machine as started (`worker.ts:357`). Okapia logs it (`patches/macemu/0005`), and `screenshot.sh`
   captures on it instead of after a guessed number of seconds. `run-test.sh` does **not** end early on it — killing an
   idle Mac is the easy case — but refuses a green verdict without it. Whether `idle_wait()` should also yield the core
   is a thermal question to measure, not taken.
7. **A guard on guest memory access, in trace builds.** Their `sysdeps.h` checks addresses before dereferencing and
   prints in the hot path. Okapia's `DIRECT_ADDRESSING_GUARD` (`patches/macemu/0007`) counts and remembers the first
   stray, reported periodically, under `OKAPIA_TRACE=1` only.
8. **One video driver for two Macintosh — but not their way.** `video_js.cpp:23-86` is a `#ifdef SHEEPSHAVER` stub that
   fakes Basilisk's `monitor_desc` so one file compiles for both. That works for a simple driver; ours has dirty
   regions, mode changes driven by the Mac's driver and a `VModes` table owned by `video.cpp`, and the stub would have
   had to pretend about all of it. Okapia separates **the rules from the translation**: `video_shared_circle.cpp` states
   once which modes fit, which converter a depth needs, the palette, the refresh and the reports; each engine's file
   keeps its contract. 579 + 538 lines, half duplicated, became 266 + 370 plus the shared file, with identical composite
   figures afterwards (1,123 µs and 7% of wall on the 68k, 1,210 µs and 7.2% on the PowerPC, 59–60 Hz).

Measured once in place: the PowerPC seam ran at 112 Hz under QEMU with `PPC_CHECK_TICKS=50000` (later recalibrated to
1 kHz) and the PowerPC PRAM got written during a session for the first time — five times during a 7.6 boot. System 7.1
signalled idle at 11.5 s; 7.6 never did, because that card started the PowerPC engine, which has no idle patch.

Two defects surfaced because these changes made them live: **both Macintosh wrote the same PRAM file** (8,192 bytes
against 256, fields at different offsets) — now one file per engine, reading one byte more so a longer file is refused;
and **`run-test.sh` gave a green verdict on a guessed volume** when the card started the PowerPC engine, whose kernel
logged no `Boot volume:` — now it does, and the test says INCONCLUSIVE when nobody names one.

## Not taken

- **Hashing the whole frame buffer** (`video_js.cpp:208`, SpookyHash over 1.2 MB per frame, then sending the whole
  frame). Okapia's tile comparison with early exit measured 452 µs against 74,637 µs in a QEMU window; the only idea kept
  is that a palette change invalidates the image, which `bFullRedraw` already does.
- **Their durability model**: dirty chunks stacked in OPFS with no write order nor flush policy — nothing for an SD card
  someone unplugs.
- **A heap size hard-coded in `sysdeps.h`** that must agree with a shell script: the kind of constant generators exist
  to avoid.
- **Cascading `#ifdef EMSCRIPTEN` in `external/`**: `patches/macemu/0001` generalises the same need instead of adding a
  platform to the `#if`, which is the upstreamable form.
- **Clipboard** (`clip_js.cpp`): a bare board has no host clipboard.
- **Dirty regions on SheepShaver**: `video_set_dirty_area()` is empty in Infinite Mac (`video_js.cpp:352`); Okapia hands
  QuickDraw's announcements to the compositor. Extending it to the 68k would mean marking tiles on every guest write in
  the hottest loop; the cheap-set compositor got a still screen to 198 µs and 1.1% instead.

## Converged independently

`uae_cpu_2021` forced on every architecture; static low memory and kernel data for the PowerPC; no `SIGSEGV`, VOSF, JIT or
threads; a pull audio model raising `INTFLAG_AUDIO` when the queue has room. The one detail Okapia lacked was taken:
their `close_grace_period` keeps asking for ten blocks after the mixer reports no sources, because it reports when the
last source has finished *feeding*, not when its last sample was fetched ([Sound](../../topics/sound.md)).

## FPU

Routing ARM to MPFR is **upstream's** (`Unix/configure.ac:1881` sends `arm` and `aarch64` to `fpu_mpfr.cpp`); Infinite
Mac only added its target to the condition, and builds GMP 6.2.1 and MPFR 4.1.1 to WebAssembly for it. Okapia was then
the only AArch64 user on `FPU_UAE`, whose register is a `double` (`fpu/types.h:67`): 53 mantissa bits for the 68881's
64, an exponent of ±308 for ±4932.

The study first concluded the choice was binary, `double` or MPFR. It was not: on AArch64 `long double` **is** IEEE
binary128 — a 15-bit exponent with the Mac's own bias, 112 fraction bits — and `FPU_IEEE`'s quad variant was in the file,
disabled (`types.h:147-150`, "the emulator's implementation is not correct"). Of its two objections one had expired and
the other was a real bug: `make_extended()` was not the inverse of `extract_extended()`, and **1.0 went in and 1.5 came
out**. `patches/macemu/0008` enables and fixes it. MPFR would have cost `libgmp.a` 829 KB and `libmpfr.a` 787 KB against
about 1 MB of kernel headroom, a port of two autotools libraries, and **a `malloc` and a `free` per floating-point
instruction** (`fpu_mpfr.cpp:1517`); binary128 cost 62 KB. SheepShaver was already exact: a PowerPC float register is a
host `double` (`ppc-registers.hpp:161`), as the architecture specifies. A System 7.1 boot executes four FPU instructions,
which is how 1.0 → 1.5 went unnoticed.

## Build settings compared

| Setting | Infinite Mac | Okapia |
|---|---|---|
| JIT, VOSF | disabled | none (heap is `PXN`) |
| `OPTIMIZED_FLAGS` | undefined (x86 assembler) | n/a |
| Optimisation | `-O3` | **`-O2`**: `-O3` was tried — idle at 11,933/12,024 ms against 11,946/12,017, opcode rate 2–8% lower under QEMU, image 175 KB larger — and it vectorised `GfxBlit()` into an alignment fault on the Device-memory frame buffer (`EC 0x25`, `DFSC 0x21`) |
| `PPC_DECODE_CACHE`, `PPC_ENABLE_FPU_EXCEPTIONS` | defaults (1, 0) | defaults |
| Change detection | whole-frame hash | tiles |

## Worth studying later

- **Speed governor** (`speed-governor.ts`): an exponential moving average of the timing error bringing the guest to a
  target instructions-per-millisecond — for a period-accurate speed many 68k games depend on. No use while speed is the
  goal.
- **Copy-on-write overlay** (`disk-saver.ts`): a read-only base image plus a bitmap of modified blocks would make a
  rescue volume **writable yet indestructible**. Owes its flush policy and its test in the same change.
- **AppleTalk in the default PRAM** (`ether_helpers.h`): five PRAM bytes that let a System find the network with no
  setting — for the [network](../../topics/network.md) work.

## Sources

- <https://github.com/mihaip/infinite-mac>; <https://github.com/mihaip/macemu>, branch `infinite-mac-kanjitalk755`, base
  `d7c0303`. Lines cited from `reference/` are at that commit; lines from `src/` and `external/` at the time of the study.
