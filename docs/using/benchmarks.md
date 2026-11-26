<!-- Copyright (C) 2026 Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

## Recorded results, with their context

These are development measurements recorded on **15–16 November 2026**, rather than scores for a published
Okapia release. The Pi’s interpreter is the default; the translator is an experimental build option.
The diagrams below keep separate tests and configurations separate. Open **Raw numbers** under a diagram to see its table.

## Raspberry Pi 4: the 68K translator

Speedometer 4.0.2, Pi 4 at **1.5 GHz**, Macintosh display **1280×720, thousands of colours**. Kernel `a6e93db`,
built with `OKAPIA_JIT_ARM64=1` and run with `jit true`. Higher scores are better.

| Test | Interpreter | Translator | Translator / interpreter |
| --- | ---: | ---: | ---: |
| CPU | 6.53 | 35.01 | 5.4× |
| Graf | 3.96 | 28.08 | 7.1× |
| Disk | 3.47 | 3.53 | About 1× |

The CPU score improves substantially; disk throughput barely changes. Individual integer tests in this record
improve by roughly **10–16×**. Neither figure means every application will run that much faster.

Each configuration was run once. The exact guest System and frame-skip setting were not recorded for this
comparison, and repeated runs are still needed. A separate session that day checked guest ticks against the
clock: the difference was within 0.2%. The table excludes the separate `qdaccel sync` column, whose graphics
improvement cannot be attributed to QuickDraw acceleration.

[Full Pi CPU record and limitations](../contributing/benchmarks.md#raspberry-pi-4-with-the-68k-translator-2026-11-15).

## Raspberry Pi 4: floating-point progress

Speedometer 4.0.2, Pi 4 at **1.5 GHz**, **System 7.6.1**, translator enabled. The following columns compare the
older binary128 FPU with the softfloat FPU and its T1–T3 translation work. Higher scores are better.

| FPU test | Binary128, 15 November | Softfloat + translation work, 16 November |
| --- | ---: | ---: |
| FFT | 2.68 | 4.70 |
| K’Whet | 5.09 | 7.89 |
| Matrix | 3.66 | 6.18 |
| Average, computed | 3.81 | 6.26 |

That average is about **1.64×** the older configuration, from one recorded run. It combines FPU implementation
and translation changes; it is not an isolated measurement of a single optimisation. Later trace builds cover
more FPU instruction forms but have their own timings and first-pass effects.

The translator calls the shared 80-bit softfloat core from its blocks. This preserves the guest’s floating-point
model; it does not replace the 68881 with the host’s hardware floating-point unit.

[Full FPU record, later builds and outstanding checks](../contributing/benchmarks.md#raspberry-pi-4-the-fpu-translated-2026-11-16).

## Apple silicon: a controlled FPU comparison

Okapia’s MacBench application, built with Retro68, ran inside **desktop Basilisk II with Okapia’s translator and
FPU patches** on a **MacBook Pro M4**. This is not a benchmark of the packaged Okapia Mac application.
System 7.5.5, **640×480, 8-bit**, median of **three runs**. Times are guest ticks; **lower is better**.

| FPU test | Softfloat, interpreted | Softfloat, translated T1–T3 |
| --- | ---: | ---: |
| FFT | 45 | 24 |
| Whetstone | 48 | 25 |
| Matrix | 39 | 19 |
| Trigonometry | 22 | 17 |
| Memory moves | 34 | 21 |
| Total | 188 | 106 |

The total falls by about **44%**, or roughly **1.77× faster**. The recorded result checks match the softfloat
interpreter’s. These five tests do not cover every FPU instruction form: later call-heavy and instruction-form
tests were added to catch gaps they missed.

[Full Mac FPU comparison](../contributing/benchmarks.md#macbenchs-fpu-tests-on-the-macbook-pro-m4-2026-11-16).

## What is still being checked?

The Pi translator remains opt-in. The outstanding safety checks include System 7.1, repeated restarts and
hardware durability testing. QuickDraw acceleration has been moved to **wip-quickdraw**; it is not part of the
current implementation and the figures above do not advertise it as a feature.

The homepage’s **over 5× a Quadra 650** claim remains the older, explicitly labelled **interpreter CPU baseline**.
It is a different comparison from the translator’s 5.4× over its recorded interpreter run.

For reproducible comparisons, record the revision, clock speed, temperature, guest System, display mode and
preferences; repeat the same configuration and compare result checks as well as timings. QEMU helps with
functional checks, but its speed is not a prediction of Pi performance.

See the [measurement method](../contributing/benchmarks.md#using-it) and
[translator safety criteria](../project/decisions.md#no-jit-by-default-two-experimental-ones).
