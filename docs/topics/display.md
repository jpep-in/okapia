# Display

> From the Macintosh's frame buffer to the HDMI output: the output mode, the resolutions offered, the
> compositor that converts, scales and redraws only what changed. Main files:
> `src/common/video_shared_okapia.{h,cpp}` (rules shared by both engines), `src/common/video_sizes_okapia.{h,cpp}`,
> `src/common/compositor_okapia.{h,cpp}`, `src/common/video_okapia.cpp` (68k), `src/common/sheepshaver/video_okapia.cpp`
> (PowerPC), `src/hosts/circle/firmware/okapia_output.cpp`, `patches/macemu/0003`, `0011`.

## Using it

- **The output frame buffer** comes from the Pi firmware, which normally chooses the display mode from the
  monitor's EDID. `okapia_output.cpp` logs the frame buffer size and the EDID's preferred size. If they differ,
  check the HDMI signal in the monitor's input information and look for active `hdmi_group`, `hdmi_mode`,
  `hdmi_cvt`, `hdmi_timings` or `framebuffer_width` / `framebuffer_height` overrides on the card.
- **For a 1920×1200 monitor on a Pi 4**, try the automatic mode with the overrides commented out. If the Pi
  still chooses the wrong HDMI timing, `hdmi_group=2` and `hdmi_mode=68` select the DMT 1920×1200 mode at
  60 Hz with reduced blanking. Keep `disable_overscan=1` and verify the timing on the monitor.
  [Raspberry Pi HDMI mode table](https://www.raspberrypi.com/documentation/computers/legacy_config_txt.html#hdmi_mode)
- **A monitor the firmware drives at the wrong mode** can be told in `config.txt`. Seen on a Philips 246B1 whose
  EDID prefers 2560×1440 and which the firmware drove at CEA 1080p: `hdmi_group=2`, `hdmi_mode=87`,
  `hdmi_cvt=2560 1440 60 3 0 0 1`, `max_framebuffer_width=2560`, `max_framebuffer_height=1440`. Keep
  `disable_overscan=1`: without it a 1920×1080 output came back as 1824×984 inside a black frame.
- **Resolutions** are chosen in the Mac's **Monitors** control panel (Mac OS 7.5 and later list them). Both
  engines offer the same sizes, those that fit the output unscaled: 512×384, 640×480, 800×600, 1024×768,
  1152×870, 1280×720, 1280×960, 1280×1024, 1600×900, 1600×1200, 1920×1080, 1920×1200, 2560×1440, plus the
  display's own size and the `screen` preference's when not listed. All six depths up to what 16 MB holds.
- **Display in the boot menu** shows the output frame buffer, the monitor's preferred EDID mode, and whether the
  card's top-level `config.txt` contains HDMI mode directives. Its **Monitors** column controls which sizes the
  Mac is offered. Its **Startup** column sets the initial size for the next Macintosh start. Sizes which exceed
  the output stay listed in the boot menu so their visibility preference can still be edited. 640×480 remains
  available for recovery when it fits.
- **Custom Mac sizes** are added with width and height on the Display page. Width is a multiple of eight so packed
  indexed modes have whole pixels and bytes at the end of each row. The **New resolution…** row opens a dialog to enter and validate the size. The page limits new entries to six, and shows
  the compositor's integer scale beside each available size. The HDMI diagnostics are read-only.
- **Starting resolution**: `screen win/<width>/<height>` (upstream's spelling; `0` = the display's). On System 7.1,
  whose Monitors panel shows no resolution list, this is the way to pick one.
- **Scaling**: the Mac's screen is scaled by the largest integer that fits and centred — 1280×720 fills a 2560×1440
  monitor at ×2. An integer factor keeps the picture sharp; a slow pointer then moves in steps of that factor.
  A 1920×1200 output fills at ×1 with a 1920×1200 Mac screen, or at ×2 with `screen win/960/600` for larger text.
- **Refresh** is in the Mac display group. `frameskip 0` (dynamic, the default) holds the compositor to about an eighth of wall time.

## How it works

### One screen, two Macintosh, one set of rules

The engines present the display differently — Basilisk hands the platform a `monitor_desc` and a `video_mode`
list; SheepShaver runs the Mac's own native driver and wants a `VModes[]` table and a frame buffer at a Mac address
— but everything between those contracts and the compositor is the same question asked twice. It *was* written
twice, and the copies drifted: only one repeated the palette across 256 entries, and the other drew 4 and 16
colours as coloured noise. `video_shared_okapia.cpp` states each rule once — which modes fit, which converter a
depth needs, the palette, the refresh rate, the reports — and each `video_okapia.cpp` is its engine's contract and
nothing else. **A display fix goes in the shared file unless it is about one core's vocabulary.**

### The output

`FwOutputClaim()` claims a `CBcmFrameBuffer (0, 0, 32)` once for the life of the board; the firmware draws the boot
menu on it and the compositor borrows it. A mode change in Monitors updates the Mac's buffer and the
compositor's plan. Circle queries the Pi firmware for the default frame buffer size
(`PROPTAG_GET_DISPLAY_DIMENSIONS` in `bcmframebuffer.cpp`). Okapia queries EDID separately
(`PROPTAG_GET_EDID_BLOCK`) to log the maker, name and preferred dimensions.

**Pixel order** is asked of the firmware (property tag `0x00040006`): QEMU gives red in the low byte, a real Pi blue.
The palette masks, the expansion map's shifts and the direct-mode converters follow it — a fix written for QEMU drew
the wrong colours on the Pi until then.

### The mode list

`VideoScreenSizeList()` (`video_sizes_okapia.cpp`, pure, host-tested) builds the list: the thirteen standard sizes,
then the display's size and the preference's, each only if not listed. Each engine supplies its identifiers:
Basilisk numbers `0x80…0x8C` then `0x8D`, `0x8E`; SheepShaver keeps upstream's Apple names where they exist and
numbers the rest past `APPLE_CUSTOM` (`patches/macemu/0011`). **A fixed size keeps its identifier whatever monitor
is plugged in**, because Mac OS 7.5 and later store the Monitors choice by identifier in the Preferences folder, not
in PRAM.

Custom slots are added after the built-in list. Each slot has a reserved ID in each driver, so hiding a size,
changing the startup choice, or moving the card to a different monitor cannot reassign an ID belonging to a custom
slot. When a custom size matches a legacy extra it takes that size's place in the list under its own ID. The
visibility filter works by dimensions and is shared by both drivers (`video_shared_okapia.cpp`). At the first
Macintosh start Basilisk reserves space for the active output up to the existing 16 MB ceiling per buffer, so a
size enabled on a subsequent guest restart fits the same allocations.

`VideoScreenEnumerate()` keeps a size only if it fits the output unscaled, and a depth only if its frame fits the
16 MB buffer (2560×1440 in millions of colours takes 14.1 MB). Basilisk allocates that buffer on the heap;
SheepShaver's is part of the Mac's memory block ([PowerPC engine](engine-powerpc.md#memory-layout)).

### The compositor

1. **Conversion**: `Screen_blitter_init()` picks upstream's `Blit_Expand_{1,2,4,8}_To_32` or the direct-mode
   converters (`CrossPlatform/video_blit.cpp`), fed by `ExpandMap[256]`. `VideoScreenPalette()` **repeats** the Mac's
   palette over all 256 entries: `Blit_Expand_4_To_32` reads `ExpandMap[c >> 4]` then `ExpandMap[c]` unmasked, so
   entry `i` and `i & (n−1)` must hold the same colour. The 1-bit expanders ignored the palette entirely until
   `patches/macemu/0003`. Grey and black-and-white come free: the core applies the luminance mapping before handing
   the palette over (`video.cpp:569`).
2. **Scale and centre** by the largest integer that fits.
3. **Only what changed**: a 16×16 tile grid, a shadow copy of the last frame, `memcmp` per tile; adjacent dirty
   tiles in a row are converted as one span. A mode or palette change sets `bFullRedraw`.
4. **Never draw from a partial scan.** A frame first looks at a cheap set — where the screen has been moving
   (each tile kept in it for twelve frames after it stops, `WatchFor[16][16]`), a one-tile margin, and a rotating
   eighth of the rest. If it finds nothing, nothing is drawn and nothing can tear. The moment it finds anything,
   a second pass compares every remaining tile before a single pixel goes out, so a change shows whole.
   `nFullScans` in the report counts frames that took the second pass.
5. **Announced regions**: SheepShaver's accelerated QuickDraw paths call `video_set_dirty_area()`; announced tiles
   are redrawn without comparison, the rest still compared (it is a hint, not the whole story).

### Writing to the output safely

**The output frame buffer is Device memory.** `translationtable64.cpp:145` gives `ATTRINDX_DEVICE` to every page at
or above the ARM's share of RAM, and the GPU's buffer is there. Device memory requires naturally aligned accesses,
so a wide NEON store at an arbitrary offset is an alignment fault (`EC 0x25`, `DFSC 0x21`). The compositor scales a
row into RAM, then writes it with `OutputPut()`/`OutputClear()`: 32-bit stores up to a 16-byte boundary, `memcpy`
or `memset` for whole 16-byte blocks, 32-bit stores for the tail, compiled without tree vectorisation. The compositor
output test in `tests/host/check_platform.cpp` covers odd origins and pitches that are not multiples of 16.
`src/kernel/Makefile` keeps `-O2`: `-O3` vectorised `GfxBlit()` into the same fault, and measured slower.

### Refresh rate

`frameskip` follows upstream's Window Refresh Rate menu: 1 = 60 Hz, 2 = 30, 4 = 15, 6 = 10 (upstream's default),
8 = 7.5, 12 = 5, and **0 = Dynamic**. Dynamic re-measures every second and composites one VBL in
`composite_time × 8 / 16,667 + 1`, capped at 12 (`Retune()`, `video_shared_okapia.cpp:410`). The Mac draws its own
cursor into its own buffer, so the pointer can never move more often than the compositor runs: a 10 Hz screen reads
as a laggy mouse. Report the composite rate, not the VBL rate — calling 55 VBL/s "fps" once hid a 9 Hz screen.

### 68k QuickDraw

The [Color QuickDraw call map](../notes/research/quickdraw-8bit.md) follows
the drawing procedures to their pixel loops and lists the 8-bit coverage work.

`qdaccel sync` uses ARM64 for the boolean transfers of `BitBlt` and `RgnBlt`
at every depth, 1 to 32 bits: a source or a pattern, copy, OR, XOR and BIC,
inverted or not, with or without a foreground and a background colour.
Shapes, text and conversions between depths are covered at 8 bits only, and
partly. The default is `off`. This experiment uses the
`uae_cpu_2021` core, interpreter or translator, and ROM `F1ACAD13`. At first guest idle,
`quickdraw_okapia.cpp` installs a system-heap trampoline for the private
`BitBlt` Toolbox trap (`$AB00`) if it still points to the ROM original.
The vector is written into the Toolbox trap table (`$E00`) itself: see the
Process Manager pitfall below.
The ROM caller returns to `ROMBaseMac + $36e40`. For the RAM caller,
Okapia resolves the live `StretchBits` trap (`$AB27`) and fingerprints the
code from entry through the return branch. The System 7.1 and 7.6.1 images
used for testing load the same routine at different addresses. Both use its
relative return offset `$6de`. Systems with a different patch signature
fall back to the guest handler for their RAM calls.
Both callers do palette selection, clipping and cursor protection before
the call and finish it afterwards. Unknown callers and unsupported transfers
tail-jump to the original handler with the saved registers and flags.

`quickdraw_blit.cpp` validates the full pixel spans against ordinary Mac RAM
and the active guest framebuffer. It takes the final clip and expanded
pattern from the verified `StretchBits` frame. The 8-bit `RgnBlt` hook
intersects the three region streams and shares the span writer. Overlapping
copies on one surface use ordered rows and a saved source row for region
spans. The `DrawSlab` hook handles the verified ROM call sites and four
ROM mode dispatchers that use a byte mask and a short pattern.
`ScaleBlt` (`$AB96`) handles same-size RGB32-to-indexed-8 `srcCopy` with
the current device's valid RGB inverse table and the guest's clip.
`stScanLoop` (`$AB99`) handles same-palette indexed-8 `srcCopy` with
a same-size 1-bit stencil or an unmasked full-rectangle expansion by
factors of 1, 2 or 4 on each axis. It returns through QuickDraw's own
cleanup. The ROM and the known RAM paths have separate frame checks.
Dithering, palette remapping, gray inverse tables, other resizes and
masks, alpha and indexed arithmetic continue through the guest.
The compositor continues to see all guest drawing through its shadow scan.

What each hook takes, by depth of the destination. Everything else stays with the guest, at any depth:
arithmetic and highlight modes, dithering, palette remapping, alpha, and every change of depth other than
the two named here.

| Destination | `BitBlt` and `RgnBlt` | `DrawSlab` | `ScaleBlt` | `stScanLoop` |
|---|---|---|---|---|
| 1, 2, 4 bits | copy, OR, XOR, BIC of a source or a pattern, inverted or not, through the colour loops or not | nothing | nothing | nothing |
| 8 bits | the same | four mask modes at three ROM call sites, none of which System 7.5.5 uses | `srcCopy` from 32 bits through the device's inverse table | `srcCopy` from 8 bits, same palette: a 1-bit stencil, or a ×1, ×2, ×4 expansion |
| 16, 32 bits | the same, alpha excepted | nothing | nothing | nothing |

`StretchBits` itself is not hooked: clipping, cursor protection, pattern expansion and the choice of path
stay the guest's.

**One writer for six depths.** A boolean transfer is the same operation on every bit, so depth only decides
where a row starts and ends. `quickdraw_blit.cpp` counts in bits: a pixel is `1 << shift` of them, and a
rectangle has a first bit in its first byte (`destinationBit`, `sourceBit`). From 8 bits up both are zero
and a row is whole bytes for the span writers. Below, a row may start and end inside a byte: those two
bytes take the same operation on a copy of themselves and only the span's bits are kept; and when the
source does not start at the same bit as the destination, `ShiftBits` first lays its row into the one row
of scratch at the destination's alignment, which also settles any overlap. QuickDraw's own loops work on
longs laid from the start of the row; a long of colour, of solid pattern or of inversion that holds the same
value for every pixel reads the same wherever a span starts, and the native path takes those only
(32 bits: any; 16 bits: the same two words; 8 bits and below: the same four bytes).

**The colour loops.** The word at `-$104(A6)` of the `StretchBits` frame is
`BGnotWhite`. The System's `StretchBits` sets it when a source transfer goes
through other colours than black on white, or inverts its source; the ROM's
`BitBlt` (`$2CBE4`) and `RgnBlt` (`$2A838`) then take their colour loops for
copy, OR and BIC, and never look at it for XOR or for a pattern mode, where
it is often left set. Those loops work on longs: copy lays the foreground
(`-$A0(A6)`) where the source has a one and the background (`-$A4(A6)`) where
it has a zero; OR lays the foreground and BIC the background where it has a
one, and leaves the rest. The native path does the same for one colour per
pixel, takes the plain path when the colours are black on white, and reads
the nine loop addresses in the trap table (`$1AF0`…, `$1B30`…, `$1BB8`…) at
installation: it stands in for the ROM's loops only (pitfall below).

**The loops the System has replaced.** On System 7.5.5 the trap table holds
RAM addresses for some of the loops the native fill and copy stand in for.
Read from a running guest and disassembled (2026-11-15), they are these:

| Entry | Loop | What the replacement does |
|---|---|---|
| `$1AD0`, `$1AE0` | `BitBlt` pattern copy, small and big pattern | tests `alphaMode`; jumps to the ROM's loop (`$2CFC0`, `$2D1B0`) when it is clear |
| `$1B10`, `$1B20` | the same for a row of one long | ANDs the mask with `alphaMask`, all ones without alpha, then the ROM's loop |
| `$1B78`, `$1B88` | `RgnBlt` pattern copy, small and big pattern | tests `alphaMode`; the ROM's loop (`$2B0EC`) or a plain copy of its own |
| `$1B60` | `BitBlt` copy, left to right | the same masked copy, sixteen bytes at a time with `MOVE16` when both ends allow |

They match the "alpha changes block" of Apple's `QDciPatchROM.a`. `ColorMap` clears `alphaMode` for every
call and sets it for a 16 or 32-bit port alone, so at 8 bits the replacements and the ROM draw the same
pixels; the native path refuses any frame with `alphaMode` set, at every depth. The arithmetic and
highlight entries are replaced too; the native path does not serve those modes.

The span writers copy, fill and combine eight or sixteen bytes at a time
through unaligned loads and stores, with the transfer chosen outside the
loop. They call nothing from the C library (pitfall below), and guest pixels
are ordinary memory, so the Device memory rule of the output does not apply.
A region copy puts a source row aside only when the move is horizontal.
The region runner keeps the list of spans of a row and replays it on the
rows below until an edge of one of the three regions changes: a region is
mostly rows that repeat, and walking its edges again for each of them cost
more than the pixels of a thin one.

**With the translator** (`jit true`) a hook is worth something else.
QuickDraw's loops are ARM64 code already, the System's own copy loop moves
sixteen bytes at a time with `MOVE16`, and what costs is leaving translated code for the host. `sync`
and `parallel` then build the `BitBlt` and `RgnBlt` trampolines with a gate
written in 68k, which the translator compiles: the return address must be
one of the two verified call sites and the
clipped rectangle at least `qdaccelmin` bytes of pixels (4096 by default:
the pixels, shifted by the depth the call receives in `D4`), at 4 bits a
pixel or more. At 1 and 2 bits a row is a few dozen bytes and the guest's
loops beat the journey whatever the area. Anything
else jumps to the guest's handler without leaving translated code.
`DrawSlab` is not hooked at all: it is called once for each scan line of
every line, oval and polygon. With the interpreter it is hooked on trial:
if fewer than one call in a hundred of its first 65,536 was served, the
vector goes back to the ROM and the log says so. `ScaleBlt` and `stScanLoop` keep their plain
trampolines, each call being a whole transfer. Probe mode keeps every hook
open and ungated, to count.

MacBench under QEMU (`raspi3b`, translator, System 7.5.5, 640×480 at 8 bits, 2026-11-15), in ticks, four
runs with `qdaccel off` and two for each threshold. The twenty drawing checks are identical in every timed
run and equal to the interpreter's. (`qd_stretch` had a second value in three probe runs of nine: the
caption of its scene, Chicago at 18 points, which the Font Manager draws one of two ways as the system
heap of the moment allows. The stretched pixels were the same. MacBench now draws that caption at 12
points, and the checks of `qd_stretch`, `qd_pictures` and `qd_windows` changed with it.)

| Test | `off` (four runs) | `sync`, 1024 | `sync`, 4096 | `sync`, 16384 |
|---|---|---|---|---|
| `qd_rects` | 156–166 | 168, 175 | 144, 156 | 138 |
| `qd_text` | 177–186 | 160, 162 | 162, 165 | 157 |
| `qd_copybits` | 49, 49, 84, 85 | 25, 25 | 25, 25 | 27 |
| `qd_scroll` | 84–87 | 92, 105 | 92, 93 | 92 |
| `qd_lines` | 124–129 | 122, 123 | 141, 142 | 141 |
| all twenty | 48.6–51.1 s | 47.5, 48.6 s | 48.0, 48.4 s | 48.0 s |

Large copies gain a factor of two, text a tenth; rectangles of about 3,000 pixels lose at 1024 and not at
4096, which is where the default comes from. Scrolling loses a tenth at every threshold and the reason is
not known. The other tests do not move by more than the runs differ among themselves (several lines are
bimodal under QEMU). These are QEMU figures: a journey out of translated code is dearer there than on a Pi,
so the right threshold on hardware is probably lower. **Nothing of this has been timed on a Pi 4.**

MacBench by depth (same machine and day; ticks; `off` twice, then `sync` with the default threshold).
The twenty checks of a depth are the same with and without the hooks, and with `qdaccelmin 1`.

| Depth | `qd_copybits` | `qd_regions` | `qd_scroll` | What happens |
|---|---|---|---|---|
| 1 bit | 18 → 18 | 85 → 86 | 46 → 47 | left to the guest by the gate; taken, scrolling cost 109 |
| 2 bits | 25 → 18 | 95 → 95 | 74 → 74 | left to the guest by the gate; taken, scrolling cost 116 |
| 4 bits | 28, 30 → 16 | 96, 101 → 96 | 108, 121 → 117 | taken |
| 8 bits | 47, 84 → 24 | 100, 103 → 93 | 84, 90 → 86 | taken |
| 16 bits | 93, 94 → 46 | 185, 190 → 130 | 166, 167 → 125 | taken |
| 32 bits | 153, 161 → 60 | 212, 225 → 113 | 227, 240 → 157 | taken |

The gain grows with the bytes in a pixel, which is what a graphics card was for. The totals of the twenty
tests are not quoted: `qd_text`, `qd_windows` and `qd_textedit`, which no hook serves, moved by up to a
factor of two between two runs of the same configuration that evening, with another build running on the host.

That table predates the colour loops. With them, two more runs each way gave 49.0 and 49.9 s with
`qdaccel off`, 49.1 and 49.6 s with `sync` at 4096, and 50.3 s with `qdaccelmin 1`, where all 373,735
`BitBlt` calls are served by the host (352,964 fills, 20,771 copies, 604 through the colour loops): the
same picture, and about 3 µs for each journey under QEMU.

With the interpreter (`jit false`, same card, one run each) the hooks are worth what they were built for:
370.7 s without, 330.7 s with, 327.5 s with the colour loops. `qd_copybits` 1048 → 57 ticks,
`qd_scroll` 1008 → 297 → 203, `qd_rects` 1306 → 1332 → 966, `qd_regions` 884 → 520,
`qd_textedit` 880 → 527; lines and ovals within 3 and 9 % once `DrawSlab` has given its vector
back (1269 → 1767 ticks on `qd_lines` before it did). The tests no hook touches move by up to 8 % from
one run to the next.

What reaches the hooks in that run (`qdaccel probe`), and what they would take:

| Hook | Calls | Eligible | Refused for |
|---|---:|---:|---|
| `BitBlt` | 373,735 | 373,735 | nothing. 76 % before `BGnotWhite` was read for what it is |
| `RgnBlt` | 22,202 | 21,824 (98 %) | mode 58, the pattern highlight (378). 74 % before |
| `DrawSlab` | 21,834,170 | 0 | call site `$29D80` (11.2 M); dispatchers `$2FFC0`, `$30010` and a RAM one (10.6 M) |
| `stScanLoop` | 8,505 | 0 | not broken down: text and pictures |
| `ScaleBlt` | 0 | | no 32-bit source in this benchmark |

To build the experimental worker, run
`OKAPIA_QD_MULTICORE=1 ./scripts/build-libs.sh pi4`, then run
`make -C src/kernel okapia-clean` and rebuild the kernel.
Set `qdaccel parallel` on the card. For non-overlapping copies and fills
of at least 256 KiB, cores 0 and 2 split the rows and join before the guest
returns. Other builds and smaller transfers use the synchronous path.
The worker is board-owned, starts once and survives guest restarts.

Use `perfreport true` to log eligible calls, native copies, overlapping
copies, fills, split jobs, bytes and native drawing time. Video reports
include compositor cost and a rolling 95th-percentile screen interval.
Each rejected `BitBlt` or `RgnBlt` is counted once at the first check it fails.
The five-second report includes one example per reason, with the internal
transfer mode, register depths and `BGnotWhite` when the frame is valid.
`DrawSlab` reports its rejections by call site and by mode dispatcher.
`BitBlt` also counts the transfers that went through the colour loops.
`RgnBlt`, `DrawSlab`, `ScaleBlt` and `stScanLoop` report total and native
calls; in probe mode the second figure is what qualified, since nothing is
drawn natively. With the translator, a timed mode only counts what passed
the 68k gate: use `probe` for coverage.
Use `qdaccel probe` for coverage with the guest drawing, `off` as the
baseline and `sync` or `parallel` for timed runs.

**`qdaccel verify` compares the two, call by call.** The `BitBlt` and `RgnBlt` trampolines then call the
host twice. The first call keeps the destination as it is and lets the guest's own handler draw; the second
puts the destination back, draws natively, and compares every byte with what the guest left. The guest's
pixels stay on the screen either way, so the mode is safe to leave on with any software, at any depth and
under any System: it answers "does the native path draw what this QuickDraw draws" without a benchmark that
knows the answer. The log says every five seconds how many calls were the same and how many differed, and
describes the first sixteen that did (mode, depth, size, bit positions, colours, pattern, first differing
byte). A difference in the bytes between two rows of the rectangle, which neither side writes, is counted
apart: something an interrupt drew meanwhile. The other hooks only count in this mode. It is slow, and
allocates two 4 MB buffers at the first installation.

One MacBench pass at each depth under QEMU (System 7.5.5, translator, 2026-11-15), every call compared:

| Depth | `BitBlt` the same | `RgnBlt` the same | Different | Not taken |
|---|---:|---:|---:|---|
| 1 bit | 376,849 | 21,874 | 0 | nothing |
| 2 bits | 369,396 | 21,829 | 0 | `RgnBlt` mode 58 (378) |
| 4 bits | 369,546 | 21,837 | 0 | `RgnBlt` mode 58 (378) |
| 8 bits | 373,708 | 21,828 | 0 | `RgnBlt` mode 58 (378) |
| 16 bits | 373,135 | 21,835 | 0 | `RgnBlt` mode 58 (378), two `BitBlt` with alpha |
| 32 bits | 373,133 | 21,835 | 0 | `RgnBlt` mode 58 (378), two `BitBlt` with alpha |

A kernel built with one wrong bit in the solid fill reported 18,785 differences in its first seconds: the
comparison sees what it is meant to see. On the Pi 4, compare five
alternating runs at fixed CPU frequency, resolution, depth and `frameskip 1`.
Measure repeated large same-format `CopyBits`, Finder list scrolling or
Speedometer Graf, using the same version for both runs (3 under System 7.1).
The targets are 2× on eligible 8-bit copies and 20%
on scrolling or Graf. Keep the parallel mode experimental unless large
transfers improve by 15% over `sync` with at most 5% higher p95 screen interval.
For QEMU durability use
`OKAPIA_EXTRA_PREFS='qdaccel sync' ./scripts/run-test.sh 40 qemu/sd-contents/boot71.img`.

## Status

- [x] Fixed output frame buffer, integer scaling, centring (2026-08-26)
- [x] All six depths; 1-bit palette fixed (`0003`)
- [x] Dirty tiles with shadow copy and spans (2026-08-27 → 09-08)
- [x] Dynamic frameskip (2026-08-28)
- [x] Compositor shared by both engines, announced regions (2026-09-07)
- [x] Pointer never lost from the cheap set (twelve-frame memory, 2026-09-08)
- [x] Pixel order from the firmware; EDID logged; native 2560×1440 output (2026-09-13)
- [x] Alignment-safe output writes (2026-09-13)
- [x] Same resolution list for both engines, 16 MB frame buffers, stable identifiers (2026-09-14)
- [x] QuickDraw hooks reach every application; gated in 68k under the translator; span writers free of the
      C library, checked against their byte-by-byte definition on the host (2026-11-15)
- [ ] QuickDraw hooks on a Pi 4: MacBench and Speedometer with `qdaccel off` and `sync`, and `qdaccelmin`
      at 1024, 4096 and 16384
- [x] The colour loops of `BitBlt` and `RgnBlt`, and `BGnotWhite` ignored where the ROM ignores it: every
      `BitBlt` of a MacBench pass is eligible, 98 % of `RgnBlt` (2026-11-15)
- [ ] QuickDraw coverage, by what the probe counts: the
      `stScanLoop` cases of text and pictures, at every depth; the highlight modes; `DrawSlab`, `ScaleBlt`
      and `stScanLoop` at other depths than 8 bits. `DrawSlab`
      is worth it for the interpreter only, and none of its call sites or dispatchers on System 7.5.5 is one
      the hook knows
- [x] The loops the System has replaced in the trap table, read from the guest: alpha drawing and a
      `MOVE16` copy, nothing the native path draws differently (2026-11-15)
- [x] `BitBlt` and `RgnBlt` at every depth, 1 to 32 bits, and `qdaccel verify`: 2.3 million calls of six
      MacBench passes compared with the guest's drawing, none different (2026-11-15)
- [x] `qd_stretch` had two outcomes: a scaled font in its caption, not its pixels nor the translator. The
      benchmark was at fault and is corrected (2026-11-15)
- [x] Why `qd_scroll` lost a tenth with the hooks on: the region runner walked the region's edges again
      for every row. It keeps the row's spans now (110 → 86 ticks at 8 bits, 84 to 90 without the hooks)
- [ ] Below 8 bits the native row writer costs more for each row than the guest's loop: shifted rows go
      through the scratch and two passes. Left to the guest under the translator at 1 and 2 bits; worth
      a single-pass writer only if a Pi shows the interpreter case matters
- [ ] Vertical sync and a double buffer where the board allows
- [ ] Compositor on core 2 (S2) — see [Architecture](../project/architecture.md#multicore)
- [ ] Gamma for direct modes (it would also have to set `bFullRedraw`)
- [ ] Re-measure the old anomaly "270 µs with 6 modes offered, 1,224 µs with 22" under the current compositor
- [ ] Doom target: 35 stable updates per second on a 60 Hz output
- [ ] Validate the generic path on a Pi 5, where the application cannot set the resolution
- [ ] Optionally compensate mouse speed for the integer scale

## Pitfalls

- **A patch installed from the idle seam belongs to one application.** `SetToolTrapAddress`, called at first
  idle, runs in the Finder's context, and the Process Manager swaps such a patch out whenever another
  application is in front. Until 2026-11-15 the QuickDraw hooks therefore saw the Finder and nothing else:
  102 `BitBlt` calls while it drew the desktop, none during 50 s of MacBench, the vector back to `$1002C870`
  at every blank. Every `qdaccel` figure taken before that date with an application in front measured the
  Finder's share alone. Write the trap table; `SetToolTrapAddress` is for code that is an application.
- **The ROM is older than the System that drives it.** `F1ACAD13` carries an older QuickDraw than the
  System's; System 7.5.5
  replaces `StretchBits`, patches the head of `RgnBlt`, and replaces single loops through the trap
  table (`$1AD0`, `$1AE0`, `$1B60`, the arithmetic modes…) while leaving the rest in ROM. The frame word
  at `-$104(A6)` was taken for a "colorize" flag from the System side of that border and every transfer
  that had it set was refused: a quarter of `BitBlt`. Read on the ROM side it is `BGnotWhite`, set for
  inverted sources too, and ignored by both dispatchers for patterns and XOR, which were most of those
  refusals. Before standing in for a loop, disassemble the ROM's dispatcher, and check the table entry
  it would jump through. Apple's later source (`reference/supermario`) names things; the ROM says what runs.
- **Circle's `memcpy` copies byte by byte** unless both pointers are aligned on eight *and* the length reaches
  128; its `memmove` does whenever it must run backwards; its `memset` until the address is a multiple of 32.
  A row of pixels meets none of these. The native copy of a 300×200 rectangle measured slower than the
  translated ROM's until the span writers stopped calling them (295 → 23 ticks on `qd_copybits`, the guest's
  own code taking 49).
- **A row has a price before its first byte.** The native writers are judged by bytes a second, and a
  transfer is also rows: a region runner that worked out the spans of every row again made scrolling a
  tenth slower than the guest at 8 bits, and at 1 bit, where 600 pixels are 75 bytes, the shifted row
  writer made it twice slower with every pixel right. Count rows when the rows are short.
- **Leaving translated code is the unit of cost under the translator**, and QEMU overstates it: every
  indirect branch leaves TCG's chained code. A call the host declines cost 1.3 µs under QEMU with three
  journeys, 0.5 µs with one. `DrawSlab`, declined 21.8 million times in one MacBench, turned 126 ticks of
  lines into 1,063. A hook that cannot serve must not be installed, and a size below which the guest wins
  must be tested before leaving, in 68k.
- **A QEMU window makes frame buffer writes 12× more expensive**: QEMU tracks dirty pages once a display is attached.
  The same composite measured 1.5 ms headless at 640×480, 6 ms headless at 1280×960 and **74 ms with
  `-display cocoa`**. Never tune headless and assume it holds with a window. The window also costs about a third of
  guest speed.
- **A screen at rest used to cost the whole comparison** (0/256 boxes, 1,225 µs a frame, 7% of wall time); the cheap
  set made it ~200 µs and 1.1%.
- **The cheap set cannot have one frame of memory.** Overwriting it with this frame's dirty tiles meant one still
  frame erased where the screen was moving; a slow pointer then waited its turn in the eighths rotation, up to
  133 ms at random — reported by eye and present in no measurement. See the
  [pointer note](../notes/dev/2026-09-08-stuttering-pointer.md).
- **The blitters count source bytes, not pixels** — invisible above 8 bits, broken below.
- **`DEPTH` is compiled into `libcircle.a`**: an indexed mode needs our own `CBcmFrameBuffer`, not `CScreenDevice`.
- **QEMU accepts modes a Pi refuses** (`raspi3b` takes 8 bpp with a palette that a Pi 5 refuses) and has no EDID;
  its "monitor" is `-global bcm2835-fb.xres=/yres=`. It validates the mailbox path, not the negotiation.
- **Pi 5**: the resolution cannot be set by the application nor by `config.txt`. Print what the firmware returned.
- **Sizes that fill the display at ×2 or ×3** were once added automatically, including 912×492 and 853×480.
  One crashed a Pi through an unaligned write. The custom editor checks width alignment and the compositor's
  output writes remain alignment-safe.
- **A resolution Mac OS stores by identifier survives a zap of the PRAM**: with an identifier that later meant
  another size, System 7.6 crashed at every boot. Identifiers are now fixed per size.

## Development notes

- **2026-08-27** — First measurements: composite 236 µs headless and 452 µs windowed after dirty tiles, from 6,191 and
  74,637 µs.
- **2026-09-08** — Spreading the comparison over eight frames and drawing what each found (upstream's
  `update_display_dynamic`, `video_x.cpp:2343`) measured faster and looked wrong: a menu came down as a mosaic.
- **2026-09-13** — On the Pi 4: colours swapped (pixel order), a 1824×984 output in a black frame (overscan and CEA
  1080p), a crash one second after the boot menu at 912×492 (Device memory), weird sizes in Monitors — all fixed.
- **2026-09-14** — SheepShaver's mode list raised to Basilisk's, its buffer from 4 to 16 MB.

## References

- `BasiliskII/src/include/video.h`, `video.cpp:569`, `CrossPlatform/video_blit.cpp`, `Unix/video_x.cpp:2343`
- `SheepShaver/src/include/video.h`, `video.cpp` (`cscGetNextResolution`, `cscGetModeTiming`), `gfxaccel.cpp`
- Circle `lib/bcmframebuffer.cpp`, `lib/translationtable64.cpp:145`, Raspberry Pi 5 appendix of the Circle docs
