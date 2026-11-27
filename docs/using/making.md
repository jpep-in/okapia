<!-- Copyright (C) 2026 Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

## Months of finding out how a Macintosh works

Okapia has taken months of design, implementation, investigation and testing. A successful boot is a milestone;
making the next boot reliable, keeping sound and input responsive, and protecting a disk when a session stops
are much larger jobs.

The work combines knowledge of period Macintosh hardware and Mac OS with reading Apple’s technical
documentation, books published at the time and the source of established emulators. It includes host tests, comparisons
between emulators, execution traces, custom benchmarks, QEMU runs and checks on real Raspberry Pi hardware.
Testing has been a substantial part of the project, and remains so.

The [architecture](architecture.md) explains the layers. [About Okapia](about.md) tells the personal story behind
them.

## Built with AI assistance

All of Okapia’s own implementation has been developed with the help of **ChatGPT and Claude**. They have been
used throughout the work: exploring designs, writing and revising code, investigating failures, creating tests
and interpreting traces. Claude was particularly helpful in resolving a difficult translator problem using
execution telemetry and the project’s own benchmark tools.

Jonathan Pepin defines the project, makes the design decisions and checks the results. The new code builds on
decades of work by the emulator and library authors credited below; their code and authorship remain theirs.

## The foundations, and the people behind them

Okapia’s own code is **GPL-3.0-or-later**. The components below retain their own copyright notices and licences.
This is a guide to the main foundations; each upstream project’s source and notices carry the complete list of
contributors and terms.

| Project and people | What Okapia uses | Licence |
| --- | --- | --- |
| [Basilisk II](https://github.com/kanjitalk755/macemu) — Christian Bauer and contributors; maintained in the kanjitalk755 fork | 68K Macintosh emulation and Macintosh integration | GPL-2.0-or-later |
| [SheepShaver](https://github.com/kanjitalk755/macemu) — Christian Bauer, Marc Hellwig and contributors | PowerPC Macintosh emulation | GPL-2.0-or-later |
| UAE / WinUAE and the ARAnyM developers | The lineage of the `uae_cpu_2021` 68K core | GPL-2.0-or-later; retained source notices |
| SoftFloat and its 68K/FPSP adaptations — John R. Hauser, Andreas Grabher, the QEMU, Previous, WinUAE and Hatari contributors; Motorola’s FPSP work | 80-bit floating-point arithmetic and transcendental functions | SoftFloat-2a, BSD and GPL-2.0-or-later notices, by file |
| [Circle](https://github.com/rsta2/circle) — Rene Stange and contributors | Raspberry Pi bare-metal environment and drivers | GPL-3.0 |
| [circle-stdlib](https://github.com/smuehlst/circle-stdlib) — smuehlst and contributors | C/C++ runtime integration for Circle | GPL-3.0 integration; bundled libraries retain their terms |
| [hfsutils / libhfs](https://github.com/JotaRandom/hfsutils) — Robert Leslie; fork maintained by Pablo Lezaeta and contributors | HFS inspection and repair | GPL-2.0-or-later |
| [FatFs](http://elm-chan.org/fsw/ff/) — ChaN | Access to the Pi card’s FAT filesystem, through Circle | FatFs permissive licence |
| [SDL3](https://www.libsdl.org/) — Sam Lantinga and SDL contributors | Desktop window, input and audio | [zlib](https://www.libsdl.org/license.php) |
| [curl / libcurl](https://curl.se/) — Daniel Stenberg and contributors | Desktop release downloads | [curl licence](https://curl.se/docs/copyright.html) |
| Slirp — Danny Gasparovski and contributors, as carried by macemu | Desktop networking | BSD-style and individual source notices |
| [stb_truetype](https://github.com/nothings/stb) — Sean Barrett and contributors | Firmware font rasterisation | MIT or public domain / Unlicense |
| [miniz](https://github.com/richgel999/miniz) — Rich Geldreich, Tenacious Software, RAD Game Tools, Valve and contributors | Release ZIP extraction | MIT |
| [TeX Gyre Heros](https://www.gust.org.pl/projects/e-foundry/tex-gyre/heros) — GUST e-foundry and TeX Gyre contributors | Interface and website typefaces | GUST Font License |

The C and C++ runtime dependencies include newlib and libstdc++: their individual notices and the GCC Runtime
Library Exception also matter. Raspberry Pi release archives carry the notices for the boot firmware, device
tree, ARM stub and Wi-Fi firmware they include. Those files have their own redistribution terms.

Okapia uses a modified SoftFloat derivative, which may contain errors. Its provenance and changes are recorded
in the vendored softfloat README and the [FPU documentation](../topics/engine-68k.md#fpu-the-68881-in-80-bit-softfloat).

## Tools, reference projects and inspiration

[Retro68](https://github.com/autc04/Retro68), by Wolfgang Thaller and contributors, is used to build the project’s
OkapiaBench guest application, for the 68k and the PowerPC. Retro68 carries GPL and component-specific licences.
OkapiaBench’s own files are MIT. It runs [Embench-IoT](https://github.com/embench/embench-iot) (Embench and
contributors, GPL-3.0-or-later) and [CoreMark](https://github.com/eembc/coremark) (EEMBC, Apache-2.0 with EEMBC’s
acceptable use agreement) as they are published, and uses stb and miniz for its JPEG, PNG and ZIP tests; the
application as built is therefore distributed under GPL-3.0-or-later. CoreMark is a trademark of EEMBC.

The wider community has been generous with examples and ideas: [M5Tab-Macintosh](https://github.com/amcchord/M5Tab-Macintosh),
[Mini vMac](https://www.gryphel.com/c/minivmac/), [Snow](https://github.com/twvd/snow),
[Infinite Mac](https://infinitemac.org/), [BlueSCSI](https://github.com/BlueSCSI/BlueSCSI-v2),
[macemu-jit](https://github.com/rcarmo/macemu-jit), [Emu68](https://github.com/michalsc/Emu68)
and [pom68k](https://github.com/habib256/pom68k) have informed research, comparisons or testing. This acknowledgement
does not mean each project’s code is included in Okapia. Reuse follows the provenance and licence of the
particular source, recorded in the patches and source headers.

## Apple’s software stays yours

Apple’s ROMs, Mac OS, applications and system extensions are separate from Okapia. They are supplied by the
user. The project does not include an Apple ROM or a bootable Mac OS disk image.

Source, patches and the project licence are available in the [Okapia repository](https://github.com/jpep-in/okapia).
