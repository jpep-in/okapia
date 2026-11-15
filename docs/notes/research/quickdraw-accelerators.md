<!--
Copyright (C) 2026  Jonathan Pepin
SPDX-License-Identifier: GPL-3.0-or-later
-->
# QuickDraw versions and the accelerator cards that hooked them

> Reading study, 2026-11-15. Two questions: which QuickDraws exist and how they differ for someone who hooks them, and
> how the accelerator cards of 1988–1995 dealt with that. Sources: Apple's 1994 source under
> `reference/supermario/base/SuperMarioProj.1994-02-09` (cited as `SM:`), the 1984 QuickDraw source Apple released
> through the Computer History Museum (`reference/quickdraw-1984`, cited as `QD84:`), Apple Technical Notes, *develop*,
> Inside Macintosh, manufacturer announcements and READMEs, read on the web on that date, and four period drivers
> read statically from `drivers/` (outside the repository). **Nothing here was run for this note**; what the running
> machine does is in [Display](../../topics/display.md#68k-quickdraw) and
> the [call map](quickdraw-8bit.md), not repeated. A claim without a link is marked *not confirmed*. Forum threads on
> 68kmla.org that probably hold more on Radius and SuperMac software refused automated reading and are not used.

## Verdict

1. **The loop table Okapia found in the trap table is a patch interface, and it has a history.** The entries at `$1AC0`
   and after are Toolbox traps `$AB30`–`$AB90`, declared as "quickdraw hooks" next to `BitBlt` and `RgnBlt`. The first
   program to live on them was Andy Hertzfeld's QuickerDraw (December 1987), which Apple licensed and folded into the
   System. Apple's own later patches use the same vectors, and so, by Apple's account of what disabled them in 1994,
   did most third-party cards.
2. **There are three QuickDraws on a 68k Macintosh, and one number for the last of them.** Gestalt says `$230` from
   System 7.0 onwards and cannot tell the patch sets apart. On a ROM of the IIci family, which `F1ACAD13` is, the
   QuickDraw that runs is the ROM's routines with the System's replacements laid over them, vector by vector.
3. **The period knew three ways to accelerate it**: patch the private vectors (QuickerDraw, Radius, SuperMac, by Apple's
   account most third parties), port the whole of QuickDraw to the card below the documented bottlenecks (Apple's 8•24
   GC), and a hook the system calls with a parameter block and a right to decline (Power Macintosh, from 1994). The
   first broke when Apple rewrote QuickDraw and stopped calling the vectors; the second never followed the 68040 or
   System 7.5; the third is the one SheepShaver still uses.
4. **A systematic study of QuickDraw versions is worth doing only in one form**: a census, per System release, of which
   private vector is the ROM's and which is the System's. That is what a vendor's per-release software revision
   amounted to, and it is the part Gestalt does not give.

## Which QuickDraws exist

### The generations

| QuickDraw | Where it lives | Gestalt `'qd  '` | Source |
|---|---|---|---|
| Original, 1 bit (1984) | ROM of the Plus, SE, Classic, Portable | `$000` | [Gestalt Manager summary][im-gestalt]; `QD84:` |
| Color QuickDraw (Macintosh II, 1987) | ROM of the Macintosh II (version word `$0178`) | `$100` | [Imaging With QuickDraw][im-cqd]; `SM:Internal/Asm/LinkedPatchMacros.a:1829` |
| 32-Bit QuickDraw 1.0 (May 1989) | a file copied into the System Folder; loaded whole into RAM over a II-class ROM | `$200` | [TN QD01][qd01] |
| the same, in ROM | Macintosh IIci and later (version word `$067C`) | `$200` and up | [TN QD01][qd01]; `SM:Internal/Asm/PatchMacros.a:97` |
| 32-Bit QuickDraw 1.2 | System 6.0.5 and later | `$220` | [TN QD01][qd01] |
| 32-Bit QuickDraw 1.3 | System 7 | `$230` | [Imaging With QuickDraw][im-cqd]; `SM:QuickDraw/GWorld.a:115` |
| QuickDraw 1.3.5, rewritten in C, native | Power Macintosh (1994) | *not confirmed* | [develop 18][dev18] |
| "Allegro" QuickDraw | Mac OS 8.5 (PowerPC only) | `$250` | Apple's `Gestalt.h`, still in the macOS SDK (`CarbonCore.framework/Headers/Gestalt.h`) |

Version 1.1 has two values in Apple's own headers: `$210` in the 1991 `GestaltEqu.a`
(`SM:Interfaces/AIncludes/GestaltEqu.a:420`) and `$0201` in the later `Gestalt.h`. Which System shipped 1.1 is *not
confirmed* here.

The codename of the 32-Bit QuickDraw file was Jackson Pollock: its sources are `SM:QuickDraw/JacksonPollock/`, and
trap `$AB03` carries the name in the dispatch table (`SM:OS/DispTable.a:1067`).

### The 1984 source: no vector below the bottlenecks

Apple released the original QuickDraw through the Computer History Museum in 2010 ([CHM][chm]): 30 assembly files and
6 Pascal units by Bill Atkinson, copyright 1984, for non-commercial use (`scripts/fetch-reference.sh quickdraw-1984`;
the files end their lines with CR, and line numbers below are counted after converting them). It is the same text as
`SM:QuickDraw/Classic/`, which still names the Lisa files (`--> BITBLT.TEXT`) and is within twenty lines of the same
length for `BitBlt`, `RgnBlt` and `Stretch`. *Not diffed line by line.*

What it settles for a hook is where the extension points were at the start:

- **The bottleneck record is original.** `StdBits` and `StdRect` test `grafProcs` and call through `bitsProc` in 1984
  (`QD84:Bitmaps.a:18`, `:264`–`268`; `Rects.a:19`, `:179`).
- **Below it there is nothing to patch.** QuickDraw calls its own transfer routines by name: `JSR RGNBLT`
  (`QD84:Rects.a:216`), `JSR BITBLT` (`RgnBlt.a:207`), `JSR DrawSlab` (`DrawLine.a:462`), `JSR StretchBits`
  (`DrawText.a:901`). The same calls in Color QuickDraw go through `JSR ([$E00+n*4])`. The "quickdraw hooks" are an
  addition of the Macintosh II, not part of Atkinson's design: on a 1-bit QuickDraw an accelerator's only seams are
  the public traps and the bottlenecks.
- **Apple's first QuickDraw patch wrote the trap table.** The archive holds `QDPatch.a`, a "QuickDraw patch program"
  that copies new code into the system heap and installs five routines (`CopyMask`, `GetMaskTab`, `MeasureText`,
  `CalcMask`, `SeedFill`) with `MOVE.L A0,$C00+<4*n>`, each commented "Set new trap address kludge"
  (`QD84:QDPatch.a:33`–`51`). It carries no date. Okapia writes the same table for another reason (the Process Manager
  pitfall in Display).

### Two regimes, by ROM family

What a hook meets depends less on the QuickDraw version than on the ROM under it.

- **II-class ROM (`$0178`)**: 32-Bit QuickDraw is a complete QuickDraw in RAM, installed from a `'ptch'` resource; the
  installer looks at the version word at `ROMBase+8` and does nothing on a `$067C` ROM
  (`SM:QuickDraw/JacksonPollock/JPInit.a:25`). The whole QuickDraw is then of the System's age.
- **IIci-class ROM (`$067C`)**: the ROM's QuickDraw stays and the System lays `QDciPatchROM.a` over it, a file of
  19,649 lines whose header reads "Quickdraw patches for ci and descendents" and which binds to ROM addresses by
  version (`RomVersions Aurora`, `SM:QuickDraw/Patches/QDciPatchROM.a:5`, `:434`). `F1ACAD13` has `$067C` at offset 8
  (read from `qemu/sd-contents/okapia.rom`), so this is Okapia's regime: a ROM QuickDraw of one age driven by System
  replacements of another, which is the pitfall already recorded in Display.

The patch file even lists what it will not fix: one arithmetic-mode case "will not be added to ci since it requires
patching out the entire blit loop" (`SM:QuickDraw/Patches/QDciPatchROM.a:383`). The ROM and the RAM version of the
same release are not the same QuickDraw.

### What each System lays on top

From the change histories at the head of `SM:QuickDraw/Patches/QDciPatchROM.a` and `QuickDrawPatches.a`, and from what
Okapia read in running guests (Display, call map):

| System | What is known | Basis |
|---|---|---|
| 6.0.5 | The first `QDciPatchROM`: `StdBits`, `stScanLoop`, dithering, pictures, `OffscreenVersion` raised to `$120` | history entries "NEEDED FOR 6.0.5", January–February 1990 |
| 7.0 | `OffscreenVersion` raised to `$130` (15 January 1991); alpha-channel fixes (October–November 1990) | `QDciPatchROM.a` entry `<90>`; `SM:QuickDraw/QDHooks.a` entries `<4>`, `<5>` |
| 7.1 | `StretchBits` rewritten to send indexed and direct transfers through `ScaleBlt`, new `ScaleBlt` loops, the meaning of `multColor` changed (June 1992, bug 1031825) | `QDciPatchROM.a` entry `<132>`; `SM:QuickDraw/QuickDraw Release Notes`. That this is the 7.1 build is my reading of the dates and of the "Cube-E" tags; the same `StretchBits` was fingerprinted on 7.1 and 7.6.1 (Display) |
| 7.5.5 | The pattern-copy loops replaced for alpha, the forward copy `bSetup0` (`$AB58`, the entry at `$1B60`) replaced by a `MOVE16` version | read from a guest (Display). The source shows the `MOVE16` patch removed for "Cube-E" in February 1992 (`QDciPatchROM.a` entries `<127>`, `<128>`) and a "working fast bSetup0 patch for 68040 machines" moved to `QuickDrawPatches.a` in April 1992 (`:22`, `:1833`) |
| 7.6.1 | Same `StretchBits` routine as 7.1, at another address | read from a guest (Display) |
| 8.0, 8.1 | *Not confirmed.* No source read, no guest dumped | |

On Power Macintosh the System 7.5 updates also changed the native QuickDraw ("changes to native QuickDraw which speed
up operations such as scrolling", a Type 11 crash fixed: [System 7.5 Update 2.0 notes][sys753]); that is the other
engine's QuickDraw, not the 68k one.

### Telling them apart, then

- **Gestalt `'qd  '`.** The selector is computed, not stored: original QuickDraw if `ROM85` says no colour; `$100` if
  trap `$AB03` is unimplemented; otherwise `OffscreenVersion` plus `$100` (`SM:OS/Gestalt/GestaltFunction.a:826`–
  `856`). `OffscreenVersion` is selector 20 of `QDExtDispatcher` (`$AB1D`) and returns a constant, `$130` in the 1994
  source. Apple's headers define no value between `$230` and `$250`, so every System from 7.0 to the last 68k release
  answers `$230` whatever it has patched. *Inferred from the two sources; not checked on 7.5.5, 7.6.1 or 8.x guests.*
- **Trap addresses.** Gestalt itself recognises 32-Bit QuickDraw by comparing the address of `$AB03` with that of
  the unimplemented trap; a program could do the same before Gestalt existed.
- **The ROM version word** at `ROMBase+8`: `$0075` Plus, `$0276` SE, `$0178` II, `$037A` Portable, `$067C` IIci and
  descendants, `$077D` SuperMario (`SM:Internal/Asm/LinkedPatchMacros.a:1829`). Inside Macintosh warns that it says
  what the ROM is and nothing of what runs, since ROM routines "may have been patched by the system at startup time"
  ([Using the Gestalt Manager][im-gestalt-use]).
- **Apple's own patches did not trust either.** The 32-Bit QuickDraw 1.2 file refuses System 6.0.3 by comparing two
  longs of instructions at its caller's return address (`SM:QuickDraw/JacksonPollock/JPAlert.a:29`–`31`). That is a
  code fingerprint, in Apple's own installer, in 1990.

### Entry points: three levels, one of them documented

| Level | Entries | Status |
|---|---|---|
| The drawing calls | `CopyBits` (`$A8EC`), `ScrollRect` (`$A8EF`), `PaintRect`… | Public, stable from 1984 |
| The standard bottleneck procedures | `StdText` (`$A882`), `StdLine` (`$A890`), `StdRect` (`$A8A0`), `StdRRect` (`$A8AF`), `StdOval` (`$A8B6`), `StdArc` (`$A8BD`), `StdPoly` (`$A8C5`), `StdRgn` (`$A8D1`), `StdBits` (`$A8EB`), `StdTxMeas`, `StdGetPic`, `StdPutPic`, `StdComment`; `StdOpcodeProc` in Color QuickDraw | Public. A port may replace them through `QDProcs` or `CQDProcs` (`SetStdProcs`, `SetStdCProcs`), and a replacement must keep the standard calling sequence ([Imaging With QuickDraw][im-custom]). Stable across every QuickDraw |
| The "quickdraw hooks" | Toolbox traps from `$AB00`: routines (`BitBlt` `$300`, `DrawSlab` `$30B`, `RgnBlt` `$31F`, `StretchBits` `$327`, `SlabMode`, `SeekRgn`…), then row loops `$330`–`$390` (`bMAIN0`…, `rMASK0`…, `stMASK0`…, `slMASK8`…) | Private. Reached by `JSR ([$E00+n*4])` from inside QuickDraw (`SM:QuickDraw/QDHooks.a`), declared in `SM:OS/DispTable.a:1064`–`1217`. *develop* calls them "low-level (undocumented) vectors" ([develop 18][dev18]) |

Within the private level the age differs. None of it exists in 1984 (above). The routine and loop vectors up to `$391`
come from the Macintosh II (tag `<C701>`; `ITabMatch`, `$391`, is dated 20 February 1987). `ScaleBlt` (`$396`),
`stScanLoop` (`$399`), `BlitCase`, `FastLine`, `BitsDevLoop` and the tables after them are tagged `<3.4>` and came with
32-Bit QuickDraw in 1989: a Macintosh II ROM alone does not have them. `QDExtDispatcher` (`$AB1D`) is the one entry of
that block with a public face, as `QDOffscreen`.

So of the five entries `qdaccel` hooks, three (`BitBlt`, `RgnBlt`, `DrawSlab`) exist in every Color QuickDraw and two
(`ScaleBlt`, `stScanLoop`) only from 32-Bit QuickDraw on, and none has a published contract. Their frames, the register
conventions and the meaning of fields such as `multColor` changed inside one file's history (above, 7.1).

## How the accelerators dealt with it

### QuickerDraw: the loop vectors, and Apple licensing the patch

Andy Hertzfeld wrote QuickerDraw between 22 December 1987 and January 1988. Its source is in Apple's tree, with his
header: it "patches out some of the Color QuickDraw inner loop traps" for 8-bit mode
(`SM:QuickDraw/Patches/QuickerDraw.a:12`–`15`) and lists them: `$330`, `$338`, `$35A`, `$35E`, `$360`, `$362`, `$372`,
`$373`, `$380`, `$381`, `$384`, and the arithmetic loops of the three families (`:83`–`:120`). In an interview of 1988
he says he first copied 60 K of ROM into RAM, found it unnecessary, and settled on replacing addresses in the "little
memory jump table" the inner loops go through; he claims a factor of about three on some operations and describes
region counting and a cache of colour mappings ([MacTech 4.6][hertzfeld]).

He signed a non-exclusive contract with Apple in February 1988 and kept the right to distribute it himself
([MacTech 4.6][hertzfeld]). The source says the rest: "A New Life at Apple", then
`PB415 … 01Mar88 QuickerDraw integrated into system patch files`, then four months of Apple's fixes to it (edge
masks, jumps back into ROM that crashed `CopyBits`, A/UX) (`QuickerDraw.a:55`–`71`). The interview says it would ship
in System 6.0; the file's first line says it was in the 6.0.3 sources. Whether Radius distributed it is *not
confirmed*, though Hertzfeld wrote the firmware of Radius's first product ([Wikipedia, Radius Inc.][wp-radius]).

Two things follow. The loop table is not an implementation detail that leaked: it was used as an extension point within
a year of the Macintosh II. And a patch at that level needed Apple's maintenance from the first month.

### Apple's Macintosh Display Card 8•24 GC: a port

The card (1990) carries an Am29000 at 30 MHz with its own memory, and runs **a QuickDraw of its own**: "GC QuickDraw",
a kernel ("GC OS") and an IPC layer. The IPC layer takes effect "at the level immediately under the standard
(bottleneck) procedures": the application calls `DrawChar`, QuickDraw calls `StdText`, and the call is queued for the
card and the application gets control back ([develop 3][dev3]). Apple describes the software as a transparent upgrade
to 32-Bit QuickDraw and gives 5 to 30 times as the gain. Whether the interception is done by trap patches is *not
stated* in the article.

What that model costs is in Apple's own documents:

- **It is asynchronous**, so everything that looks at pixels behind QuickDraw's back is a hazard. Do not draw directly
  to the screen; announce changes with `CTabChanged`, `PixPatChanged`, `PortChanged`, `GDeviceChanged`; ask `QDDone`
  before reading ([develop 3][dev3]). GWorlds were designed with this in mind: `GWorld.a` is titled "Extensions to
  Quickdraw for 32-bit Quickdraw and Graphics Accelerators" and carries an `AsyncQD` switch
  (`SM:QuickDraw/GWorld.a:97`, `:190`).
- **It falls back to the processor in named cases.** An application that replaces all the bottleneck procedures turns
  acceleration off ([develop 3][dev3]). `CopyBits` loses its parallelism when the destination has a `SearchProc`, when
  an offscreen pixel map was built by hand instead of by `NewGWorld`, and for some palette colour tables ([TN
  QD16][qd16]).
- **It followed neither the processor nor the System.** Version 1.0 for System 6; version 7.0.1 (May 1992) requires
  System 7.0, forbids System 6, is "intended for use with System 7.1 and earlier" and "not fully compatible with
  System 7.5"; on a 68040 acceleration "is permanently disabled" and the card is a frame buffer
  ([8•24 GC README][gc-readme], [Service Source copy][gc-ss], [TN HW555][hw555]). The README lists the applications
  that crash with it.
- **Benchmarks disagreed with the claim.** On a IIfx under System 7.0.1, `CopyBits` through the GC reached 92 frames
  per second where the unaccelerated one reached 19, at the largest size of the table ([develop 11][dev11]); a later
  hobbyist measurement finds 67–70 % on Speedometer ([Low End Mac][lem-gc], secondary).

### Radius QuickColor: a patch, with the inner loops on a second processor

Announced 17 July 1989 at $795: a NuBus board with a VL86C010 (an ARM2) at 10 MHz beside a Radius display interface.
Its software is "two portions of code": a multitasking operating system on the board "with routines that implement
QuickDraw inner loops", and a second part that "patches 32-Bit QuickDraw at boot time". Only "some" operations are
diverted (window movement, text scrolling, fills, image display), for 16 and 32-bit modes; the claim is up to 600 %
([announcement, Usenet 20 July 1989][qc-usenet]; [brochure][qc-brochure]). One INIT and a control panel that turns it
off. Which vectors the 1989 board's software patches is *not stated* in either document; Wikipedia says bottleneck
operations, citing *Byte*, December 1989, which was *not read* ([Wikipedia][wp-radius]). The control panel of 1994 that
carries the name is read below: it patches the loop vectors and `BitBlt`.

The name outlived the board: a "QuickColor" control panel, version 3.2.6 of December 1994, "provides the acceleration
for supported Radius cards", Power Macintoshes included, and that revision fixes a fault under KanjiTalk 7.1.2 ([Mac
Update Report, February 1995][radius-msur]). DirectColor/GX and the Radius Thunder boards: no period document on their
interception level was found. *Not confirmed.*

