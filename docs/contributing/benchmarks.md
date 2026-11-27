# Benchmarks

> How Okapia's speed is measured, and the results so far. Speedometer 4.0.2 inside the guest; screenshots in
> [`benchmark/`](../../benchmark/). Higher is better. See also the
> [bottleneck study](../notes/research/bottlenecks.md).

## Using it

**Method.** Run Speedometer 4.0.2 in the guest and record the **This Machine** column (the `0:30`, `0:60` scale
setting only sizes the charts). With each run, record: the git revision, the effective ARM frequency, the
temperature and throttling state (the log's clock watch), the guest System, the video mode and the preferences.
Repeat a configuration at least five times and keep median, minimum and maximum: one run per configuration gives
neither spread nor thermal stability. Disk tests keep synchronous writes and run on a controlled copy of the image.

**QEMU is a functional bench, not a predictor.** It detects regressions at constant configuration; the Pi 4 now beats
the QEMU capture on CPU (6.84 against 5.14), so the QEMU/Pi ratio says nothing about the quality of the port.

Speedometer's averages are pulled by `K'Whet`; the median of the ten integer subtests reads less distorted.

### OkapiaBench

Okapia's own benchmark, a Macintosh application in [`tools/okapiabench/`](../../tools/okapiabench/), C for
[Retro68](https://github.com/autc04/Retro68), **one fat application**: 68k code for a 68k Macintosh (68020 or later)
and a PowerPC fragment for a Power Macintosh. Called **MacBench until 2026-11-27**, renamed because Ziff-Davis's
MacBench of the 1990s is a different program, the one period reviews quote. Every dated entry on this page now
says OkapiaBench; those before 2026-11-27 are **version 1**, whose tests, names and times (ticks) are **not
comparable with version 2** (microseconds, other work).

Its own files are MIT, so that Basilisk II, another emulator or a period machine's owner can take them. The
application as built also runs [Embench-IoT](https://github.com/embench/embench-iot) (`external/embench-iot`,
GPL-3.0-or-later) and [CoreMark](https://github.com/eembc/coremark) (`external/coremark`, Apache-2.0), both
compiled as they are published, so it is distributed under **GPL-3.0-or-later**; `NO_EMBENCH=1` builds it without
Embench. A guest application is not linked with the emulator that runs it: the GPL asks nothing of whoever runs it.

**What it runs**, each test a fixed amount of work, by itself from Startup Items:

| Family | Tests | What they stand for |
|---|---|---|
| `cpu_` | `sieve` `sort` `copy` `crc` `unpack` `calls` `muldiv` `recurse` `interp` `chase_16k` `chase_256k` `chase_4m` `stream` `codegen` | integer code: a Dhrystone-like mix of calls, records and strings, 32-bit multiply and divide, recursion, a bytecode interpreter, memory latency at three working sets and bandwidth on 4 MB, code written then its cache flushed (what a translator pays) |
| `cpu_coremark` | [CoreMark](https://github.com/eembc/coremark) 1.0 | EEMBC's own, self-calibrated to ten seconds or more and self-validated: iterations a second, comparable with published scores |
| `tb_` | `packbits` `memory` `strings` `traps` `resources` `events` `textwidth` | the Toolbox outside QuickDraw: the Memory and Resource Managers, a trap that does nothing, `WaitNextEvent` with nothing to do, text measured |
| `qd_` | `rects` `ovals` `lines` `text` `regions` `copybits` `stretch` `mono` `scroll` `pictures` `windows` `textedit` | QuickDraw at the starting depth |
| `cqd_` | `rgb` `gworld` `convert` `mask` `modes` `pixpat` | Color QuickDraw (32-bit QuickDraw): arbitrary RGB colours, double buffering through a GWorld, 8 and 32-bit pictures copied to another depth, masked sprites, the arithmetic transfer modes, colour patterns |
| `…@1` `@8` `@16` `@32` | `qd_rects` `qd_text` `qd_copybits` `qd_scroll` `cqd_rgb` `cqd_gworld` | the same, at every depth the screen offers (`HasDepth`), the starting depth restored after |
| `fp_` | `fft` `matrix` `whet` | floating point in double, portable C |
| `fpu_` | `whet` `trig` `move` `forms` `calls` | the 68881 itself (68k only): extended precision, the transcendental instructions, every instruction form written by hand, calls that save FPU registers |
| `app_` | `deflate` `unzip` `jpeg_enc` `jpeg_dec` `png_dec` `qt_jpeg` `text` `sheet` `adpcm` `raytrace` | real work: a ZIP archive made and extracted (miniz), JPEG and PNG (stb), QuickTime's own Photo-JPEG decompressor when QuickTime is installed, search and replace and a word count, a spreadsheet recalculated, IMA ADPCM sound, a ray tracer |
| `emb_` | the 19 programs of [Embench-IoT](https://github.com/embench/embench-iot) 2.0 | `aha-mont64` `crc32` `depthconv` `edn` `huffbench` `matmult-int` `md5sum` `nettle-aes` `nettle-sha256` `nsichneu` `picojpeg` `qrduino` `sglib-combined` `slre` `statemate` `tarfind` `ud` `wikisort` `xgboost`: Embench's score against its official reference |

**What it writes.** A new file per run, **`OkapiaBench 2026-11-27 1432`** (` b`, ` c`… for another run in the same
minute, by the Mac's clock), at the root of the shared folder when there is one (Basilisk II's and SheepShaver's
ExtFS, whatever its name: `Unix` upstream, `Okapia` here), otherwise next to the application. Then a **recap on
screen** — each family's time, CoreMark's iterations a second, Embench's score — and every test's time, page by
page, until a click or a key. The file:

```
OkapiaBench 2
# date 2026-11-27T20:46:18          the Mac's clock: a Pi without network may be wrong
# code 68k                          the code that ran: 68k or ppc
# sysarch 68k                       the machine: a 68k application on a Power Mac says "code 68k, sysarch ppc"
# machine 11 / cpu 5 / cput … / fpu 3 / system 00000761 / ram … / vm off / qd 00000230 / quicktime … / screen 1024x768x8
# clock microseconds               or "ticks": System 7.1's Microseconds does not count
cpu_sieve 84271 5 00000404          name, microseconds (Time Manager), ticks, check
cpu_coremark 19346848 1163 ok it/s=16077.83 iterations=200000
emb_crc32 588953 35 ok gsf=994
app_qt_jpeg skip no-quicktime
END
```

`check` is a hash of what the test computed or drew (the window read from the frame buffer at any depth), or
CoreMark's and Embench's own verdict: two runs that disagree on it did not do the same work. The two clocks are
both written so that a Macintosh whose clock drifts shows it (`run.py` says so above 2 %). Embench's global scale
factor (`gsf`) grows until a program runs half a second; Embench's score divides by it.

```
RETRO68=~/github-repositories/Retro68-build ./scripts/build-okapiabench.sh   # build/okapiabench/OkapiaBench.bin, fat
./scripts/build-okapiabench.sh qemu/sd-contents/boot755.img                    # ...and into Startup Items
./scripts/build-okapiabench.sh --68k-only                                      # build/okapiabench/68k/OkapiaBench.bin
./scripts/build-okapiabench.sh -- -DONLY=cpu_crc -DNO_RECAP                    # with switches
REFERENCE=ref.h ./scripts/build-okapiabench.sh                                 # the recap scores against ref.h
```

Retro68 is built once by hand, **with the PowerPC**: `./build-toolchain.bash --no-carbon` in a build directory beside
its clone (`--no-ppc` leaves out what the fat application needs; `--68k-only` builds without it). `RETRO68` names the
directory holding its `toolchain/`. The two builds are joined by Retro68's `Rez` (`fat.r`): the 68k `CODE`
resources, the PowerPC fragment in the data fork and a `cfrg` to find it. The script copies with Retro68's
hfsutils straight into the image, into `System Folder:Startup Items` or a French System's `Dossier
Système:Ouverture au démarrage`: **never on a card an emulator is using**. Compile-time switches: `-DONLY=<name>`
runs one test; `-DNO_RECAP` quits once the file is written; `-DDEPTH=<bits>` starts at that depth;
`-DREPEAT=<n>` runs each test n times to see whether its check is stable; `-DSCALE=<n>` multiplies every
iteration count; `-DSTRETCH_DEBUG` hashes `qd_stretch` part by part. `NO_EMBENCH=1` and `REFERENCE=<header>` are
given to the script, not as flags.

**Comparing a 68k and a PowerPC.** The same source compiled for each, big-endian both, `int` and `long` 32 bits:

| Family | Times comparable | Same checks | Why |
|---|---|---|---|
| `cpu_` (but `codegen`), `cpu_coremark`, `emb_`, integer `app_` | yes: this machine running this work natively | yes | integer C; only the compiler's back end differs |
| `cpu_codegen` | no | no | each processor's own code, flushed with `FlushCodeCache` or `MakeDataExecutable` |
| `tb_` | as the system's cost | yes but `tb_events` and `tb_textwidth` | on a Power Mac part of the Toolbox is still 68k code under the ROM's emulator; another System's fonts measure differently |
| `qd_`, `cqd_` | yes, the same calls | only with the same System and ROM | the Quadra ROM's QuickDraw and a Power Mac's native one may differ by a pixel; Mac OS 8.5's font smoothing must be off |
| `fp_`, `app_raytrace`, `app_jpeg_enc` | yes | no | a 68881 computes intermediate results in extended and rounds twice; the PowerPC build is compiled `-ffp-contract=off` |
| `fpu_` | — | — | 68k only: a PowerPC has neither the extended format nor these instructions |
| `app_qt_jpeg` | as QuickTime's cost | no | Apple's codec, 68k or native; called by the 68k build only (QuickTimeLib is not linked on PowerPC) |

The fat application's 68k part, built alone (`--68k-only`) and run on a Power Mac, is a third case: a 68k
application under Mac OS's own 68k emulator, inside SheepShaver. Its `cpu_` checks are the 68k engine's; the gap in
time is what the double emulation costs. The 68k build compiles the tests for the 68020 (as applications of the
1990s were compiled), floating point in hardware only where an FPU is required (`fp_`, `fpu_`, `app_jpeg_enc`, `app_raytrace`),
soft everywhere else: CoreMark and Embench are therefore integer and soft-float code on a 68k, as on Embench's
reference, a Cortex-M4 without an FPU; on a PowerPC they use its FPU.

**Scores.** CoreMark's iterations a second are CoreMark's own (the run lasts ten seconds or more and validates its
CRCs; reporting it as a CoreMark score follows EEMBC's rules in `external/coremark/README.md`). Embench's speed is
Embench's definition, reference time / time × gsf, geometric mean of the nineteen, against
`external/embench-iot/baseline-data/speed.json`: 16 for the reference, a Cortex-M4 at 16 MHz; divided by the clock it
is the score per MHz, meaningful for a period machine (`run.py --mhz 33` for a Quadra 650), not for an emulator.
For everything else `run.py --reference FILE` scores each test against a run kept as reference, with a geometric
mean per family and overall over the tests both ran. No reference is committed yet: a period machine's run, a
Quadra 650 or a Power Mac 6100/60, would be the one to keep.

**On the Mac, automatically.** `scripts/build-native-basilisk.sh <name> [configure options]` builds a desktop Basilisk II
from a copy of the `kanjitalk755/macemu` clone (`MACEMU_CLONE`) with Okapia's FPU patches and its translator, against the
dependencies of `MACEMU_DEPS`, into `build/native/<name>/BasiliskII`. `run.py` then compares builds and settings:

```
./scripts/build-native-basilisk.sh softfloat --enable-fpe=softfloat --enable-jit-arm64
./scripts/build-native-basilisk.sh mpfr --enable-jit-arm64                       # upstream's FPU on ARM
python3 tools/okapiabench/run.py --repeat 3 --only fpu_ --save base.json \
    build/native/softfloat/BasiliskII qemu/sd-contents/okapia.rom systems/prepared/sys755.img /tmp/mb \
    "jit=jit true" "interp=jit false" "mpfr=build/native/mpfr/BasiliskII|jit true"
python3 tools/okapiabench/run.py --repeat 3 --compare base.json ...              # after a change
python3 tools/okapiabench/run.py report "OkapiaBench 2026-11-27 1432" pi.txt --reference quadra.txt --mhz 33
```

A configuration is `name=prefs` or `name=binary|prefs`; `--repeat` keeps each test's median, `--save` and `--compare`
keep a baseline, `report` reads files without booting (a Pi's card, a SheepShaver volume, a period machine's disk),
and the last column says whether every configuration computed the same: an exact FPU gives a 68881's bits on any
host, so `fpu_` checks agree between exact cores and runs, interpreted or translated. `--emit-header` writes a run as
the recap's reference.

## Results

### Summary

| Environment | Engine | CPU | Graf | Disk | Math | PR | Bench. Ave. | FPU Ave. | Color Ave. | Reference shown |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| [Basilisk II native, MacBook Pro M4](<../../benchmark/basilisk arm64 macbook pro m4.png>) | 68k | 43.36 | 34.72 | 9.42 | 468.29 | 25.44 | 46.64 | 9.21 | 49.67 | Quadra 650 |
| [Okapia, QEMU raspi3b on M4 — first](<../../benchmark/okapia in rpi3 qemu in macbook pro m4.png>) | 68k | 2.33 | 0.00² | 2.24 | 28.15 | 0.00² | 2.90 | 0.86 | 2.66² | Quadra 650 |
| [Okapia, QEMU raspi3b on M4 — later](<../../benchmark/okapia in rpi3 qemu in macbook pro m4 - basilisk.png>) | 68k | 5.14 | 1.68 | 4.48 | 65.77 | 3.30 | 5.78 | 2.82 | 2.48 | Quadra 650 |
| [Okapia, Raspberry Pi 4 — first](<../../benchmark/okapia rpi4 68K basilisk.png>) | 68k | 2.25 | 1.06 | 3.40 | 28.76 | 1.91 | 2.38 | 1.12 | 1.92 | Quadra 650 |
| [Okapia, Raspberry Pi 4 — rev2](<../../benchmark/okapia rpi4 68K basilisk-rev2.png>) | 68k | 6.84 | 3.18 | 3.72 | 94.45 | 4.82 | 7.15 | 2.98 | 5.58 | Quadra 650 |
| [Okapia, Raspberry Pi 4 — translator](#raspberry-pi-4-with-the-68k-translator-2026-11-15)³ | 68k | 35.01 | 28.08 | 3.53 | 177.67 | 12.60 | 55.26 | 3.81 | 33.94 | Quadra 650 |
| [Okapia, QEMU raspi3b on M4](<../../benchmark/okapia in rpi3 qemu in macbook pro m4 - sheepshaver.png>) | PowerPC | 1.56 | 0.00² | 3.36 | 319.94 | 3.16 | 7.50 | 3.02 | 1.13² | Power Mac 8100/80AV |
| [Okapia, Raspberry Pi 4 — first](<../../benchmark/okapia rpi4 ppc sheepshaver.png>) | PowerPC | 1.24 | 0.46 | 4.72 | 208.52 | 0.98 | 5.17 | 1.87 | 0.90 | Power Mac 6100/60 |
| [Okapia, Raspberry Pi 4 — rev2](<../../benchmark/okapia rpi4 ppc sheepshaver-rev2.png>) | PowerPC | 2.96 | 0.95 | 3.08 | 492.45 | 1.99 | 11.08 | 4.62 | 2.08 | Power Mac 6100/60 |

"rev2" is after the first batch of seven performance changes ([68k engine](../topics/engine-68k.md#performance-work-pi-4)),
applied together: the before/after ratio is their combined effect, not each one's. The two QEMU 68k captures differ
in the depths offered (8-bit only, then all), with no revision recorded, so their gap cannot be attributed.

¹ The native capture shows CPU 1.32 and Math 19.17 for the same record; a rounding difference.
² `0.00` means the test or depth was not run; the colour average is then 8-bit only and not comparable.
³ `OKAPIA_JIT_ARM64=1`, `jit true`, binary128 FPU (2026-11-15). With the FPU translated in softfloat the FPU average
is 6.26 ([below](#raspberry-pi-4-the-fpu-translated-2026-11-16)).

### 68k engine, detailed

| Test | Basilisk II, M4 native | QEMU first | QEMU later | Pi 4 first | Pi 4 rev2 | Quadra 650¹ |
|---|---:|---:|---:|---:|---:|---:|
| CPU | 43.36 | 2.33 | 5.14 | 2.25 | 6.84 | 1.31 |
| Graf | 34.72 | 0.00 | 1.68 | 1.06 | 3.18 | 1.31 |
| Disk | 9.42 | 2.24 | 4.48 | 3.40 | 3.72 | 1.93 |
| Math | 468.29 | 28.15 | 65.77 | 28.76 | 94.45 | 19.16 |
| PR | 25.44 | 0.00 | 3.30 | 1.91 | 4.82 | 1.56 |
| K'Whet | 126.28 | 13.85 | 23.96 | 8.46 | 26.59 | 6.93 |
| Dhry | 31.92 | 1.69 | 3.93 | 1.37 | 3.45 | 1.38 |
| Towers | 37.54 | 1.79 | 3.96 | 1.69 | 4.95 | 1.36 |
| Quicksort | 37.09 | 1.77 | 3.88 | 1.63 | 4.76 | 1.35 |
| Bubble Sort | 30.55 | 1.33 | 2.97 | 1.15 | 3.88 | 1.34 |
| Queens | 31.04 | 1.46 | 3.38 | 1.37 | 4.29 | 1.32 |
| Puzzle | 42.56 | 1.79 | 3.59 | 2.00 | 4.13 | 1.38 |
| Permute | 44.14 | 1.88 | 4.27 | 2.17 | 6.16 | 1.29 |
| Int. Matrix | 56.04 | 1.92 | 4.55 | 2.28 | 7.02 | 1.34 |
| Sieve | 29.23 | 1.48 | 3.35 | 1.67 | 4.26 | 1.24 |
| **Bench. Ave.** | **46.64** | **2.90** | **5.78** | **2.38** | **7.15** | **1.89** |
| FPU FFT | 5.45 | 0.54 | 1.51 | 0.78 | 2.16 | 0.99 |
| FPU K'Whet | 15.36 | 1.42 | 5.19 | 1.61 | 4.10 | 1.05 |
| FPU Matrix | 6.81 | 0.63 | 1.75 | 0.96 | 2.69 | 1.00 |
| **FPU Ave.** | **9.21** | **0.86** | **2.82** | **1.12** | **2.98** | **1.01** |
| B&W | 44.12 | 0.00 | 2.12 | 1.29 | 4.05 | 0.98 |
| 2 Bit | 44.52 | 0.00 | 2.10 | 1.66 | 4.67 | 1.03 |
| 4 Bit | 49.24 | 0.00 | 2.15 | 1.98 | 5.63 | 1.19 |
| 8 Bit | 54.86 | 2.66 | 2.86 | 2.28 | 6.71 | 1.07 |
| 16 Bit | 55.62 | 0.00 | 3.15 | 2.39 | 6.85 | 1.15 |
| **Color Ave.** | **49.67** | **2.66¹** | **2.48** | **1.92** | **5.58** | **1.08** |

### PowerPC engine, detailed

| Test | Okapia QEMU | Power Mac 8100/80AV | Pi 4 first | Pi 4 rev2 | Power Mac 6100/60 |
|---|---:|---:|---:|---:|---:|
| CPU | 1.56 | 4.18 | 1.24 | 2.96 | 3.13 |
| Graf | 0.00 | 2.08 | 0.46 | 0.95 | 1.51 |
| Disk | 3.36 | 1.80 | 4.72 | 3.08 | 1.70 |
| Math | 319.94 | 145.54 | 208.52 | 492.45 | 107.33 |
| PR | 3.16 | 2.84 | 0.98 | 1.99 | 2.25 |
| K'Whet | 50.53 | 26.74 | 37.45 | 77.02 | 27.47 |
| Dhry | 1.99 | 4.50 | 1.12 | 2.72 | 3.31 |
| Towers | 2.67 | 5.01 | 1.51 | 3.62 | 4.10 |
| Quicksort | 3.69 | 6.48 | 2.14 | 5.07 | 4.85 |
| Bubble Sort | 1.99 | 4.07 | 1.22 | 2.85 | 3.04 |
| Queens | 1.85 | 4.52 | 1.14 | 2.78 | 3.23 |
| Puzzle | 2.72 | 5.92 | 1.79 | 4.99 | 4.42 |
| Permute | 2.57 | 5.80 | 1.77 | 3.90 | 4.40 |
| Int. Matrix | 4.85 | 6.74 | 2.42 | 5.58 | 5.04 |
| Sieve | 2.10 | 3.97 | 1.14 | 2.97 | 2.97 |
| **Bench. Ave.** | **7.50** | **7.38** | **5.17** | **11.08** | **6.28** |
| FPU FFT | 2.97 | 7.24 | 1.73 | 4.01 | 5.40 |
| FPU K'Whet | 2.72 | 1.96 | 2.07 | 5.17 | 1.55 |
| FPU Matrix | 3.38 | 6.43 | 1.81 | 4.69 | 6.57 |
| **FPU Ave.** | **3.02** | **5.94** | **1.87** | **4.62** | **4.51** |
| B&W | 0.00 | 2.38 | 0.58 | 1.16 | 1.31 |
| 2 Bit | 0.00 | 1.99 | 0.78 | 1.77 | 1.62 |
| 4 Bit | 0.00 | 1.72 | 0.82 | 1.92 | 1.56 |
| 8 Bit | 1.13 | 1.43 | 1.01 | 2.42 | 1.57 |
| 16 Bit | 0.00 | 1.14 | 1.30 | 3.04 | 1.18 |
| **Color Ave.** | **1.13¹** | **1.73** | **0.90** | **2.08** | **1.45** |


### Raspberry Pi 4, rev2

**Basilisk II (68k)** — Quadra-class with margin:

| Area | rev2 / first | rev2 / Quadra 650 |
|---|---:|---:|
| CPU | 3.04× | 5.22× |
| Graf | 3.00× | 2.43× |
| Disk | 1.09× | 1.93× |
| Math | 3.28× | 4.93× |
| PR | 2.52× | 3.09× |
| Bench. Ave. | 3.00× | 3.78× |
| FPU Ave. | 2.66× | 2.95× |
| Color Ave. | 2.91× | 5.17× |

Median of the integer subtests: 4.53 (first 1.68, Quadra 1.35), 3.36× the Quadra. Every integer subtest beats its
reference, `Dhry` included (2.50×); all three FPU subtests too; disk 1.93× with no write cache. Further 68k work is
comfort, thermal efficiency or real workloads, no longer catching up.

**SheepShaver (PowerPC)** — close to a Power Mac 6100/60, graphics still short:

| Area | rev2 / first | rev2 / Power Mac 6100/60 |
|---|---:|---:|
| CPU | 2.39× | 0.95× |
| Graf | 2.07× | 0.63× |
| Disk | 0.65× | 1.81× |
| Math | 2.36× | 4.59× |
| PR | 2.03× | 0.88× |
| Bench. Ave. | 2.14× | 1.76× (pulled by `K'Whet` 77.02) |
| FPU Ave. | 2.47× | 1.02× (FFT 74%, Matrix 71%, K'Whet over 3×) |
| Color Ave. | 2.31× | 1.43× |

Median of the integer subtests: 3.76 (first 1.64, 6100/60 4.25), 0.88× the Power Mac. `Quicksort`, `Puzzle` and
`Int. Matrix` beat the reference, `Sieve` matches it. Speedometer is a fat application, so these figures describe the
native PowerPC path, not 68k applications under the PowerPC environment's own emulator.

### Raspberry Pi 4 against the MacBook Pro M4

Geekbench 7: [Pi 4 Model B Rev 1.1](https://browser.geekbench.com/v7/cpu/319984) single-core 264, multi-core 369;
[MacBook Pro 14" M4](https://browser.geekbench.com/v7/cpu/328324) 3,286 and 14,315 — **12.45× single-core**. Single-core
is the relevant ratio: the interpreter runs the guest processor on one core.

| Speedometer 68k | M4 native | Pi 4 rev2 | M4 advantage | Relative to Geekbench single-core |
|---|---:|---:|---:|---:|
| CPU | 43.36 | 6.84 | 6.34× | 0.51× |
| Bench. Ave. | 46.64 | 7.15 | 6.52× | 0.52× |
| Median of integer subtests | 37.32 | 4.53 | 8.25× | 0.66× |
| Math | 468.29 | 94.45 | 4.96× | 0.40× |
| PR | 25.44 | 4.82 | 5.28× | 0.42× |
| FPU Ave. | 9.21 | 2.98 | 3.09× | 0.25× |
| Graf | 34.72 | 3.18 | 10.92× | 0.88× |
| Color Ave. | 49.67 | 5.58 | 8.90× | 0.71× |
| Disk | 9.42 | 3.72 | 2.53× | 0.20× |

The first Pi run amplified the hardware gap (the M4 led by 19 to 22× on integer tests against 12.45× in Geekbench);
after rev2 it leads by 6.34× on CPU. The initial shortfall was configuration and porting, not a fundamental limit of
Basilisk. Subsystems do not scale with Geekbench: they include maths libraries, storage and video presentation.

### Raspberry Pi 4 with the 68k translator (2026-11-15)

Kernel `a6e93db` built with `OKAPIA_JIT_ARM64=1`, `jit true`, ARM clock 1500 MHz, Mac at 1280×720 in thousands of
colours shown at ×2. Speedometer 4.0.2 run by the owner, once with `qdaccel off` and once with `qdaccel sync`,
each session ended with Shut Down. The reference column is a record made on the same board without the
translator. System version and `frameskip` were not noted. In a session of the same day the Mac's tick count
stayed within 0.2 % of the clock under the translator, so the scores are not flattered by lost ticks.

| Test | Interpreter | Translator | Translator + `qdaccel sync` | Translator / interpreter |
|---|---:|---:|---:|---:|
| CPU | 6.53 | 35.01 | 35.17 | 5.4× |
| Graf | 3.96 | 28.08 | 35.48 | 7.1× |
| Disk | 3.47 | 3.53 | 3.46 | 1.0× |
| Math | 95.04 | 177.67 | 174.76 | 1.9× |
| PR | 5.11 | 12.60 | 12.78 | 2.5× |
| K'Whet | 24.14 | 36.09 | 36.41 | 1.5× |
| Dhry | 3.19 | 50.68 | 52.09 | 15.9× |
| Towers | 4.75 | 60.03 | 59.71 | 12.6× |
| Quicksort | 4.55 | 46.05 | 46.07 | 10.1× |
| Bubble Sort |  | 55.75 | 55.65 |  |
| Queens | 4.10 | 57.09 | 56.31 | 13.9× |
| Puzzle | 5.77 | 58.56 | 58.02 | 10.1× |
| Permute | 5.84 | 58.78 | 56.96 | 10.1× |
| Int. Matrix | 6.73 | 76.29 | 76.67 | 11.3× |
| Sieve | 4.06 | 53.32 | 53.45 | 13.1× |
| **Bench. Ave.** |  | **55.26** | **55.13** |  |
| FPU FFT | 2.07 | 2.68 | 2.69 | 1.3× |
| FPU K'Whet | 4.34 | 5.09 | 4.69 | 1.2× |
| FPU Matrix | 2.45 | 3.66 | 3.58 | 1.5× |
| **FPU Ave.** | 2.96 | 3.81 | 3.64 | 1.3× |
| B&W | 4.46 | 23.18 | 25.36 | 5.2× |
| 2 Bit |  | 24.07 | 24.27 |  |
| 4 Bit |  | 29.16 | 26.56 |  |
| 8 Bit | 6.44 | 34.51 | 38.20 | 5.4× |
| 16 Bit | 6.78 | 58.81 | 54.78 | 8.7× |
| **Color Ave.** | 5.64 | 33.94 | 33.83 | 6.0× |

Read off two screenshots; the reference values of Bubble Sort, 2 Bit, 4 Bit and Bench. Ave. were not legible.

- **The translator**: 5.4× on CPU, 10 to 16× on the integer tests, 7.1× on Graf, 6× on the colour average.
  What does not move is what it does not touch: the disk, and floating point, which stays in the binary128
  core. Against a Quadra 650 (CPU 1.31) the CPU line is 27×; the integer average is above the interpreted
  Basilisk II on the MacBook Pro M4 of the first table (55 against 46.64).
- **The `qdaccel sync` column measures almost nothing of `qdaccel`.** Until the fix of the same day the hooks
  were the Finder's own patch and were out of the trap table whenever Speedometer was in front (the hooks
  are on the `wip-quickdraw` branch since 2026-11-26). Graf at 35.48 against 28.08 cannot be credited to them; one run
  each, and the cause of that gap is not known. The column stays as a second run of the translator: its lines
  differ from the first by 1 to 3 %, the colour lines by up to 11 %, Graf by 26 %.
- **Owed**: both columns again with a kernel that carries the fix, OkapiaBench beside Speedometer, the System
  version, depth and `frameskip` written down. Done in part the same night: next section.

### Raspberry Pi 4, the FPU translated (2026-11-16)

Speedometer 4.0.2, read off the screen by the owner, one run; kernel with the softfloat FPU and the translator's
FPU instructions (T1–T3), `jit true`, System 7.6.1, ARM at 1500 MHz. Not the trace build, so no `okapia-fpu` timing.
Earlier columns from the runs above, with the translator:

| Test | binary128 (2026-11-15) | softfloat before T1 (2026-11-16) | softfloat, T1–T3 | vs binary128 |
|---|---:|---:|---:|---:|
| FPU FFT | 2.68 | 2.29 | 4.70 | 1.75× |
| FPU K'Whet | 5.09 | 6.09 | 7.89 | 1.55× |
| FPU Matrix | 3.66 | 3.00 | 6.18 | 1.69× |
| FPU average (computed) | 3.81 | 3.79 | 6.26 | 1.64× |
| Math | 177.67 | 241.45 | 276 | 1.55× |
| K'Whet (benchmark section) | 36.09 | 44.97 | 72.22 | 2.00× |

**Then with `FMOVEM` translated and declined F-line instructions no longer chained** (kernel of 10:21, one run, trace
build): FFT 4.36, Matrix 5.86, FPU K'Whet 2.4 on its first pass and 10.2 on the next ones, Math 387, the benchmark
section's K'Whet 34 then 84. The trace showed about 250,000 `FDIV`, `FSUB` and `FMOVE.D` a second still run by the
interpreter during the first passes, each now ending its block on its own where the chaining used to run a run of
them in one call: the likely cost of the first passes and of FFT and Matrix slipping 5 to 7 %. Immediate operands,
the one mode still declined, are translated since; `jit: declined` in a trace build names what is left.

**Then with immediates translated** (kernel of 10:47, one run, trace build): FFT 4.64, Matrix 5.87, FPU K'Whet 8.1 on
its first pass and 10.7 after, Math 377, the benchmark section's K'Whet 85 then 97. Not one FPU instruction reached
the interpreter after the boot (no `okapia-fpu` line at all), and `jit: declined` named only `CPUSHA`, `FSAVE`,
`FRESTORE` and Basilisk's own `FE05` trap. What is left of the first pass's deficit is not the FPU's.

- FFT and Matrix, which the softfloat core had made slower than binary128, are now twice what they were before T1;
  K'Whet, heavy in transcendentals, gains least, as the M4 profile said it would.
- 6.26 against 9.21 for interpreted Basilisk II on the M4 (MPFR) and 27.74 for the iMac G3, whose Speedometer runs
  PowerPC code on a hardware double FPU.

### OkapiaBench's FPU tests on the MacBook Pro M4 (2026-11-16)

Version 1, then called MacBench: ticks, and names of that version.

Desktop Basilisk II built from the translator's clone, System 7.5.5, 8-bit 640×480, median of three runs, in ticks
(lower is better):

| Test | MPFR, translator | softfloat, interpreter | softfloat, translator (before) | softfloat, translator (T1–T3) |
|---|---:|---:|---:|---:|
| `fpu_fft` | 177 | 45 | 43 | 24 |
| `fpu_whet` | 192 | 48 | 48 | 25 |
| `fpu_matrix` | 134 | 39 | 38 | 19 |
| `fpu_trig` | 105 | 22 | 21 | 17 |
| `fpu_move` | 164 | 34 | 35 | 21 |
| **total** | **772** | **188** | **185** | **106** |

- **softfloat is 4.2 times as fast as MPFR**, upstream's FPU on Apple silicon, with the same results on everything but
  the transcendentals (MPFR rounds them correctly, the 68040's FPSP does not).
- **Before T1, the translator gave the FPU nothing** (185 against 188 interpreted): every FPU instruction left the block.
  With it calling the FPU core from inside the block (T1), translating `FBcc`, `FDBcc` and `FScc` (T2) and direct paths
  for `FADD`, `FSUB`, `FMUL`, `FMOVE` and `FMOVE.D` out (T3), 1.75 times as fast; checks identical to the interpreter's
  on all twenty-five tests. Profiled after: the arithmetic itself is most of what is left, the call into the core about
  a tenth.
- **The C tests do not see what the Pi's trace saw.** The translator from before `FMOVEM` and immediates were
  translated runs `fpu_fft` to `fpu_move` within 4 % of the one after, but `fpu_calls` 1.53 times and `fpu_forms` 1.15
  times slower (75 and 55 ticks against 49 and 48): those two exist for that. Baseline in
  `build/native/baseline-forms.json` (not kept in the repository).
- **Okapia's own kernel computes the same numbers**: built with GCC, under QEMU's AArch64 with `jit true` and the
  translator's FPU, its five `fpu_*` checks are the M4's to the bit (and so are the `cpu_*` ones). Its times under
  QEMU mean nothing.

### Raspberry Pi 4, the night of 2026-11-15

Run by the owner: `jit true`, System 7.6.1, Mac at 1280×720 in thousands of colours (16 bits), a kernel built
after the softfloat FPU and the boot-menu commits (`80cb577`, `f47f91e`; the exact revision was not noted, nor
`frameskip` or the clock). One run of each. The result files and screenshots stayed with the owner.

**OkapiaBench**, `qdaccel` off then on: 152.0 s → 132.2 s for the twenty tests, the same checks in both files.
The eight computing tests do not move; the gain is copies (`qd_copybits` 4.8×), scrolling (3.2×) and regions
(−31 %), the tests whose transfers are whole windows. The table and its reading are on the `wip-quickdraw` branch,
with the hooks ([Decisions](../project/decisions.md#quickdraw-acceleration-set-aside)).

**Speedometer 3**, top lines read off three screenshots (several lower lines lose their first digit to the
column width and are not copied here):

| | CPU | Graf | Disk | Math | FPU Ave. | Color Ave. |
|---|---:|---:|---:|---:|---:|---:|
| Interpreter, `qdaccel` off | 101.76 | 66.62 | 26.33 | 662.06 | 27.83 | 28.51 |
| Interpreter, `qdaccel` on | 108.12 | 63.44 | 24.69 | 661.65 | 27.24 | 28.20 |
| Translator, `qdaccel` off | 576.67 | 285.50 | 27.84 | 595.97 | 40.66 | 131.86 |

- **Speedometer does not see the hooks**: Graf and the colour average are within the 6 % by which CPU, which
  they do not touch, differs between the first two runs. Its drawing tests are small objects; OkapiaBench's are
  whole windows.
- **Translator**: 5.3× on CPU, 4.5× on Graf, 4.7× on the colour average. `Int. Matrix` reads 0.00 in that
  run, which is not a score, and Math is 10 % lower than with the interpreter: neither is explained.

**Speedometer 4.0.2**, translator (whether `qdaccel` was on was not noted), against the run of the afternoon
above:

| | CPU | Graf | Disk | Math | FPU Ave. | B&W | 2 Bit | 4 Bit | 8 Bit | 16 Bit | Color Ave. |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Afternoon, `a6e93db` | 35.01 | 28.08 | 3.53 | 177.67 | 3.81 | 23.18 | 24.07 | 29.16 | 34.51 | 58.81 | 33.94 |
| Night | 34.53 | 27.14 | 2.31 | 241.45 | 3.79 | 22.84 | 18.02 | 18.15 | 18.91 | 25.92 | 20.77 |

- **Math is up by a third** (K'Whet 36.09 → 44.97), with the 68881 now in softfloat.
- **The colour tests from 2 bits up are 25 to 56 % lower, and Disk a third lower.** One run each, different
  kernels, a state of `qdaccel` that was not written down: nothing can be attributed yet. It is the first
  thing to reproduce, twice on one kernel (without the hooks since they left for `wip-quickdraw`).

### OkapiaBench 2 on the MacBook Pro M4, both engines (2026-11-27)

The first runs of version 2, in desktop Okapia (`build/desktop/okapia --headless`, the binary of 2026-11-26), one run
each, on clones of the QEMU card's French Systems, 8 bits at the start. The 68k engine with the translator (`jit
true`) unless "interpreted" (`jit false`); the PowerPC engine as desktop SheepShaver runs it, interpreted. "68k code on 8.6" is the 68k-only build on
Mac OS 8.6: a 68k application under Mac OS's own emulator, itself emulated.

| | 68k, 7.1 | 68k, 7.6.1 | 68k, 7.6.1, interpreted | PowerPC, 7.6.1 | PowerPC, 8.6 | 68k code on 8.6 |
|---|---:|---:|---:|---:|---:|---:|
| Tests run | 101 | 101 | 101 | 95 | 95 | 91 (no FPU) |
| Measured, s | 49 | 50 | 202 | 269 | 414 | 954 |
| Wall, s | 72 | 76 | 230 | 291 | 443 | 1000 |
| CoreMark, iterations/s | 15,766 | 16,174 | 1,483 | 1,465 | 1,461 | 147 |
| Embench-IoT speed | 9,127 | 9,032 | 585 | 744 | 758 | 54 |
| `cpu_` (but CoreMark), s | 2.5 | 2.5 | 26.7 | 22.2 | 21.7 | 263 |
| `qd_` at 8 bits, s | 1.8 | 1.9 | 20.8 | 43.0 | 83.2 | 88.8 |

**Checks.** On all six: every `cpu_` test (`cpu_codegen` too: its generated code computes the same sums),
CoreMark validated, the 19 Embench programs verified, the integer `app_` tests, every `tb_` test but `tb_textwidth`
(another System's fonts) and `tb_events`. `fp_`, `app_raytrace` and `app_jpeg_enc` differ between the 68k and the
PowerPC and agree between the two PowerPC runs. The interpreter and the translator agree on every check, the 68881's
bits included. QuickDraw at 8 bits, against 68k 7.6.1: 7.1 differs in nothing; PowerPC 7.6.1 in `qd_lines`,
`qd_text` and `cqd_pixpat` (the ROM's QuickDraw); 8.6 in four more. `cqd_convert` is left out: its check, in these
runs, changed from one call to the next (fixed since, [Pitfalls](#pitfalls)). At 1 bit every System draws its own; at 16 and 32 bits the PowerPC's rectangles and text differ. The
68k code on 8.6 draws 1 bit as the PowerPC on 8.6 does, not as the 68k on 7.6: the System, not the processor.

**Readings.**
- **The translator against the interpreter**, same System: CoreMark 10.9×, Embench 15.4×, and the median test of
  each family 14× (`cpu_`), 12× (`qd_`), 11× (`cqd_`), 10× (`tb_`, `app_`), but 1.7× (`fp_`) and 1.8× (`fpu_`):
  floating point stays in the softfloat core. `cpu_codegen` is the one test the translator loses, 179 ms against
  39 ms: code rewritten and flushed is translated again each time, what the test exists to show.
- **The interpreted 68k and the interpreted PowerPC are even** on CoreMark (1,483 and 1,465); Embench favours the
  PowerPC (744 against 585), its drawing and Toolbox tests the 68k (QuickDraw 20.8 s against 43.0 s).
- **The translated 68k engine is ten times the interpreted PowerPC one on this host**: CoreMark 16,174 against
  1,465, Embench 9,032 against 744. The System matters little to the processor (7.1 and 7.6 within 3 %, PowerPC
  7.6.1 and 8.6 within 2 %).
- **The double emulation costs ten times more**: 147 CoreMark iterations a second for the 68k code on 8.6. Mac OS's
  68k emulator is a 68LC040: no FPU, so the `fp_` and `fpu_` tests are skipped there.
- **Mac OS 8.6 draws text twenty times slower** than 7.6.1 on the same engine: `qd_text` 41 s against 2.1 s, at
  every depth of the sweep (190 s of the 414). Font smoothing was left as the System set it.
- **The PowerPC Toolbox is slow where it is still 68k**: `tb_traps` 7.7 s and `tb_packbits` 8.9 s against 0.2 s
  translated; `cqd_mask` 24.6 s on 7.6.1.
- **SheepShaver's ticks fall behind its microseconds** by 3 to 7 % on the PowerPC runs (`run.py`'s clock lines), and
  by a third under the double emulation (`tb_traps` 25.7 s by Microseconds, 16.4 s by ticks).
- **The work is fixed, so the slow paths are long**: sized for about 200 ms on the translated 68k, a test costs
  seconds on the interpreted PowerPC and tens of seconds under the double emulation. A Pi's interpreter or a period
  machine will take an hour or more.

## Status

- [x] First QEMU baseline (2026-08-27)
- [x] First Pi 4 runs, both engines (2026-09-12)
- [x] rev2 after seven changes: 68k Quadra-class with margin (2026-09-13)
- [x] First Speedometer runs with the 68k translator on a Pi 4 (2026-11-15)
- [x] OkapiaBench on a Pi 4 with the QuickDraw hooks off and on: 152.0 s → 132.2 s (2026-11-15)
- [x] Speedometer's FPU tests on a Pi 4 with the FPU translated: average 3.81 → 6.26 (2026-11-16)
- [ ] Speedometer 4's colour tests fell by 25 to 56 % between two kernels of 2026-11-15: reproduce on one
      kernel before reading anything into it
- [ ] `Int. Matrix` at 0.00 in Speedometer 3 under the translator
- [ ] Five runs per configuration with revision, frequency, temperature, System, mode and preferences recorded
- [ ] Measure the seven changes separately, or by family (frequency, libraries, CPU loop, video, I/O)
- [ ] Reproduce SheepShaver's `Disk` regression (4.72 → 3.08) noting cluster size, fragmentation and file position
      before changing the I/O path
- [ ] Profile PowerPC gaps: `Dhry`, `Towers`, `FPU FFT`, `FPU Matrix`, `Graf` — opcode rate, PMU counters, composite
      time
- [x] OkapiaBench's FPU tests, and native builds compared automatically on the Mac (2026-11-16)
- [ ] OkapiaBench's FPU tests on the Pi 4, interpreter and translator
- [ ] Real workloads: StuffIt decompression, file copies, Finder, Doom, OkapiaBench
- [ ] A native SheepShaver run on the M4 for a homogeneous PowerPC comparison
- [ ] Period references also worth recording: Norton System Info, Ziff-Davis MacBench
- [x] OkapiaBench 2: renamed, PowerPC and fat application, CoreMark, Embench-IoT, real work, Color QuickDraw and the
      depth sweep, result files dated and written test by test, recap on screen, `run.py report` and scores (2026-11-27)
- [ ] OkapiaBench 2 on a Pi 4, both engines, interpreter and translator
- [ ] A period machine's run kept as the reference (`run.py --reference`, `--emit-header`)
- [ ] OkapiaBench on Okapia's releases, so that a user need not build it

## Pitfalls

- **Changes applied together cannot be attributed.**
- **OkapiaBench 1 and 2 do not compare**: other tests, other work, ticks against microseconds. `run.py` refuses a
  version 1 file.
- **QuickDraw checks compare only with the same System and ROM**: the Quadra ROM's QuickDraw and a Power Mac's
  native one may differ by a pixel, and Mac OS 8.5's font smoothing changes text (turn it off in Appearance).
- **A hand-made PowerPC transition vector needs a barrier.** `cpu_codegen` calls its generated code through a
  `{ code, toc }` descriptor of its own; GCC did not see that the indirect call reads it and dropped the store of
  the code address, so the call jumped anywhere and SheepShaver logged 79 million illegal instructions (4 GB) before
  it was stopped. The descriptor is static and a memory barrier precedes the call.
- **The result file is written test by test**: a test that never returns leaves the lines before it, which is how
  the one above was found.
- **`-DONLY=name`, without quotes**: CMake's flags lose them; the name is turned into a string in `bench.h`.
- **QuickDraw's dither carries its error from one call to the next**: `cqd_convert`'s copies with `ditherCopy` drew
  differently at every repetition (three checks in three passes), the same sequence with the translator and the
  interpreter. It looked like a translator fault between two runs and was not. The dithered copies are still timed;
  the check is taken after one plain copy of each picture.
- **System 7.1 has a `Microseconds` trap that does not count**: it answers, always the same. Every time read 0 and
  CoreMark, which calibrates itself on that clock, never reached its second and ran for ever. The clock is now tried
  across a tick at start; when it does not move, the times are ticks (`# clock ticks`).
- **Two runs set to 0:60 and 0:480 once differed by exactly 8×** under QEMU; the guest clock was not at fault (Mac Ticks
  30,894 for ~30,600 expected at 519 s). Reproduce before investigating.
- **Never tune headless and trust it windowed**: a QEMU window changes frame buffer costs by an order of magnitude
  ([Display](../topics/display.md)).

## Development notes

- **2026-08-27** — First QEMU baseline, `raspi3b` on the M4, 640×480 8-bit shown at 1280×960: composite 236 µs headless
  and 452 µs windowed, 55 and 50 frames/s, guest 20,162 and 23,584 k opcodes/s. Speedometer against the Quadra 650
  record: CPU 44.24 (1.32), Graf 34.92 (1.31), Disk 9.61 (1.93), Math 473.19 (19.17), FPU 9.03 (1.01), Color 47.24
  (1.08). These do not match the later QEMU captures (CPU 2.33 to 5.14) and sit beside the native M4 run; they are
  kept for the record and not used.
- **2026-09-12** — Pi 4 far below expectations: CPU 2.25. Led to the bottleneck study and its counter-review.
- **2026-09-13** — rev2.
