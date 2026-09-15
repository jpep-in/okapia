# Display

> From the Macintosh's frame buffer to the HDMI output: the output mode, the resolutions offered, the
> compositor that converts, scales and redraws only what changed. Main files:
> `src/circle/video_shared_circle.{h,cpp}` (rules shared by both engines), `src/circle/video_sizes_circle.{h,cpp}`,
> `src/circle/compositor_circle.{h,cpp}`, `src/circle/video_circle.cpp` (68k), `src/circle/sheepshaver/video_circle.cpp`
> (PowerPC), `src/firmware/circle/okapia_output.cpp`, `patches/macemu/0003`, `0011`.

## Using it

- **The output mode** is the monitor's preferred mode, which the Pi firmware reads from its EDID. The log names
  the monitor and warns when the mode differs: `Display 2560x1440, 32 bpp … Monitor PHL PHL 246B1, preferred mode
  2560x1440`.
- **A monitor the firmware drives at the wrong mode** can be told in `config.txt`. Seen on a Philips 246B1 whose
  EDID prefers 2560×1440 and which the firmware drove at CEA 1080p: `hdmi_group=2`, `hdmi_mode=87`,
  `hdmi_cvt=2560 1440 60 3 0 0 1`, `max_framebuffer_width=2560`, `max_framebuffer_height=1440`. Keep
  `disable_overscan=1`: without it a 1920×1080 output came back as 1824×984 inside a black frame.
- **Resolutions** are chosen in the Mac's **Monitors** control panel (Mac OS 7.5 and later list them). Both
  engines offer the same sizes, those that fit the output unscaled: 512×384, 640×480, 800×600, 1024×768,
  1152×870, 1280×720, 1280×960, 1280×1024, 1600×900, 1600×1200, 1920×1080, 1920×1200, 2560×1440, plus the
  display's own size and the `screen` preference's when not listed. All six depths up to what 16 MB holds.
- **Starting resolution**: `screen win/<width>/<height>` (upstream's spelling; `0` = the display's). On System 7.1,
  whose Monitors panel shows no resolution list, this is the way to pick one.
- **Scaling**: the Mac's screen is scaled by the largest integer that fits and centred — 1280×720 fills a 2560×1440
  monitor at ×2. An integer factor keeps the picture sharp; a slow pointer then moves in steps of that factor.
- **Refresh**: `frameskip 0` (dynamic, the default) holds the compositor to about an eighth of wall time.

## How it works

### One screen, two Macintosh, one set of rules

The engines present the display differently — Basilisk hands the platform a `monitor_desc` and a `video_mode`
list; SheepShaver runs the Mac's own native driver and wants a `VModes[]` table and a frame buffer at a Mac address
— but everything between those contracts and the compositor is the same question asked twice. It *was* written
twice, and the copies drifted: only one repeated the palette across 256 entries, and the other drew 4 and 16
colours as coloured noise. `video_shared_circle.cpp` states each rule once — which modes fit, which converter a
depth needs, the palette, the refresh rate, the reports — and each `video_circle.cpp` is its engine's contract and
nothing else. **A display fix goes in the shared file unless it is about one core's vocabulary.**

### The output

`FwOutputClaim()` claims a `CBcmFrameBuffer (0, 0, 32)` once for the life of the board; the firmware draws the boot
menu on it and the compositor borrows it. It is never renegotiated: a mode change in Monitors touches only the
Mac's buffer and the compositor's plan — no HDMI renegotiation, no flicker. Circle never reads the EDID; the Pi
firmware does, and Okapia asks it for the block (`PROPTAG_GET_EDID_BLOCK`) only to log the maker, name and
preferred timing.

**Pixel order** is asked of the firmware (property tag `0x00040006`): QEMU gives red in the low byte, a real Pi blue.
The palette masks, the expansion map's shifts and the direct-mode converters follow it — a fix written for QEMU drew
the wrong colours on the Pi until then.

### The mode list

`VideoScreenSizeList()` (`video_sizes_circle.cpp`, pure, host-tested) builds the list: the thirteen standard sizes,
then the display's size and the preference's, each only if not listed. Each engine supplies its identifiers:
Basilisk numbers `0x80…0x8C` then `0x8D`, `0x8E`; SheepShaver keeps upstream's Apple names where they exist and
numbers the rest past `APPLE_CUSTOM` (`patches/macemu/0011`). **A fixed size keeps its identifier whatever monitor
is plugged in**, because Mac OS 7.5 and later store the Monitors choice by identifier in the Preferences folder, not
in PRAM.

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
`composite_time × 8 / 16,667 + 1`, capped at 12 (`Retune()`, `video_shared_circle.cpp:410`). The Mac draws its own
cursor into its own buffer, so the pointer can never move more often than the compositor runs: a 10 Hz screen reads
as a laggy mouse. Report the composite rate, not the VBL rate — calling 55 VBL/s "fps" once hid a 9 Hz screen.

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
- **Sizes that fill the display at ×2 or ×3** were once offered and came out as 912×492 or 853×480 — nobody's
  resolution, and 912×492 crashed a Pi through an unaligned write. Only standard sizes and the display's own.
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