### SuperMac: hardware on the frame buffer, software tied to the ROM

- A $499 "Graphics Accelerator" module for the ColorCard/24, 30 October 1989: it plugs onto the display card, claims
  "all 32-bit QuickDraw operations" (scrolling, fills, moves, ovals, lines, rectangles) and 20 times, and says its
  speed is "not limited by NuBus data transfer rates" ([press release][sm-accel]).
- Thunder/8 and Thunder/24, 30 July 1991: a "custom-designed pixel accelerator" ASIC, SuperVideo 3.0, "fully
  compatible with Apple's new System 7.0" ([press release][sm-thunder]).
- Compatibility was shipped as pairs of card ROM and software: System 7 needs SuperVideo 2.7 or later; ROM 1.7.2 or
  earlier takes SuperVideo 2.7.5, ROM 3.0 takes 3.1; on Power Macintosh a separate "SuperPower" extension provides the
  acceleration ([Low End Mac, SuperMac FAQ][lem-sm], a 2018 reconstruction of the vendor's documents). A TidBITS list
  of December 1991 has "Spectrum/24 Series III: Not compatible – new ROM in December" ([TidBITS][tidbits-sm]); the
  agent that read it did not capture what the list measured compatibility with.

How SuperMac's software reached QuickDraw is *not stated* in any of these. A ROM and a control panel read below show it:
the patches are in the card's ROM.

### RasterOps and the rest

Nothing found on the interception level. The one documented fault is a card ROM, not a QuickDraw patch: bus errors with
a ColorBoard 364 in an SE/30 under System 7, fixed by a free replacement ROM ([TidBITS, January 1992][tidbits-ro]).
*Not confirmed* beyond that.

Apple's engineers summarised the field in 1994: "The Macintosh 8•24GC card attempted to accelerate entire operations,
while most third-party accelerators just concentrate on the blits" ([develop 18][dev18]).

### What the drivers themselves show

Four files of the period were read statically: resource forks listed, 68k code disassembled, the imports of one PowerPC
fragment listed. Nothing was run. They are kept in `drivers/`, outside version control (no disk image goes in the
repository), so a reference below is a resource type and ID and an offset inside it, not a line.

| File | What it holds | Date |
|---|---|---|
| RadiusWare 3.4.1, floppy image, Japanese release | `QuickColor` 3.3 control panel, Dynamic Desktop 3.4.1, Soft PrecisionColor 3.1.1, a read-me | `QuickColor` modified 8 December 1994; read-me September 1995 |
| SuperVideo 2.7.4, floppy image | SuperMac's control panel | 16 August 1993 |
| `specpdq.bin` | Declaration ROM of the SuperMac Spectrum/24 PDQ, "v1.11"; the dump is stored complemented | not dated |
| Thunder 3D / TX•1152 installer 1.0.7 | A PCI card of 1997: read-me, ROM, RAVE engines. Its extensions are inside a compressed installer and were *not read* | 19 May 1997 |

**Radius QuickColor 3.3 patches the private vectors, from tables.** Its `INIT` 0 scans the slots and loads one `QCOD`
resource for each family of boards it finds (`QCOD` 0 for a Radius Rocket, recognised by its Gestalt selector; the
families behind the others were not identified). A `QCOD` starts with three tables of eight-byte entries: a trap word,
the offset of the replacement, room for the old address. `INIT` 0 installs them with `GetToolTrapAddress` and
`SetToolTrapAddress` (`INIT` 0, `$2B2`–`$35C`), the first table always and one of the other two by System version:

| `QCOD` | Always | System before 7.0 | System 7.0 and later |
|---|---|---|---|
| 0 | `bSETUP8` `$AB34`, `bXMAIN8` `$AB38`, `bSetup0` `$AB58`, `bLeft0` `$AB59`, `rMASK8` `$AB5E`, `rXMASK8` `$AB62`, `slXMASK8` `$AB84`, `FastSlabMode` `$AB0C` | `GetSeek` `$AB0D` | nothing |
| 1 | the eight of 0, and `rMASK0` `$AB5A`, `bcMain0` `$AB3C`, `stMASK0` `$AB73`, `stMASK1` `$AB74`, `stMASK3` `$AB76`, `slMASK8` `$AB80`, `DrawLine` `$AB0A`, `GetSeek`, `UnpackBits` `$A8D0`, `StdGetPic` `$A8EE` | nothing | nothing |
| 2 | the eight of 0, and `rMASK0` | `GetSeek` | `SetUpStretch` `$AB24` |
| 4 | the eight of 0, and `rMASK0` | nothing | `SetUpStretch` |
| 5 | `BitBlt` `$AB00`, `rMASK0`–`rMASK3` `$AB5A`–`$AB5D`, `rMASK8`, `rMASK10` `$AB60`, `rXMASK8`, `rHilite` `$AB72`, `rHiliteTab` `$ABA2`, `slXMASK8`, `FastSlabMode`, `DrawLine` | nothing | `SetUpStretch` |

These are the loop vectors QuickerDraw patched in 1988, and five of them (`bSETUP8`, `bXMAIN8`, `bSetup0`, `rMASK8`,
`rXMASK8`) are the ones Display found replaced by System 7.5.5.

**How it handles versions**, all of it read in the code:

- **At startup it refuses, it does not adapt.** `INIT` 0 leaves unless Gestalt `'sysv'` is at least `$0604`, `'qd  '` is
  above `$200` and `'sysa'` says 68k (`$1A`–`$4E`). There is no upper bound: nothing in it knows of a System it should
  not run on.
- **The patch set depends on the System**: the second or the third table, by comparing `'sysv'` with `$0700` (`$2F8`).
  `GetSeek` is patched on System 6 only, `SetUpStretch` on System 7 only.
- **Each patch asks QuickDraw's version again.** A `QCOD` keeps the answer to `'qd  '` and its patches test it on entry.
  The three `st` loop patches of `QCOD` 1 jump to the original when it is `$230` or more (`$1394`, `$16F0`, `$1748`):
  served on 32-Bit QuickDraw 1.2, declined on System 7's. The region loop patches test one more word of QuickDraw's
  frame, `-490(A6)`, when it is below `$220` (`QCOD` 1 `$402`, `$970`, `$C6C`; `QCOD` 2 `$99C`; `QCOD` 4 `$89A`).
- **Every patch can say no, and can be switched off while running.** Each begins by testing a state word (off, on, or
  "on when Caps Lock is up", which reads the key map at `$17B`) and ends a refusal by pushing the saved address and
  returning to it.
- **It reads QuickDraw's stack frame.** The `BitBlt` patch (`QCOD` 5, `$F6`) takes the clipped rectangle at `-116(A6)`
  and the mode at `-152(A6)`, and stores its own values in the frame (`-504(A6)`). It refuses a rectangle narrower than
  eight pixels, a register value I read as a depth below 8 bits, arithmetic modes, pattern modes other than copy and
  XOR, a destination whose base address is not in a slot holding one of its boards, and a source transfer whose source
  is not on the same board.

**Its PowerPC half uses the formal hook, in 1994.** The data fork of the same file is a PowerPC fragment that imports
five symbols from `InterfaceLib`: `PBStatusSync`, `GetKeys`, `SNextTypeSRsrc`, `SRsrcInfo` and `NQDMisc`. The
acceleration hook of the native QuickDraw therefore existed on NuBus Power Macintoshes by December 1994, before any PCI
machine, and one control panel carried both models: vector patches for the 68k QuickDraw, `NQDMisc` for the native one.
What it registers was not read (the code section is packed).

**SuperMac put the accelerator in the card's ROM.** In ROM 1.11 of the Spectrum/24 PDQ a routine (`$BA1A`) installs a
table of ten patches (`$BAF6`): `BitBlt` `$AB00`, `RgnBlt` `$AB1F`, `ScaleBlt` `$AB96`, `SetUpStretch` `$AB24`,
`SlabMode` `$AB25`, `FastSlabMode` `$AB0C`, `DrawLine` `$AB0A`, `UnpackBits` `$A8D0`, `StdGetPic` `$A8EE` and the OS
trap `AttachVBL` `$A071`; it then replaces `FastLine` `$AB9C` (`$BA76`). Its conditions are that trap `$AB03` is
implemented, which is the test for 32-Bit QuickDraw, and a bit of the board's slot parameter RAM (`$B80E`–`$B86A`). The
ROM contains no test of `'sysv'` or `'qd  '`. Before installing, it enumerates the files of a directory (which one was
not traced), runs the `'RDRP'` resources of any file of type `'RDRP'`, and installs nothing of its own if one of them
answers (`$B980`): a ROM that lets a file on disk take its place. Its patches work like Radius's: they read the clipped
rectangle at `-116(A6)`, write into QuickDraw's frame (`-600(A6)`, `-520(A6)`), honour the same Caps Lock test, and
decline through the saved address. By a rough count, some fifty offsets from A6 appear in both vendors' code.

**SuperVideo 2.7.4 patches none of the drawing vectors.** Its `INIT` 7777 asks Gestalt `'addr'` for the addressing mode
and loads an `SOff` resource (IDs 24 and 32 exist; the path to 32 was read). `SOff` replaces `QDExtensions` `$AB1D`, the
dispatcher of the GWorld calls, and puts patches on `NewHandle` and `DisposeHandle` in and out around it; it compares
`'sysv'` with `$0700` and tests a bit of `'os  '`. What it does with the buffers was not traced. The panel that goes
with it is named "GWorlds"; the "Accelerator" panel only speaks to the ROM, through a Gestalt selector `'mswt'` that the
ROM registers. The control panel refuses a Spectrum card whose firmware is older than 1.9 (`STR ` -3241).

**Which Systems they say they run on**

| Software | Date | What it states or enforces |
|---|---|---|
| QuickColor 3.3, 68k | December 1994 | Enforced: System 6.0.4 or later, QuickDraw above `$200`. No upper bound |
| RadiusWare 3.4.1 read-me (Japanese; my translation) | September 1995 | System 7.1 or KanjiTalk 7.1 recommended, runs from 6.0.7. On a II, IIx, IIcx or SE/30 under 6.0.7 or 6.0.8, 32-Bit QuickDraw 1.2 or later is required. Tested on the Macintosh II family, LC 475, Centris, Quadra, Duo with a dock, Power Macintosh |
| Soft PrecisionColor 3.1.1 (same read-me) | 1994 | For System 7.1.1 and earlier; not needed from 7.1.2, where the Display Manager does the same, and unusable once Apple's display software is installed; KanjiTalk 7.5.1 promised for a later version |
| MrFlash ROM updater read-me | February 1995 | System 7.1 or later, and not a II, IIx or IIcx ("not 32-bit clean") |
| SuperVideo 2.7.4 | August 1993 | Nothing stated on the disk. In code: one branch at System 7.0, one on the addressing mode |
| Spectrum/24 PDQ ROM 1.11 | not dated | Enforced: 32-Bit QuickDraw present. Nothing about the System |
| Thunder 3D 1.0.7 read-me | May 1997 | A list of PCI machines it does and does not work in; QuickDraw 3D 1.0.6 to 1.5.1. No System version stated |

The same read-me explains a benchmark, in terms that match Okapia's measurements. Norton Utilities 3.1's System Info
rates Radius cards low, it says, because it times many QuickDraw commands that graphics applications do not use; the
accelerator was tuned for the commands that matter in Photoshop and the like, and as a result some of the others have
become slower.

### What Apple said about patching

- Patching is allowed and discouraged: think about whether you need it; use `GetTrapAddress` and `SetTrapAddress`;
  "Do not access the trap tables directly"; assume nothing about ROM entry points ([TN OV13][ov13], 1993). A patch runs
  in anybody's context and must not depend on A5 ([TN OV14][ov14], 1986).
- Nothing public was found that documents the QuickDraw hooks or forbids them by name. The public way to change
  QuickDraw's drawing is the bottleneck record of a port ([Imaging With QuickDraw][im-custom]).
- To applications: writing to video memory "is unsupported"; and with an accelerator present "you may not be able to
  beat QuickDraw at all" ([develop 11][dev11]).

### What broke

| Event | Effect | Source |
|---|---|---|
| A patch three months old | Apple fixes edge masks, crashes in `CopyBits`, A/UX in QuickerDraw | `QuickerDraw.a:57`–`71` |
| 68040 (1991) | 8•24 GC acceleration permanently off | [README][gc-readme] |
| System 7.5 (1994) | 8•24 GC software "not fully compatible" | [Service Source][gc-ss] |
| System 7 | SuperMac needs SuperVideo 2.7; a Spectrum/24 Series III needs a new ROM | [FAQ][lem-sm], [TidBITS][tidbits-sm] |
| QuickDraw 1.3.5 on Power Macintosh (1994) | "Version 1.3.5 doesn't use these trap vectors, which disables most accelerator cards"; the frame buffer still works; any 68k patch on a QuickDraw routine now costs a mode switch | [develop 18][dev18] |

The last line is the end of the patch model. It was not a version check that failed: the vectors were still there and
the patches still installed. QuickDraw had simply stopped calling them.

### The formal answer: a hook with a right to decline

On a Power Macintosh a display driver registers acceleration with the native QuickDraw. A Radius control panel of
December 1994 already imports the call (above), so the hook is as old as the NuBus Power Macintoshes, not the PCI ones.
SheepShaver does it in `external/macemu/SheepShaver/src/gfxaccel.cpp`:

- It finds `NQDMisc` in `InterfaceLib` and calls it with selector 6 and a record of three words: a hook, a
  synchronisation routine, and an operation code (`gfxaccel.cpp:426`–`460`, `include/video_defs.h:395`). It registers
  one for `ACCL_BITBLT`, one for `ACCL_FILLRECT`, and a hook that always declines for the six other codes.
- For each operation QuickDraw calls the hook with one parameter block: transfer and pen modes, colours, and for source
  and destination the base address, row bytes, bounds, pixel type and size, colour table, and the rectangles
  (`video_defs.h:319`–`367`). Clipping and colour setup are done.
- **The hook answers yes or no.** If it accepts, it stores a drawing routine in the block and QuickDraw calls it; if
  it returns false QuickDraw draws. SheepShaver accepts same-depth `srcCopy` at 8 bits and more, and rectangle fill and
  invert (`gfxaccel.cpp:294`–`317`, `:384`–`403`).
- The synchronisation routine is how the system waits for a card that draws on its own.

Apple's side of it is thinly documented in public. A 1999 Technical Q&A answers a card developer about how the "NQD
HWA handler" receives `notSrcCopy` (to the bit-blit function when depths and tables match, to the scale-blit function
otherwise, with `bMustScale` and `scaleTable`) and so confirms the interface and two of its operations ([Q&A
QD60][qd60]). No public header or programming guide was found, and SheepShaver's structure has thirteen fields named
`unknown`: whether the interface was given to card makers under licence is *not confirmed*. The Mac OS release that
introduced it is *not confirmed*; it was in use by December 1994.

