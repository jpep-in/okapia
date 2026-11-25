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
  display's own size, the `screen` preference's, half the display (width down to a multiple of 8: 960×540 on
  1080p, 752×491 on a 1512×982 desktop screen, every Mac pixel 2×2) and two thirds of it (1008×654 there: three
  device pixels a Mac pixel on a Retina screen, the sharp size between the two) when not listed. All six depths up to what 16 MB holds.
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
then the display's size, the preference's, half the display (`VideoHalfSize`) and two thirds
(`VideoTwoThirdsSize`), each only if not listed. Each engine supplies its identifiers: Basilisk numbers
`0x80…0x8C` then `0x8D`, `0x8E`, `0x8F`, and `0x96` after the custom slots; SheepShaver keeps upstream's Apple
names where they exist and numbers the rest past `APPLE_CUSTOM` (`patches/macemu/0011`), half and two thirds after
the six custom slots so theirs do not move. That is twenty-three sizes at six depths: 0011 sizes SheepShaver's
`VModes` at 139 entries, exactly that and the end marker. **A fixed size keeps its identifier whatever monitor
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

### 68k QuickDraw acceleration: set aside

Native ARM64 stand-ins for QuickDraw's `BitBlt` and `RgnBlt` loops (`qdaccel`) were built and measured, then
taken out of this branch on 2026-11-26: with the translator, QuickDraw's own loops already run as ARM64 code, and
Speedometer did not see the hooks. The code, its measurements and its study live on the `wip-quickdraw` branch, to
be challenged again against the translator ([Decisions](../project/decisions.md#quickdraw-acceleration-set-aside)).

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
- [ ] Vertical sync and a double buffer where the board allows
- [ ] Compositor on core 2 (S2) — see [Architecture](../project/architecture.md#multicore)
- [ ] Gamma for direct modes (it would also have to set `bFullRedraw`)
- [ ] Re-measure the old anomaly "270 µs with 6 modes offered, 1,224 µs with 22" under the current compositor
- [ ] Doom target: 35 stable updates per second on a 60 Hz output
- [ ] Validate the generic path on a Pi 5, where the application cannot set the resolution
- [ ] Optionally compensate mouse speed for the integer scale

## Pitfalls

- **Circle's `memcpy` copies byte by byte** unless both pointers are aligned on eight *and* the length reaches
  128; its `memmove` does whenever it must run backwards; its `memset` until the address is a multiple of 32.
  A row of pixels meets none of these: a native copy of a 300×200 rectangle measured slower than the translated
  ROM's until it stopped calling them (found by the QuickDraw work, `wip-quickdraw`).
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
