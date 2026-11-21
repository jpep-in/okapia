# Boot menu

> The Okapia firmware: a configuration utility in a plausible Apple style that opens before any emulator
> code runs — the Open Firmware a 68k Macintosh never had. One window with six tabs: System, Settings,
> Network, Information, Display, Logs. Main files: `src/firmware/` (pure: no Circle type), `src/common/okapia_menu.cpp` (the driver: preferences, card and pages, the same on every host), `src/common/okapia_host.h` (what it asks of the machine), `src/hosts/circle/firmware/okapia_host_circle.cpp` (the Pi's answers), `okapia_input.cpp` (USB
> bridge), `okapia_output.cpp` (frame buffer claim), `assets/strings.tsv`, `assets/fonts/`, `assets/icons/`.

## Using it

**Opening it.** At every start the screen shows about two seconds of plain grey (`#BCBCBC`). Hold
**Option** alone to open the menu; set `bootmenu true` to always open it. It also opens by itself when the
configuration is missing or no volume on the card is bootable. Any other combination with Option belongs to
the Macintosh — except **Command-Option-P-R**, which forgets the parameter RAM, as a real Macintosh did in
its ROM before ADB was alive.

**One window, six tabs**, with the tab control Mac OS 8's Appearance Manager introduced: **System** (the
volumes), **Settings**, **Network**, **Information**, **Display** and **Logs**. The footer is the window's and the same on every page: forget
the PRAM, shut down, **Show this menu at startup** (`bootmenu`, beside Start because it is about starting; unticked, a grey line under it says *Press Alt during startup to show this menu*, so the way back is never lost), and **Start**, which reads **Save and restart** with a note beside it when a change made on any page needs Okapia to
restart. What the pages change is written to the card when the Macintosh starts or the board shuts down — as a
control panel keeps what was set in it; there is no Cancel.