### The three models

| | Patch | Port | Formal hook |
|---|---|---|---|
| Examples | QuickerDraw, Radius QuickColor on 68k, SuperMac's ROM, "most third-party accelerators" | 8•24 GC | Power Macintosh from 1994: Radius QuickColor on PowerPC, SheepShaver |
| Level | private vectors: routines and row loops | below the documented bottlenecks; a whole QuickDraw on the card | one call per operation, after QuickDraw's setup |
| Contract | none; the frame and registers of one build | the public bottleneck calling sequences, and all of QuickDraw's semantics | a parameter block defined by Apple |
| Unsupported case | the patch must recognise it and jump to the original | the port must implement it or switch itself off | the hook returns false |
| Cost to the vendor | re-verify at each System and ROM | maintain a second QuickDraw | implement the cases worth it |
| How it ended | QuickDraw stopped calling the vectors | did not follow the 68040 or System 7.5 | still what SheepShaver registers on the Mac OS releases it runs |
| What it cannot speed up | QuickDraw's setup, which still runs on the processor ([develop 18][dev18]) | anything the application does behind QuickDraw | whatever QuickDraw does not offer to the hook |

## What this means for Okapia's `qdaccel`

**The loops at `$1AC0` are traps, and they were the accelerators' level.** `$1AC0` is `$E00 + $330×4`: `bMAIN0`. The
entries Display lists as replaced by System 7.5.5 are `bSETUP8` (`$334`), `bXMAIN8` (`$338`), `bLONG8`, `bXLONG8`,
`rMASK8`, `rXMASK8` and `bSetup0` (`$358`). QuickerDraw patched this table, Apple's alpha and `MOVE16` patches use it,
and Apple says third-party cards concentrated on the blits. `qdaccel` hooks one level above, at `BitBlt` and `RgnBlt`,
and therefore stands in for whichever loop the dispatcher would have chosen: reading the entries at installation, as it
does, is not a precaution, it is the price of that level. Whether hooking the loops themselves would be simpler was not
studied here; their register contract is as private as the frame's.

