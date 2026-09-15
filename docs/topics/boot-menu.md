# Boot menu

> The Okapia firmware: a configuration utility in a plausible Apple style that opens before any emulator
> code runs — the Open Firmware a 68k Macintosh never had. Main files: `src/firmware/` (pure: no Circle
> type), `src/firmware/circle/okapia_firmware.cpp` (preferences and HFS side), `okapia_input.cpp` (USB
> bridge), `okapia_output.cpp` (frame buffer claim), `assets/strings.tsv`, `assets/fonts/`, `assets/icons/`.

## Using it

**Opening it.** At every start the screen shows about two seconds of plain grey (`#BCBCBC`). Hold
**Option** alone to open the menu; set `bootmenu true` to always open it. It also opens by itself when the
configuration is missing or no volume on the card is bootable. Any other combination with Option belongs to
the Macintosh — except **Command-Option-P-R**, which forgets the parameter RAM, as a real Macintosh did in
its ROM before ADB was alive.

**The chooser** lists every volume on the card with its name, System version and era icon (System 6, 7,
Mac OS 8, 9), free space, and state (ready or in use). Per line:

| Column / control | Meaning |
|---|---|
| Startup disk | the one volume the Mac starts from; its `disk` line goes first (the order **is** the setting) |
| Mounted | whether the volume is mounted at all |
| Mount as | Hard disk, Hard disk read-only (the `*` prefix), or CD-ROM (a `cdrom` line; a disc can be the startup volume, through the boot driver) |
| Emulator | 68k (Basilisk II) or PowerPC (SheepShaver): active for a universal System, greyed but shown otherwise |

What cannot be done is greyed per line: the startup volume cannot be unmounted under itself, a volume without
a System cannot become the startup disk. **+ New volume…** makes an empty HFS volume: it asks only the
Macintosh volume name (27 characters) and a size that fits the card; the firmware picks the file name.

**Settings**: memory, screen refresh (frameskip), shared folder and its volume name, sound output, mouse
resolution, language, and "always show this menu". A setting that needs an Okapia restart (only memory) is
marked `*` and the button becomes **Save and restart**. There is deliberately no date, no time zone and no
resolution: Mac OS owns the clock (Date & Time) and the screen mode (Monitors).

**Information**: ROM and Macintosh model chosen (and whether the model came from the System or the
preferences), card, startup volume, build date, board, the display mode the firmware actually granted, and
the components and licences Okapia is made of.