**System** lists every volume on the card with its name, System version and era icon (System 6, 7,
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

**Settings**: memory, screen refresh (frameskip), mouse resolution, sound output and language, two to a row; beside
the language, the 68k engine's two experimental speed-ups, **JIT** (`jit`) and **QuickDraw acceleration** (`qdaccel`),
followed by "(68K only)" once for both; then
two groups, framed and named as the Views control panel of System 7 frames its own: **Folder sharing** (share, the
folder on the card, the volume's name on the Mac) and **Date & Time** (below). A setting that needs an Okapia restart
(only memory) is marked `*` and Start becomes **Save and restart**, with "* Restart required" beside it. There is deliberately no resolution: Mac OS owns the screen
mode (Monitors).

**Network**: the connection the Macintosh shares — none, the Ethernet cable or the Wi-Fi — and for the Wi-Fi, the
networks in range (strongest first, a lock when it asks for a password, four bars of signal, a bullet on the one
joined), its name (typed for a hidden network), the password (stars, **Show** to read it; never copied out) and the
country. **Search** scans again; **Join** tries the network at once and the line under the page says what came of it —
joined with its signal, the password refused, or why nothing was tried. Choosing a connection starts it if the board
has none; changing it once one runs needs a restart. Details: [Network](network.md#using-it).

The **Date & Time** group: the date and time Okapia keeps, in bold and to the minute, and after it where they came
from — the time server, the Macintosh's own Date & Time, the last use of a volume, or the build — with the offset in
force (`UTC+2` in summer), and at the end of that line an **(i)** whose note says how the clock is kept and how to set
it by hand. Then the **time zone**, from a list of 22 by their cities (`UTC+1 · Paris, Brussels, Berlin,
Madrid, Rome`), summer time following on its own, and **Time from the network** with the server beside it
(`pool.ntp.org`). Both act at once: the hour on the page moves with the zone chosen. Setting the clock by hand stays
the Macintosh's, and Okapia keeps what it sets. Details:
[Clock and PRAM](clock-and-pram.md#using-it).

**Information**: ROM and Macintosh model chosen (and whether the model came from the System or the
preferences), card, startup volume, build version (`YYMMDD-sha`, [Build](../contributing/build.md#version)), board, the display mode the firmware actually granted, and
the components and licences Okapia is made of.

**Logs**: the end of `okapia.log`, the log on the card, in a list that opens on the newest line and scrolls; **Previous
session**, a tick box under the list, shows `okapia-previous.log` instead. Above the list, a group *Serial telemetry* with two tick boxes: **Serial output**
(`serial`) sends the log's lines to the serial port as well as to the card, and **Performance counters** (`perfreport`)
prints the periodic counters ([Testing](../contributing/testing-and-debugging.md#logs)). Both are off on a board: the
port is polled and each character stops the Macintosh for 87 µs. The card has every line whatever they say, so the counters
alone, with the serial shut, cost the log some space and the Macintosh nothing. Under QEMU the port is on whatever they say
(`SerialLogWanted()`, `PerfReportWanted()`): a test arrangement the page does not mention. The page keeps the last 64 KB and 1024 lines of the file, the page
is read when it is turned to (after the board drained its ring onto the card), and a long log is shown by its end.
Neither box needs a restart: both are read at every start of the Macintosh.

**Shut down** and **Forget the parameter RAM** go through a confirmation alert. The shut down alert also offers
**Restart**, between Cancel and Shut down (`TConfirm::pOther`): Okapia starts again and the menu comes back, with what
the pages changed kept. Forgetting asks which Macintosh —
68k, PowerPC or both, each keeping its own ([Clock and PRAM](clock-and-pram.md#parameter-ram)) — with the engine of
the selected startup volume marked first. The choices answer the mouse and never take the keyboard focus, so Return
stays the assent and Escape the refusal.

**Keyboard**: Tab and Shift-Tab move the focus; **Control-Tab** and Control-Shift-Tab turn the page from anywhere,
and left and right turn it while the tabs hold the focus (they keep it, so the arrows go on turning). Arrows move in
lists, left and right cross the list's columns,
Space activates the focused control, Return the default button, Escape cancels. Text fields support
selection, Shift-arrows and Command-A/C/X/V. The pointer appears on the first mouse movement.

## How it works

### What earns a place here

A setting belongs in the firmware only if the guest cannot do it itself, the need appears after the card is
made, Okapia's code really consumes it, and — decisively — it **unblocks a machine that refuses to start**.
Everywhere else the alternative is to power off, take the card out and edit it on another computer; that cost
is what the firmware exists to remove. Screen depth, sound level and keyboard layout fail the first test:
the Mac has control panels for them.

The time zone and the time server pass the first three and not the last either, and are kept for the same reason
as the network: a Macintosh of this era has no notion of either — its clock *is* local time — and it is Okapia's
clock that needs both to turn the network's UTC into that local time. The date itself still fails the first test,
which is why the page shows it and does not set it: Date & Time sets it, and Okapia now keeps what it sets.

The two speed-ups pass all four: each is an experimental mode that can hang a Macintosh at startup, and turning it
back off is exactly what must not need another computer. Both are read at every start of the Macintosh
(`basilisk_glue.cpp` for `jit`, `quickdraw_okapia.cpp` for `qdaccel`), so neither asks for a restart. QuickDraw's box
is ticked for any value but `off` and writes `sync` or `off` only when it is changed, so a card set to `parallel`,
`probe` or `verify` keeps it. The JIT box is grey, still showing the file's value, in an image built without the
translator: the shared half learns that from `-DOKAPIA_TRANSLATOR`, which `src/kernel/Makefile` sets from
`OKAPIA_JIT_ARM64` for the shared objects alone, so the PowerPC window says the same as the 68k one.

The network passes the first three and not the last: a Macintosh starts without it. It earns its page on the cost
alone — a Wi-Fi password is the setting most likely to be wrong, it is found wrong only on the board, and correcting
it otherwise means taking the card out. Until an extension lets the Mac choose its network itself, nothing in the
guest can.

The Logs page passes all four: reading the log meant taking the card out and editing it on another computer, and
a board that will not start is the one whose log is wanted. Its two switches are there because the serial port is
the one cost the log has, and a Pi with no cable attached was paying it at every boot. It earns no place for the log's
*content*: the page shows it and changes none of it.

The Display page controls which modes the guest drivers expose and what they open at the next Macintosh start.
The page also compares the output frame buffer and EDID and reports HDMI directives found on the card. See
[Display](display.md#using-it).

### The window and its pages

Each page is still its own screen — its own array of components, its own `Draw`, `Operate` and `Sync` — and
`PageBeginTabbed()` (`okapia_page.cpp`) gives every one the same frame: the strip of tabs in the title's place, and the
footer with the window's three buttons. `PageChromeAnswer()` says what a press on those means (`TPageAnswer`, numbered
from 1000 so a page's own answers never meet them); the firmware asks it before the page's own `Operate`.
`PageChromeSet()` tells the frame, before each layout, whether a restart is owed (Start's words, so its width: a change
lays the page out again), whether the keyboard starts on the strip, and whether there is anything to start.

The firmware (`RunScreens()`, `okapia_menu.cpp`) keeps one model per page for as long as the window is open, so
turning pages loses nothing, and `SaveAll()` writes settings and network only if they changed, then the volumes and
`SavePrefs()` once. A page whose model changes on its own — the networks in range — has a `Tick` in its driver, asked
four times a second.

The **tabs** are a component (`WidgetTabs`): items for the names, the choice for the page in front. The theme draws
them (`DrawTabs`) and answers where each stands (`ThemeTabRect`), so drawing and hit-testing share one geometry. The
front tab is a step taller, white and open onto the page; the others are light grey behind it. Each shape reaches a
radius below the rule and that band is wiped — it belongs to the strip (`ThemeTabsHeight()`), so the component never
draws outside itself. Its focus ring circles the front tab's label, inside the tab.

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

Everything the menu changes goes to `BasiliskII_Prefs` through one `SavePrefs()` when the window closes, which keeps lines it does not
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

**Screens are pure**: handed values, answering values. That is what lets `tests/host` drive them with
synthetic events and measure them.

**The driver is the same on every host.** `src/common/okapia_menu.cpp` joins the pages to the preferences and the
card's volumes (`FirmwareRun()`, `RunScreens()`, `RunPage()`, the `Load*`/`Store*` functions). What it needs of the
machine goes through `src/common/okapia_host.h`: the log, sleeping and polling, the clock, the output surface and
its facts, input, the card's space and files, the sound outputs offered, the network page's questions. The Pi's
answers are `src/hosts/circle/firmware/okapia_host_circle.cpp` (FatFs, Circle, the board's network and clock); a
desktop's are `src/hosts/desktop/`. No POSIX or Circle type crosses the interface, and paths are the card's own,
rooted at `/`. See [Okapia on a desktop](../notes/research/desktop-host.md#the-okapia-host-interface).

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
into crawling squares. The era shows in the folder icons, not in the frame.
**Tabs** are taken from Mac OS 8 the same way: the grammar (a row of names standing on a rule, the chosen one in
front and open onto its page) without the Platinum relief — an outline, a rounded top, and grey for the tabs behind,
the one depth this theme allows itself. Alerts keep the Macintosh's three
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
- [x] One window with tabs — System, Settings, Network, Information — and a shared footer; Control-Tab and the
      arrows turn pages; changes kept when the Mac starts or the board shuts down (2026-11-12)
- [x] Network page: connection, Wi-Fi networks with lock and signal, secret password field, country, Search and Join
      with the outcome on the page (2026-11-12)
- [x] Restart in the shut down alert (2026-11-12)
- [x] Date & Time: the clock and its source, live; time zone with summer time; time server (2026-11-12)
- [x] Settings in two columns and two named groups (`WidgetGroup`, `PageGroupBegin()`/`End()`), the Date & Time page
      folded back into it; "Show this menu at startup" moved to the footer, beside Start; the tabs behind a lighter grey
      (`ColorPale`, `#EEEEEE`) (2026-11-12)
- [x] JIT and QuickDraw acceleration tick boxes beside the language, "(68K only)" (2026-11-15)
- [x] Logs page: the log on the card in a list, the previous session, serial output and counters (2026-11-18)
- [ ] Logs page tried under QEMU and on the Pi 4; the serial gate measured on hardware
- [ ] Tabs and the Network page tried on the Pi 4 and under QEMU
- [ ] **Open the menu whenever the Macintosh cannot start**, with the reason on screen, instead of halting. Today
      the menu opens by itself only for a missing configuration or no bootable volume; any other failure of
      `CKernel::StartMacintosh()` — no `rom` line, ROM file missing, wrong size or unreadable, ROM refused by
      `CheckROM()` (`MacROMLoad`, `src/common/main_okapia.cpp:190` and its SheepShaver twin), Mac RAM not
      allocated — logs to the serial port only and ends in `OkapiaHalt` (`src/kernel/kernel.cpp:485`): a frozen
      screen on a board with no UART attached. Both engines, and every restart, not only power-on
- [ ] **Repair dialog** — today `HfsRepair` scavenges silently. Three states, not two: repair without asking,
      ask, never repair — through a second boolean `hfsrepairask`, without changing the type of `hfsrepair`
      (`prefs_okapia.cpp`: the parser reads by declared type, an integer would read `0` on every existing card).
      The dialog must time out and repair on expiry, or `run-test.sh` stops on it. Strings and a specimen
      exist; the screen is not wired
- [ ] System folder icons at non-integer scales (vector or larger sources)
- [ ] Diagnostic line when a System needs the File System Manager 1.2 for the shared folder
- [ ] Provisioning screens (catalogue tree, progress) — see [Storage](storage.md#provisioning)

## Pitfalls

- **A field drawn under the footer passes `CheckFrame`**: it is still inside the dialogue. Each page checks its own
  `…Overflowed()` and that no row came out flat; only that catches a page that ran out of room.
- **A group's frame overlaps everything it holds**, on purpose: the overlap checks exempt `WidgetGroup` and check
  containment instead.
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
- **`ScreenInit()` obeys the first component that declares the focus and clears nothing**: a tabbed page declares it
  on Start *or* on the strip, never both, or two rings are drawn. After a relayout the loop puts the focus back where
  the hand was (`ScreenFocus()`), not where the layout declared it.
- **A ring outside the tabs would cross the rule into the page**: it circles the label instead, and the geometry
  checks give `WidgetTabs` no reach, as they do tick boxes.
- **The footer reserves the reach of a default button that also has the focus**: Start is both, and its grey ring sits
  outside the black one — reserving only the default's pushed it into the frame's margin.
- **A menu waiting in `CTimer::MsDelay()` holds Circle's tasks still**: the Wi-Fi's scan never came back. The menu
  sleeps through the scheduler once there is one (`FwSleep()`).
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
- **2026-11-12** — The chooser, the settings and the information pane became pages of one tabbed window, and a fourth
  page for the network. Settings lost Save and Cancel to the window's Start; the information pane lost Back.
- **2026-11-12** — A fifth page, Date & Time. The zone and the time server went into Settings first and pushed the
  server's field under the footer at 640×480 — `CheckFrame` passed, because the field stayed inside the dialogue;
  only the render showed it. The new page checks `DateTimeOverflowed()` like the network page does.
- **2026-11-12** — Tried on the Pi and reworked: the menus two to a row freed the room, so the Date & Time page went
  back into Settings as a group; the folder sharing became a group too. Settings now checks `SettingsOverflowed()`,
  that a group holds what it frames, and that the restart note is read whole beside the footer's tick box — which
  is why the note became "* Restart required". The box first read "Always show this menu", which said too little;
  "Show this menu at startup" fits. The hint on setting the clock by hand went into an (i) on the date's line.
- **2026-09-04** — `okapia_page` holds the shared page frame; the chooser folded into it with its twenty checks as
  the refactor's safety net.

## References

- Tests: `tests/host/check_geometry.cpp`, `check_screen.cpp`, `check_chooser.cpp`, `check_pages.cpp` (the network
  page's behaviour, its password never drawn or copied); renders: `render_specimen`, `render_chooser`, `render_pages`
  (`-network.ppm`)
- Mac OS 8's tab control: `kControlTabLargeProc` (128) and `kControlTabSmallProc` (129), "new with the Appearance
  Manager" — *Mac OS 8 Toolbox Reference*, Control Manager, Developer CD June 1998
- `scripts/specimen.sh [page|live]` — the specimen under QEMU
- Circle `lib/input/usbkeyboard.cpp:200`, `lib/input/mouse.cpp:85`, `Rules.mk:271`