**The vendors hooked what `qdaccel` hooks, and read the same frame.** SuperMac's ROM takes `BitBlt`, `RgnBlt` and
`ScaleBlt`; Radius takes `BitBlt` and the row loops under it; both read the clipped rectangle and the mode through A6
and both even write into QuickDraw's frame. `qdaccel` is the patch model as the trade practised it, with one difference
in its favour: it checks the code it stands in for, where the cards checked a version number or, for SuperMac's ROM,
nothing but the presence of 32-Bit QuickDraw.

**For slabs, both vendors patched the selector and not the worker.** Neither table contains `DrawSlab`. Both contain
`FastSlabMode`, and SuperMac's `SlabMode` too: the routine a shape calls once to get the address of the slab loop for
its mode ("GET DrawSlab CASE JUMP IN A4", `QD84:DrawLine.a:472`). An accelerator that answers there decides once for
each shape and is never asked for each scan line. Okapia's `DrawSlab` hook is asked 21.8 million times in one MacBench
pass and serves none. *A hook on `SlabMode` and `FastSlabMode` is the period's answer to that; it has not been tried
here, and under the translator the loop it hands back would still be a journey out of translated code for each line.*

**A version number was enough for them and is not for us.** QuickColor's patches test `'qd  '` against `$220` and
`$230`, the only values there were, and nothing tells 7.1 from 7.5. It also patches five loops that System 7.5.5
replaces for alpha drawing; installed after the System, it runs first, and the System's alpha test runs only on its
refusals. *Inferred from the two tables, not run.* It is the hazard Display already records for the native path, and the
reason the trap-table entries are part of what `qdaccel` checks.