**Shut down** and **Forget the parameter RAM** go through a confirmation alert. Forgetting asks which Macintosh —
68k, PowerPC or both, each keeping its own ([Clock and PRAM](clock-and-pram.md#parameter-ram)) — with the engine of
the selected startup volume marked first. The choices answer the mouse and never take the keyboard focus, so Return
stays the assent and Escape the refusal.

**Keyboard**: Tab and Shift-Tab move the focus, arrows move in lists, left and right cross the list's columns,
Space activates the focused control, Return the default button, Escape cancels. Text fields support
selection, Shift-arrows and Command-A/C/X/V. The pointer appears on the first mouse movement.

## How it works

### What earns a place here

A setting belongs in the firmware only if the guest cannot do it itself, the need appears after the card is
made, Okapia's code really consumes it, and — decisively — it **unblocks a machine that refuses to start**.
Everywhere else the alternative is to power off, take the card out and edit it on another computer; that cost
is what the firmware exists to remove. Screen depth, sound level and keyboard layout fail the first test:
the Mac has control panels for them.

### Engines

The chooser decides which emulator a volume needs from its System file (`HfsFlavourOf`, reading resource types
in the blessed folder's System file): 68k patch resources only → Basilisk; native PowerPC fragments only
(`cfrg`) → SheepShaver; both → a **universal** System, and the popup asks, the answer stored in an `engine`
line. Nothing is stored for the settled cases: a stored answer that repeats what the System says will one day
contradict it. Measured on the test images: System 7.1 has no PowerPC resource; 7.6 carries `cfrg`×10,
`nlib`×10, `ntrb`×8, `ndrv`×7 — from 7.5 on, one System file carries both.

The firmware is handed the engine as data (`FirmwareRun (TFirmwareEngine)`) and answers
`FirmwareWantedEngine()`; it never knows Basilisk or SheepShaver exist.

### Writing the preferences

Everything the menu changes goes to `BasiliskII_Prefs` through `SavePrefs()`, which keeps lines it does not
recognise. `disk`, `cdrom` and `engine` lines are **replaced**, not edited: the order is the setting.

### Four layers, one seam

1. **Surface** — memory, width, height, pitch. On the Pi it is the output frame buffer (through a shadow
   surface); on the host an ordinary buffer.
2. **Primitives** (`okapia_gfx`) — rectangles, lines, rounded rectangles, circles and triangles as signed
   distance fields (so edges are antialiased at any scale), glyph blits, UTF-8 text with measurement,
   truncation with an ellipsis (`GfxTextBox`) and word wrap (`GfxTextWrap`). *Only they touch surface memory.*
3. **Theme** (`okapia_theme`) — a constant structure of drawing functions **and metrics**: button height,
   margins, stroke width (`nStroke`), scrollbar width, focus ring reach (`ThemeReach`), content rectangle
   (`ThemeContent`). *Components ask; they never assume a size.*
4. **Widgets and screens** (`okapia_widgets`, `okapia_screen`, `okapia_page`) — a widget is a small value, a
   screen a static array of widgets plus an event loop in the style of `ModalDialog`: focus traversal,
   hit-testing, tracking a press like `TrackControl`. *A widget never draws; it calls the theme.*

**Layout is computed** (`okapia_layout`): a rectangle is spent from its edges; a row reserves on each side what
its control can draw outside itself *in any state it may reach* (the focus ring included), because layout runs
before anyone knows where the focus will land.

**Screens are pure**: handed values, answering values. Everything that knows a preferences file, an HFS
volume or a frame buffer stays in `src/firmware/circle/`. That is what lets `tests/host` drive them with
synthetic events and measure them.

**Only what changed is redrawn**, into the shadow surface first, then the changed rectangle is copied at once —
drawing straight into the visible buffer made the background flash under each control. Isolated redraw is
legitimate only because no widget overlaps another, which the geometry check enforces.

### Resolution independence

There is no logical canvas. The interface is drawn at the output's resolution with theme metrics multiplied by
a possibly fractional scale (36/16 on 1920×1080, 48/16 on 2560×1440). A scaled canvas only magnifies its own
staircase; recomputed, a curve is as smooth at 2.25 as at 1. Only the System folder icons (bitmaps) take an
integer scale.

### The look: an uchronia

The menu opens in front of Systems from 6 to 9, so it copies none of them — its chrome would date itself
against its own content. What carries "Apple" without a date: black and white for structure, rounded
rectangles, the double outline of the default button (reused as the focus ring), the dialog grammar (wide
margins, one clear action at the bottom right), and **flatness** — the constraint of 1984 and the fashion of
today, avoiding Platinum bevels and Aqua gel. Where the era dithered, a flat grey: the System 7 desktop's
checkerboard averages in linear light to about `#BCBCBC`, and integer scaling turns a one-pixel checkerboard
into crawling squares. The era shows in the folder icons, not in the frame. Alerts keep the Macintosh's three
levels — note, caution, stop — because they are a promise about consequences.

### Text

- **Fonts**: the X11 Adobe Helvetica bitmap faces (`assets/fonts/`), licence read in the files: permission to
  use, copy, modify, distribute and sell, compatible with GPLv3. Geneva was essentially a Helvetica fitted to a
  small pixel grid. Six sizes, real bold; `scripts/gen-font.py` reads BDF directly, `fetch-fonts.sh` and
  `trim-bdf.py` vendor only printable characters (380 KB). **Never Chicago**: it is Apple's, under copyright.
- **Translations**: `assets/strings.tsv` holds every label in every language; `scripts/gen-strings.py`
  generates both the tables and the `TStringId` enumeration, so a removed key breaks the build. English is the
  default; `language` selects; an unknown code falls back to the first language.

### Input and output on the board

- **Keyboard and mouse** are latched from the moment USB is up (`FwInputWatch` from
  `CKernel::Initialize()`), not when the window opens: a USB keyboard reports changes, never state, so a key
  held from power-on is invisible to a window that starts listening later. Modifiers and keys are latched over
  the whole window, because the four keys of Command-Option-P-R are almost never in one report.
- Characters come from Circle's `CKeyMap`, so the keyboard layout is honoured; navigation keys come from the
  physical USB usage.
- **Handing over to the Macintosh**: Circle keeps one raw keyboard handler, so `InputInit()` replacing it *is*
  the hand-over. The mouse can be claimed once per boot: the firmware keeps the registration and forwards
  reports (`FwInputPassMouseTo`). `FwInputReclaim()` takes both back after the Mac has run.
- **Output**: `FwOutputClaim()` claims the frame buffer once for the life of the board; the Mac's compositor
  borrows it.

## Status

- [x] Surface, primitives, theme with metrics, widgets, screen loop, layout model (2026-08-31 → 09-02)
- [x] Specimen screen, identical byte for byte under QEMU and on the host (2026-08-31)
- [x] Fonts with real bold, UTF-8 text, truncation and wrap (2026-09-01 → 09-02)
- [x] English and French, every page measured in both languages (2026-09-02)
- [x] Chooser with version, state and era icon per volume (2026-09-03)
- [x] Settings, information, shut down, forget PRAM, confirmation alerts (2026-09-04)
- [x] Emulator choice for universal Systems; engine passed as data (2026-09-05)
- [x] Mouse resolution setting (2026-09-09)
- [x] Forget the parameter RAM asks which Macintosh: an alert may carry a row of radio buttons, `TConfirm::pChoices`
      (2026-09-15)
- [x] New volume; mount as hard disk, read-only or CD-ROM; a disc as startup volume (2026-09-09)
- [ ] **Repair dialog** — today `HfsRepair` scavenges silently. Three states, not two: repair without asking,
      ask, never repair — through a second boolean `hfsrepairask`, without changing the type of `hfsrepair`
      (`prefs_circle.cpp`: the parser reads by declared type, an integer would read `0` on every existing card).
      The dialog must time out and repair on expiry, or `run-test.sh` stops on it. Strings and a specimen
      exist; the screen is not wired
- [ ] System folder icons at non-integer scales (vector or larger sources)
- [ ] Diagnostic line when a System needs the File System Manager 1.2 for the shared folder
- [ ] Provisioning screens (catalogue tree, progress) — see [Storage](storage.md#provisioning)

## Pitfalls

- **Metrics belong to the theme as much as pixels.** Four times a hard-coded value stood where a metric was due
  (checkbox and popup marks at fixed coordinates, a 16 px scrollbar thumb minimum, the focus ring taking the
  button's radius everywhere, a `1` for stroke width); the specimen screen made each visible.
- **A fraction of the part, never a floor.** A minimum of seven pixels for the settings icon's sliders is right
  at sixty and overlaps at fourteen — exactly the size a 640×480 output gives. A needed gap is written as a
  one-pixel minimum clearance.
- **Measure where the ink falls; don't assume it.** The power mark took three corrections (notch along a radius,
  caps at the cut edge, the half-pixel of pixel centres: ink at `(width − stroke) / 2 + 1/2`).
- **An argument is evaluated before the call it is an argument to.** `Row()` places a label and answers the space
  left, so `s_nMemory = nCount; PageAdd (..., Row (...), ...)` recorded the label's index, not the control's:
  every menu answered for its neighbour. The rectangle first, the index second, the component last.
- **`RegisterKeyStatusHandlerRaw (0)` does not detach a keyboard**, it switches it to cooked mode
  (`usbkeyboard.cpp:200`); doing it to "give the keyboard back" stopped the kernel dead. `UnregisterKeyStatusHandlerRaw()`
  exists; Okapia never needs it.
- **Circle's cooked mouse drops every report** until `Setup()` gives it screen dimensions; a working keyboard with
  a dead mouse says nothing about USB.
- **`CKeyMap::Translate` answers `KeyNone` while Alt or a Windows key is held**, so Command-C carried no character;
  the bridge translates with the Command bit removed. Circle files Space among special keys (0x100), which a
  "printable" filter dropped.
- **A literal `"\xE9"` swallows a following hex digit.** Sources stay UTF-8 and the text primitive decodes.
- **The tab order changes with state** (a disabled control takes no focus), which makes scripted tests fragile —
  the first chooser test ticked "read-only" believing it ticked "startup disk".
- **What is saved under the pointer belongs to the surface it came from**: `GfxCursorForget()` on a page change,
  or a fragment of the previous page is stamped on the next.
- **Circle tracks no header dependencies** (`Rules.mk:271`): a field added to `TThemeMetrics` sent the kernel into
  the font tables, a field added to `TEvent` turned Down into Escape. Every Makefile compiles with `-MMD -MP` —
  see [Building](../contributing/build.md).
- **A redraw of the whole screen at every event** flickered and, under a QEMU window, cost enough to queue the
  pointer's reports.

## Development notes

- **2026-08-31** — Built beside `C2DGraphics`, not on it: Circle's 2D class is a frame buffer painter, QuickDraw was
  a model. Primitives take a surface so the host renders byte for byte like the board.
- **2026-09-01** — LVGL rejected after reading its Circle glue: `CLVGL` asserts the display depth equals
  `LV_COLOR_DEPTH` (16 in `external/`, our output is 32), wires only mouse and touch, uses the cooked mouse, and
  its monochrome theme targets e-paper.
- **2026-09-01** — The logical 640×480 canvas scaled by an integer was abandoned: scaling turns every pixel into a
  block, and an antialiased canvas only yields grey blocks. Drawing at output resolution fixed curves and text.
- **2026-09-01** — `check_geometry` created after "not clean, misaligned" proved measurable: it found a centre taken
  on a shrunk half-extent, a hand-written square root off by 10% (replaced by `__builtin_sqrtf`), a ring band a
  half stroke too far, and text centred on the cell instead of the cap height. Capital accents were clipped
  because Helvetica declares an ascent of 11 and draws É on 12 rows: cells are now sized on the glyphs.
- **2026-09-02** — The specimen paginates: a document is paginated, and four rounds were lost trimming content
  to fit one page.
- **2026-09-04** — `okapia_page` holds the shared page frame; the chooser folded into it with its twenty checks as
  the refactor's safety net.

## References

- Tests: `tests/host/check_geometry.cpp`, `check_screen.cpp`, `check_chooser.cpp`, `check_pages.cpp`; renders:
  `render_specimen`, `render_chooser`, `render_pages`
- `scripts/specimen.sh [page|live]` — the specimen under QEMU
- Circle `lib/input/usbkeyboard.cpp:200`, `lib/input/mouse.cpp:85`, `Rules.mk:271`
