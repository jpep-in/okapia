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

### MacBench

**Not the Ziff-Davis MacBench of the period**: Okapia's own 68k application, in [`tools/macbench/`](../../tools/macbench/)
(C for [Retro68](https://github.com/autc04/Retro68), under the MIT licence so that Basilisk II or anyone else can take it). It runs twenty tests
by itself at startup — five of the processor (`cpu_*`), three of the Toolbox (`tb_*`), twelve of QuickDraw (`qd_*`) —
and five of the FPU when Gestalt reports one (`fpu_*`, [`fpu.c`](../../tools/macbench/fpu.c), compiled for the 68881: a
Fourier transform, a Whetstone, a matrix product, the transcendentals, extended values to and from memory) —
each a fixed amount of work timed with `TickCount`, and writes `name ticks check` lines to **MacBench Results**: at the
root of a volume named `Unix` when there is one (a desktop Basilisk II's shared folder), otherwise next to the
application. `check` hashes what the test computed or drew, the window read from the frame buffer at any depth: two
runs that disagree on it did not do the same work, which is what makes it the test of the QuickDraw hooks
([Display](../topics/display.md#68k-quickdraw)).

```
RETRO68=~/github-repositories/Retro68-build ./scripts/build-macbench.sh               # build/macbench/MacBench.bin
./scripts/build-macbench.sh qemu/sd-contents/boot755.img                               # ...and into Startup Items
./scripts/build-macbench.sh -- -DDEPTH=16 -DAPPEND_RESULTS                             # with switches
```

Retro68 is built once by hand (`./build-toolchain.bash` in its clone); `RETRO68` names the directory holding its
`toolchain/`. The script copies with Retro68's hfsutils straight into the image: **never on a card an emulator is
using**. Compile-time switches: `-DAPPEND_RESULTS` keeps every run, one block after the other (a machine run by hand);
`-DHOLD_TICKS=<n>` leaves the last drawing on screen n ticks; `-DONLY='"qd_text"'` runs one test; `-DDEPTH=<bits>`
switches the screen first; `-DREPEAT=<n>` runs each test n times to see whether its check is stable;
`-DSCALE=<n>` multiplies every iteration count; `-DSTRETCH_DEBUG` hashes `qd_stretch` part by part.
`tools/macbench/run.py` boots a desktop Basilisk II once per configuration and prints the table; on Okapia, read the
results file off the volume after Special → Shut Down.

**On the Mac, automatically.** `scripts/build-native-basilisk.sh <name> [configure options]` builds a desktop Basilisk II
from a copy of the `kanjitalk755/macemu` clone (`MACEMU_CLONE`) with Okapia's FPU patches and its translator, against the
dependencies of `MACEMU_DEPS`, into `build/native/<name>/BasiliskII`. `run.py` then compares builds and settings:

```
./scripts/build-native-basilisk.sh softfloat --enable-fpe=softfloat --enable-jit-arm64
./scripts/build-native-basilisk.sh mpfr --enable-jit-arm64                       # upstream's FPU on ARM
python3 tools/macbench/run.py --repeat 3 --only fpu_ --save base.json \
    build/native/softfloat/BasiliskII qemu/sd-contents/okapia.rom systems/prepared/sys755.img /tmp/mb \
    "jit=jit true" "interp=jit false" "mpfr=build/native/mpfr/BasiliskII|jit true"
python3 tools/macbench/run.py --repeat 3 --compare base.json ...                 # after a change
```

A configuration is `name=prefs` or `name=binary|prefs`; `--repeat` keeps each test's median, `--save` and `--compare`
keep a baseline, and the last column says whether every configuration computed the same: an exact FPU gives a 68881's
bits on any host, so `fpu_*` checks agree between exact cores and runs, interpreted or translated.

## Results

### Summary

| Environment | Engine | CPU | Graf | Disk | Math | PR | Bench. Ave. | FPU Ave. | Color Ave. | Reference shown |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| [Basilisk II native, MacBook Pro M4](<../../benchmark/basilisk arm64 macbook pro m4.png>) | 68k | 43.36 | 34.72 | 9.42 | 468.29 | 25.44 | 46.64 | 9.21 | 49.67 | Quadra 650 |
| [Okapia, QEMU raspi3b on M4 — first](<../../benchmark/okapia in rpi3 qemu in macbook pro m4.png>) | 68k | 2.33 | 0.00² | 2.24 | 28.15 | 0.00² | 2.90 | 0.86 | 2.66² | Quadra 650 |
| [Okapia, QEMU raspi3b on M4 — later](<../../benchmark/okapia in rpi3 qemu in macbook pro m4 - basilisk.png>) | 68k | 5.14 | 1.68 | 4.48 | 65.77 | 3.30 | 5.78 | 2.82 | 2.48 | Quadra 650 |
| [Okapia, Raspberry Pi 4 — first](<../../benchmark/okapia rpi4 68K basilisk.png>) | 68k | 2.25 | 1.06 | 3.40 | 28.76 | 1.91 | 2.38 | 1.12 | 1.92 | Quadra 650 |
| [Okapia, Raspberry Pi 4 — rev2](<../../benchmark/okapia rpi4 68K basilisk-rev2.png>) | 68k | 6.84 | 3.18 | 3.72 | 94.45 | 4.82 | 7.15 | 2.98 | 5.58 | Quadra 650 |
| [Okapia, QEMU raspi3b on M4](<../../benchmark/okapia in rpi3 qemu in macbook pro m4 - sheepshaver.png>) | PowerPC | 1.56 | 0.00² | 3.36 | 319.94 | 3.16 | 7.50 | 3.02 | 1.13² | Power Mac 8100/80AV |
| [Okapia, Raspberry Pi 4 — first](<../../benchmark/okapia rpi4 ppc sheepshaver.png>) | PowerPC | 1.24 | 0.46 | 4.72 | 208.52 | 0.98 | 5.17 | 1.87 | 0.90 | Power Mac 6100/60 |
| [Okapia, Raspberry Pi 4 — rev2](<../../benchmark/okapia rpi4 ppc sheepshaver-rev2.png>) | PowerPC | 2.96 | 0.95 | 3.08 | 492.45 | 1.99 | 11.08 | 4.62 | 2.08 | Power Mac 6100/60 |

"rev2" is after the first batch of seven performance changes ([68k engine](../topics/engine-68k.md#performance-work-pi-4)),
applied together: the before/after ratio is their combined effect, not each one's. The two QEMU 68k captures differ
in the depths offered (8-bit only, then all), with no revision recorded, so their gap cannot be attributed.

¹ The native capture shows CPU 1.32 and Math 19.17 for the same record; a rounding difference.
² `0.00` means the test or depth was not run; the colour average is then 8-bit only and not comparable.

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
  were the Finder's own patch and were out of the trap table whenever Speedometer was in front
  ([Display](../topics/display.md#pitfalls)). Graf at 35.48 against 28.08 cannot be credited to them; one run
  each, and the cause of that gap is not known. The column stays as a second run of the translator: its lines
  differ from the first by 1 to 3 %, the colour lines by up to 11 %, Graf by 26 %.
- **Owed**: both columns again with a kernel that carries the fix, MacBench beside Speedometer, the System
  version, depth and `frameskip` written down.

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

- FFT and Matrix, which the softfloat core had made slower than binary128, are now twice what they were before T1;
  K'Whet, heavy in transcendentals, gains least, as the M4 profile said it would.
- 6.26 against 9.21 for interpreted Basilisk II on the M4 (MPFR) and 27.74 for the iMac G3, whose Speedometer runs
  PowerPC code on a hardware double FPU.

### MacBench's FPU tests on the MacBook Pro M4 (2026-11-16)

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
- **Okapia's own kernel computes the same numbers**: built with GCC, under QEMU's AArch64 with `jit true` and the
  translator's FPU, its five `fpu_*` checks are the M4's to the bit (and so are the `cpu_*` ones). Its times under
  QEMU mean nothing.

## Status

- [x] First QEMU baseline (2026-08-27)
- [x] First Pi 4 runs, both engines (2026-09-12)
- [x] rev2 after seven changes: 68k Quadra-class with margin (2026-09-13)
- [x] First Speedometer runs with the 68k translator on a Pi 4 (2026-11-15)
- [ ] Five runs per configuration with revision, frequency, temperature, System, mode and preferences recorded
- [ ] Measure the seven changes separately, or by family (frequency, libraries, CPU loop, video, I/O)
- [ ] Reproduce SheepShaver's `Disk` regression (4.72 → 3.08) noting cluster size, fragmentation and file position
      before changing the I/O path
- [ ] Profile PowerPC gaps: `Dhry`, `Towers`, `FPU FFT`, `FPU Matrix`, `Graf` — opcode rate, PMU counters, composite
      time
- [x] MacBench's FPU tests, and native builds compared automatically on the Mac (2026-11-16)
- [ ] MacBench's FPU tests on the Pi 4, interpreter and translator
- [ ] Real workloads: StuffIt decompression, file copies, Finder, Doom, MacBench
- [ ] A native SheepShaver run on the M4 for a homogeneous PowerPC comparison
- [ ] Period references also worth recording: Norton System Info, MacBench

## Pitfalls

- **Changes applied together cannot be attributed.**
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