**A switch that works while the guest runs.** Both vendors test Caps Lock on every call, so a user could see the same
screen drawn with and without the card in one session. `qdaccel` is chosen at startup. A run-time switch would make the
comparisons Display asks for (five alternating runs) cheaper, and a drawing fault easier to pin on a hook. *A
suggestion; not built.*

**Fingerprints are the period's own method, and Gestalt would not replace them.** Gestalt answers `$230` for every
System Okapia runs, and the ROM word answers `$067C` for the whole IIci family. Apple's installer fingerprinted its
caller's instructions; vendors tied a software revision to a ROM revision. Okapia's checks on the ROM checksum, on the
`StretchBits` code and on the trap-table entries are the same thing done at run time, and they are what a vendor's
compatibility list was.

**A hook that cannot decline cheaply should not be installed.** The formal interface asks once per operation and takes
no for an answer; the GC switched itself off when an application replaced the bottlenecks. Okapia's tail-jump to the
guest is the patch model's version of that answer, and its cost is real: `DrawSlab`, declined 21.8 million times, is
the measured case. The rule Display already states, a size gate in 68k and no hook that cannot serve, is what the PCI
design builds in by asking per operation and not per scan line.

**Accelerating the blit leaves the setup.** Apple's engineers wrote that with an accelerator "you're still forced to
pay for the setup time", independent figures for the GC were far under the claim, and Radius's own read-me admits
that commands outside its target became slower. Okapia's figures say the same:
large copies gain a factor of two, text a tenth, and under the translator the gain depends on the journey out of
translated code. A wider hook will not change that; only taking over more of `StretchBits` would, and that is the port
model, with its cost.

**Stay synchronous.** Every compatibility rule Apple had to publish for the GC (no direct screen access, `QDDone`,
GWorlds only, the `CopyBits` exceptions) comes from drawing after the call has returned. `qdaccel parallel` joins its
worker before the guest resumes, and should keep doing so: the 68k guest has no GC-aware application to rely on.

**The stable interface is the expensive one.** The bottleneck procedures are documented and identical in every
QuickDraw, and taking over there means implementing clipping, patterns, colour mapping and cursor protection, which is
what the GC did with a second QuickDraw. The private vectors are cheap to serve and belong to one build. Apple's later
hook put a defined contract at the cheap level, and it exists only in the PowerPC QuickDraw. For the 68k engine there
is no third choice; for the PowerPC engine the hook is already linked, and verifying that it engages is an open item
in [PowerPC engine](../../topics/engine-powerpc.md).

**Study versions as a census, not as a history.** The useful object is a table, per System release Okapia supports, of
every vector from `$AB00` to `$ABA8`: ROM address or RAM address, and for RAM a fingerprint. It would say at once
which hooks are safe on 7.1, 7.5.5, 7.6.1, and would settle 8.0 and 8.1, about which nothing is known here. It can be
dumped from a guest with the existing probe machinery. Reading more of QuickDraw's general history would not add to
that. *This recommendation is mine; it was not asked for and nothing has been built.*

## Not confirmed

- Which vectors the 1989 QuickColor board's software and the RasterOps accelerators patched. For Radius in 1994 and
  for one SuperMac ROM the tables were read (above); which board family each Radius `QCOD` serves was not.
- What SuperVideo's `SOff` does with GWorld buffers, what the PowerPC half of QuickColor registers through `NQDMisc`,
  and the contents of the Thunder 3D installer.
- *Byte*, December 1989, "The QuickColor Option" and the *Macworld* and *MacUser* reviews of accelerated boards: not
  read.
- What System 8.0 and 8.1 patch on a `$067C` ROM, and what Gestalt `'qd  '` returns on 7.5.5, 7.6.1 and 8.x.
- Whether Radius distributed QuickerDraw; which System release first contained it (6.0 per the interview, 6.0.3 per
  the source file's first line).
- The licence status of the `NQDMisc` acceleration interface and the release that introduced it; the QuickDraw
  version number that 1.3.5 reports through Gestalt.
- The *develop* 3 article was read in a third-party transcription, not in Apple's scan.

## Sources

Apple, primary:

- [develop 3][dev3], G. Ortiz, "Macintosh Display Card 8•24 GC: The Naked Truth", July 1990 (transcription)
- [develop 11][dev11], B. Stevens and B. Guschwan, "Writing Directly to the Screen", August 1992
- [develop 18][dev18], K. Othmer, S. Holland and B. Cox, "Exploiting Graphics Speed on the Power Macintosh", June 1994
- [TN QD01][qd01], "32-Bit QuickDraw: Version 1.2 Features", April 1990
- [TN QD16][qd16], "8•24 GC QuickDraw and Deaccelerated CopyBits", February 1991
- [TN HW555][hw555], "Video Hardware Q&As"
- [TN OV13][ov13], "10+ Commandments", June 1993; [TN OV14][ov14], "A5 Within Trap Patches", June 1986
- [Q&A QD60][qd60], "Native QuickDraw Hardware Acceleration notSrcCopy Blits", June 1999
- Inside Macintosh: [Gestalt Manager summary][im-gestalt], [Using the Gestalt Manager][im-gestalt-use],
  [Using Color QuickDraw][im-cqd], [customising QuickDraw's bottlenecks][im-custom]
- [8•24 GC README][gc-readme] (version 7.0.1, Developer CD February 1995) and the [Service Source copy][gc-ss]
- [System 7.5 Update 2.0, "New in This Update"][sys753]
- [Computer History Museum][chm], "MacPaint and QuickDraw Source Code", 2010; the source itself in
  `reference/quickdraw-1984`: `Bitmaps.a`, `Rects.a`, `RgnBlt.a`, `DrawLine.a`, `DrawText.a`, `QDPatch.a`
- `reference/supermario/base/SuperMarioProj.1994-02-09`: `QuickDraw/QDHooks.a`, `QuickDraw/GWorld.a`,
  `QuickDraw/Patches/QuickerDraw.a`, `QDciPatchROM.a`, `QuickDrawPatches.a`, `QuickDraw/JacksonPollock/`,
  `QuickDraw/QuickDraw Release Notes`, `OS/DispTable.a`, `OS/Gestalt/GestaltFunction.a`,
  `Interfaces/AIncludes/GestaltEqu.a`, `Internal/Asm/PatchMacros.a`, `LinkedPatchMacros.a`

Manufacturers and period press:

- [Radius QuickColor announcement][qc-usenet], Usenet, 20 July 1989; [Radius QuickColor brochure][qc-brochure],
  July 1989
- [SuperMac Graphics Accelerator][sm-accel], 30 October 1989; [SuperMac Thunder][sm-thunder], 30 July 1991
- [MacTech 4.6][hertzfeld], "Andy Hertzfeld on QuickerDraw", 1988
- [Mac Update Report, Radius][radius-msur], February 1995
- TidBITS: [SuperMac compatibility list][tidbits-sm], December 1991; [RasterOps 364][tidbits-ro], January 1992

Later, secondary: [Low End Mac on the 8•24 GC][lem-gc], [Low End Mac SuperMac FAQ][lem-sm], [Wikipedia, Radius
Inc.][wp-radius].

Local: `external/macemu/SheepShaver/src/gfxaccel.cpp`, `include/video_defs.h`, `video.cpp:1074`; in `drivers/`
(untracked), `RadiusWare3.4.1.img`, `SuperVideo 2.7.4.img`, `Thunder3D:TX1152 Installer.img`, `nb_spdq.zip`.

[dev3]: https://gferrara.github.io/apple-develop-articles/1990/july_90_macintosh_display_card_8_24_gc_the_naked_truth.html
[dev11]: https://discmaster.textfiles.com/file/12652/www.mactech.com.tar/www.mactech.com/articles/develop/issue_11/059-064_Graphics.html?html=true
[dev18]: https://discmaster.textfiles.com/file/12652/www.mactech.com.tar/www.mactech.com/articles/develop/issue_18/043-054_Othmer_REV.html?html=true
[chm]: https://computerhistory.org/blog/macpaint-and-quickdraw-source-code/
[qd01]: https://developer.apple.com/library/archive/technotes/qd/qd_01.html
[qd16]: https://developer.apple.com/library/archive/technotes/qd/qd_16.html
[hw555]: https://developer.apple.com/library/archive/technotes/hw/hw_555.html
[ov13]: https://developer.apple.com/library/archive/technotes/ov/ov_13.html
[ov14]: https://developer.apple.com/library/archive/technotes/ov/ov_14.html
[qd60]: https://developer.apple.com/library/archive/qa/qd/qd60.html
[im-gestalt]: https://developer.apple.com/library/archive/documentation/mac/OSUtilities/OSUtilities-27.html
[im-gestalt-use]: https://developer.apple.com/library/archive/documentation/mac/OSUtilities/OSUtilities-11.html
[im-cqd]: https://discmaster.textfiles.com/file/32815/Dev.CD%20Sep%2000%20RL%20Disk%202.toast/TechnicalPublications/quicktime/qtdevdocs/INMAC/MACWIN/imClrQuickDraw.5.htm?html=true
[im-custom]: https://developer.apple.com/library/archive/documentation/mac/QuickDraw/QuickDraw-179.html
[gc-readme]: https://discmaster.textfiles.com/view/32719/Dev.CD%20Feb%2095/Dev.CD%20Feb%2095.toast/Tool%20Chest/Hardware/Display%20&%20Video%20Hardware/8%E2%80%A224GC%207.0/8%E2%80%A224GC%20README
[gc-ss]: https://discmaster.textfiles.com/view/30834/Service%20Source%202.0%201996%20Companion.iso/Apple%20SW%20Updates/Macintosh/Display%20and%20Peripheral/8%E2%80%A224%20GC%20Software
[sys753]: https://discmaster.textfiles.com/view/31952/EV1NET.ISO/mac/Programs/Apple%20System%207.5%20Updates/system%207.5.3%20Install/Important%20Information/New%20in%20This%20Update%20-%203
[qc-usenet]: https://utzoo.superglobalmegacorp.com/usenet/b130/comp/sys/mac/35017.txt
[qc-brochure]: https://archive.org/details/radius-quickcolor-graphics-accelerator-macintosh-ii-brochure
[sm-accel]: https://discmaster.textfiles.com/view/1963/BBS%20in%20a%20Box%20-%20Macintosh%20-%20Volume%20IV%20(January%201992)%20(BBS%20in%20a%20Box).iso/Files/Word/H/Graphics%20Accelerator%20for
[sm-thunder]: https://discmaster.textfiles.com/view/1963/BBS%20in%20a%20Box%20-%20Macintosh%20-%20Volume%20IV%20(January%201992)%20(BBS%20in%20a%20Box).iso/AMUG%20Info/AppleLink/SuperMac%20Announces%20Thunder
[hertzfeld]: https://discmaster.textfiles.com/file/12652/www.mactech.com.tar/www.mactech.com/articles/mactech/Vol.04/04.06/Hertzfeld/index.html?html=true
[radius-msur]: https://discmaster.textfiles.com/view/27006/macformat-027.iso/mac/Mac%20Update%20Report/MSUR%20files-Feb%2095/Update%20Database%20Files-Feb%2095/Radius.12
[tidbits-sm]: https://tidbits.com/1991/12/09/mailbits09-dec-91/
[tidbits-ro]: https://tidbits.com/1992/01/06/rasterops-364-video-bug/
[lem-gc]: https://lowendmac.com/1990/macintosh-display-card-8-24gc/
[lem-sm]: https://lowendmac.com/2018/supermac-spectrum-video-card-faq/
[wp-radius]: https://en.wikipedia.org/wiki/Radius_Inc.
