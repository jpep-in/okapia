<!-- Copyright (C) 2026  Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Desktop

> Okapia as a desktop application: one executable opens a folder laid out like an Okapia card, shows the same boot
> menu and starts the same Macintosh, 68k or PowerPC, on macOS today (Linux has what it needs; Windows needs a file host). It
> replaces the launchers and preference editors of upstream Basilisk II (`PrefsEditor.mm`, `prefs_editor_gtk.cpp`).
> Main files:
>
> - `src/common/okapia_host.h`: what Okapia asks of the machine.
> - `src/common/okapia_menu.cpp`: the boot menu's driver, the same on every host.
> - `src/hosts/desktop/`: the desktop host (SDL3 and POSIX), its shim of the Circle classes, its entry point and
>   test mode.
>
> The study behind it is [Okapia on a desktop](../notes/research/desktop-host.md).

## Using it

For the short user paths, see [Start on Mac](../using/mac.md) and [Start on Raspberry Pi](../using/pi.md). The same
application is the emulator and card installer. It creates no bundled Apple system: a workspace still needs the
user's ROM and bootable image. There is no public package yet; [Releases](../contributing/release.md) separates the
implemented installer/update announcement from planned signing and app self-replacement.

```bash
make -C src/hosts/desktop                  # build/desktop/okapia (scripts/gen-cpu.sh once before)
build/desktop/okapia qemu/sd-contents      # a card folder, or a card mounted on the Mac
```

**As an application.** `scripts/make-desktop-app.sh` packages the same executable as `build/desktop/Okapia.app`
(about 5 MB). SDL3 is in `Contents/Frameworks`, the logo is the icon, and the bundle is signed ad hoc with the
`allow-jit` entitlement. It opens by double-click on the Mac that built it. Gatekeeper rejects it elsewhere
(`spctl -a` says *rejected*), so another Mac needs a right-click > Open the first time, until it is notarised.
Copied next to a card folder's contents, or into one, it opens that folder by itself.
Finder's copyright field shows `Copyright © 2026 Jonathan Pepin`, set by the packaging script's
`NSHumanReadableCopyright` entry.

**The Okapia Workspace.** The folder laid out like a card, which the application opens, is called the Okapia
Workspace. The first of these that applies is opened:

1. the folder named on the command line;
2. the folder holding the executable, when it is a card folder (one with `Okapia_Prefs`);
3. the last folder opened in a window (a headless test run is never remembered);
4. otherwise, the **welcome page**. Holding Shift while the application opens shows it whatever the above would have
   chosen.

**The welcome page** (`src/firmware/okapia_welcome.cpp`, driven by `FirmwareWelcome()` in `okapia_menu.cpp`) is a
firmware page like the boot menu's, drawn by Okapia's own toolkit, so it is the same on every OS. The logo and two
sentences say what a workspace is, a **For** menu says whom it is prepared for — *This computer* or *Raspberry
Pi 4*, as the installer's — and three steps follow, read from top to bottom as the installer's are:

Its window is its own (`DesktopWindowForWelcome`): **borderless**, the page's frame being the window's edge, the
band over its title moving it (`SDL_SetWindowHitTest`); it opens at the menu's 640×480 design times the largest
of 1, 1.25, 1.5… that leaves a sixth of the screen free, and is **resized with the mouse** down to 600×320 design
units. The page fills it at the scale it opened with (`PageFixScale`, `PageBegin` with no width), in the screen's own
pixels: its output is the window's points times their density (2 on Retina; `--density` headless), so its
outlines are rasterised at the size they are seen, as the boot menu's are. Its bands,
scroller and pointer are in those pixels; the pointer's events arrive in them (`HandleEvent` converts every event
to the renderer's coordinates), while SDL's window sizes and the hit test's areas, in points, are multiplied by
the density. When the page needs
more height than the window has, the output is made that tall (`HostOutputExtend`, after a layout on the tallest
output to measure it) and the host shows it in three bands, as the installer did: the header (the logo beside the
title and what Okapia is, closed by a rule across the window) and the buttons (under the same rule) stay in place
(`HostOutputBands`, `WelcomeBands`), and the steps between them scroll under an overlay scroller, inside the page's
frame, that the host draws and drives itself (wheel and trackpad in the direction the system's setting gives, the thumb dragged, a click
in its column), pointer coordinates staying the output's (`HostOutputResized`, polled by `RunPage`; a headless
screenshot is the window so composed). With a scroller the *Disk* step names every image, up to `WELCOME_LINES`,
instead of two and a count. The SD card sheet takes exactly the window. The size is never saved: on *Continue* the
main thread gives the same window back its title bar and the boot menu's size and mode (`DesktopWelcomeEnd`), and
the output is made again at that size.

1. **Okapia Workspace** for this computer: *Open…* an existing folder, or *Create…* one; under it the folder's
   room. For a Raspberry Pi the step is its **SD card**: *SD Card…* one of those inserted, or *Folder…*, a folder
   standing in for one (the installer's Advanced), with the card's verdict for a Pi under it. A removable volume
   chosen or opened sets the menu to the Pi; a folder leaves it as it was. *Create…* makes one through the system's save panel
   (`HostPickWorkspaceNew`: a place and a name, `Okapia Workspace` proposed in Documents; a folder of that name is
   used only when empty), with the welcome page's language written into its preferences;
2. **Macintosh ROM**: one line an engine, **68K** and **PowerPC**, as the installer shows them: the ROM the
   preferences name, else the first usable one of that family, identified (`src/common/okapia_rom.cpp`: the
   68k checksum and the ROMs it knows by it, the Quadra F1ACAD13 recommended; Old World by its nanokernel, New
   World by `<CHRP-BOOT>`) with the Systems it starts below it; when there is none, the one to look for.
   *Choose…* / *Replace…* copies a file in and it goes to its family's line (`rom` or `romppc`); an unusable
   file is refused with its reason. *Listen* plays its startup chime as the board plays it (`HostChimeListen`,
   `rom_chime`), when this reader knows one;
3. **The Mac's disk**: two disks, then how many others, each with its System and what the ROMs at hand make of
   it (`DiskJudge`: starts with the 68K ROM, the PowerPC one, either, needs one, too new, a data disk). The
   status line says when no disk can start with these ROMs. *Add a disk image…* copies an image in and adds a
   `disk` line, or a `cdrom` line for `.iso`, `.toast`, `.cdr`. A grey paragraph says which images serve: raw
   images of a hard disk or a CD under the names the inventory knows (`hfs_volume_okapia.cpp`,
   `LooksLikeAnImage`), a single volume or a whole disk, HFS or HFS+ from 8.1; a compressed one (`.dmg`, `.sit`,
   compressed Disk Copy) converted first. Upstream skips a header of up to 511 bytes and Disk Copy 4.2's 84
   (`FileDiskLayout`, `macos_util.cpp:95`), nothing more.

**Files copied in by hand** count as the buttons' do, and the workspace step says so. The page reads the folder's
top level again every two seconds (`WelcomeFolderSignature`: names, sizes, times) and lays itself out again on a
change; on *Continue*, a ROM the page shows but the preferences do not name is written into them (`rom`,
`romppc`), since the engine loads only the one named.

**An SD card as the workspace** (the installer's card step): *SD Card…* opens a sheet listing the removable
volumes mounted now, read again four times a second so a card appears when inserted (`HostCards`,
`cards_desktop.cpp`: `getmntinfo` and DiskArbitration on macOS, ejectable or removable and not a disk image;
`/proc/mounts` and the device's `removable` in `/sys` on Linux, not yet tried). Chosen, the card is the
workspace: what is added is copied onto it, and the Mac runs from it as on the board. Under the path, what the
volume is for a Pi (`okapia_card_check.cpp`, `CardDiagnose`): FAT12 or FAT16 (the Pi needs FAT32), another file
system, clusters under 32 KB, the last session switched off with a Macintosh running (a session that only showed
the boot menu counts as clean); otherwise card or folder, FAT32 and its clusters, the room left, and the Okapia
build on it (read in `kernel8-rpi4.img`) or that it is not there yet. What the welcome page writes onto a FAT
card loses the `._name` file macOS keeps its provenance attribute in (`HostTidy`). `--cards-images` lets a
mounted disk image stand in for a card in a test.

**Putting Okapia on the card.** For a board, the footer's default button is **Prepare the Card** (**Update**
when an older Okapia is on it, which the boot menu also offers at the top right of its tab strip, the release
named on a line above, opening this page on the card); the board a card's own manifest names is chosen, a folder copy included. The status line says what the release is and then how the work goes. The
release is looked for in the background when the page opens: GitHub's latest of `OKAPIA_REPOSITORY`, through
libcurl, HTTPS only, a `User-Agent` of `Okapia` and nothing else (`install_fetch.cpp`); or a release on this
computer with `--release PATH` (a folder, its manifest, or a board's zip). Its manifest's boards fill the *For*
menu after *This computer* ("Raspberry Pi 4" until one is read), so a board a release adds appears without a new
Okapia. Preparing runs on a thread of its own (`install_host.cpp`): the board's zip downloaded into the app's
support folder (`releases/`), unzipped with miniz (`external/miniz`, MIT), each file checked against the manifest,
then written onto the card by the rules below.

`src/hosts/desktop/install.h` is the installer's core in C++ (`install_manifest.cpp`, `install_card.cpp`,
`install_release.cpp`), with its rules: a release is data, `okapia-manifest.json` naming the boards it is built
for, each with its zip and its files (name, size, SHA-256), and its desktop builds
([Releases](../contributing/release.md)); only the files the manifest names are written; each goes in as
`<name>.okapia-new`, flushed, read back against its hash and renamed; a file of Okapia's edited by hand
(`config.txt`) is kept as `.bak`; what the last release owned and this one dropped goes when untouched; the card's
manifest, resolved to its board, goes last. A later schema is read for its frozen
core only (version, page, apps: the app can still update itself), and it, or a board needing a newer installer
protocol than `INSTALLER_VERSION`, prepares no card: the welcome page says to update Okapia. `scripts/make-release.sh` (from
okapia-installer's `make-demo-release.sh`) assembles a release from a built tree. Tested on the host:
`tests/host/check_install.cpp` (JSON, SHA-256 vectors, manifests, zips, an install into a folder over two
releases, okapia-installer's demonstration release installed whole).

The rules are the installer's (okapia-installer), ported and tested on the host (`tests/host/check_rom.cpp`,
with the ROMs of `qemu/sd-contents` when the tree has them; `tests/host/check_card.cpp` for the card).

Files are **copied** into the workspace, cloned on APFS (`copyfile(COPYFILE_CLONE_FORCE)`: instant, no space until
either side changes), elsewhere read and written; either way synced with `F_FULLFSYNC` (`sync_desktop.cpp`). Not
`COPYFILE_CLONE`: where a clone is impossible (a FAT card, another volume) it falls back to a copy of its own and
never syncs it. The copy goes to a hidden `.okapia-copy` renamed when whole, the directory synced after the rename,
and the preferences file is written beside and renamed, so an abrupt stop leaves the old state or the new one.
A file already in the workspace is named, not copied again. *Continue*, once there is a workspace, goes on in the same
process and window, as a board goes on from power-on: the log moves into the workspace, the preferences are read
then (the engine reads them, after the welcome page), and the boot menu opens at once (`HostMenuReopen`). Going the
other way, from the boot menu's button, starts the process again: the workspace's preferences and the Mac's memory
are already taken, as a board would be reset. The choosers are SDL's (`SDL_ShowFileDialogWithProperties`), which open the system's
own on every OS, titled with what they are for; they run on the main thread while the engine thread waits.

**Changing workspace**: the boot menu's footer has a button with the logo, beside PRAM and Power, on a desktop only
(`HostHasWelcome()`); it keeps what the pages set, then opens the welcome page on the current workspace. **File >
Change Okapia Workspace…** (Control-O) does the same from anywhere; while the Macintosh runs it asks first, as
quitting does. **File > Show Okapia Workspace in Finder** (Control-R) shows it. The window's title names
it, and *Information* gives its path.

The layout is the card's: `Okapia_Prefs`, the ROMs, the disk images, `BasiliskII_XPRAM`, `shared/`.
`okapia-desktop.log` and `okapia-desktop-previous.log` are written there, beside the board's `okapia.log` rather than over it: a card used here and in a Pi keeps the Pi's log, which the welcome page reads. The Pi's own files (`kernel8*.img`,
`config.txt`, its firmware) are ignored. The folder is locked while Okapia runs, so a second Okapia on the same
folder is refused.

**The menu.** It is the Pi's, page for page.

- **How to open it**: hold Option while the window is grey, or tick *Show this menu at startup*.
- **What changes on a desktop** (`HostKind ()`):
  - *Information* says *Computer* and *Okapia Workspace* (its path) instead of *Board* and *SD card*.
  - *Display* reports the window and the screen instead of the Pi's output, EDID and HDMI.
  - *Date & Time* shows the host's time only: the zone and the time server are the host's, and its (i) says so.
  - The hint under the tick box says Option, not Alt.
- **Network**: one choice on the *Settings* page, *Network: None* or *Shared with this computer (NAT)*, which is the
  wired kind (`ether eth`); the Network tab, the board's (Wi-Fi, cable, country), is left out of the strip and keeps
  no desktop case. The Macintosh then gets 10.0.2.15 by DHCP behind the host, through upstream's slirp, with no
  privilege asked.
- **Sound**: *Off* or *Built-in (this Mac)* (`soundoutput builtin`), the host's default output. A card's HDMI, jack
  or USB plays there too and reads as *Built-in*; a board given `builtin` plays on its jack. The chime plays from the
  ROM the startup volume will use, as on the Pi.

**The Macintosh.** The startup volume's System starts as on the Pi, in the window, through the same compositor (an
integer scale, centred).

- **Mouse**: a click in the window gives the pointer to the Macintosh; Control-Command gives it back.
- **Keyboard**: goes to the Macintosh as a USB keyboard's reports, through the Pi's table (`BasiliskII.keycodes`
  included).
- **Command is the Macintosh's.** The application's own shortcuts — Quit, Hide, Minimize and the rest SDL puts in
  its menus — are on Control (`menus_desktop.mm`). Command-Q quits the application inside the Macintosh, and
  **Control-Q quits Okapia**: in the boot menu at once, while the Macintosh runs after asking, as closing the window
  does.
- **Closing the window while it runs** asks first: quitting then is pulling the plug, and the volumes in use are
  repaired at the next start. Shut down from the Special menu instead.
- **A volume asking for the PowerPC engine** is started by it, in the same process, as on the Pi (Mac OS 8.6 from
  `macosrom16.rom`, tried).
- **Restart from Mac OS 8–9** with the same volume restarts in place, as on the Pi. With another volume or engine,
  the process starts again on the same folder, where the board resets itself, since SheepShaver cannot be entered twice
  in place. The new process starts what the menu chose, without opening the menu a second time
  (`HostMenuAnswered`). A setting that needs a restart, such as the memory, does the same. Shut Down ends Okapia.

**The window.** Its size is chosen on the **Display** page (desktop only), not with the mouse: the *Window* popup
lists the usual sizes that fit the screen (800 × 600 to 2560 × 1440, in points), **Fill the screen** (the usable
area, under the menu bar, title bar kept), **Full screen**, and **Other…**, a size typed in (640 × 480 up to what
the screen holds). The screen's size is shown beside it.
- **What choosing does**: the output (what the menu and the Macintosh draw on) is claimed once per start, as on the
  board. So the pages' settings are kept, and the process starts again straight into the boot menu, on the Display
  page, at the new size (`HostWindowApply`, `OKAPIA_MENU_TAB`). It is the output the Macintosh is then offered as a
  resolution in Monitors (*the display's size*).
- **What is remembered**: the mode, and the size of a sized window (`window WxH`, `fill`, `fullscreen` in the
  application's support folder); Fill the screen and full screen are measured again at each start.
- **The Macintosh's screen alone is magnified** while it runs: the compositor scales by whole output points and says
  where it put the screen, its own size and that factor (`BoardOutputShows`); the window takes the rectangle back to
  the Mac's size (nearest neighbour undoes a whole factor exactly) and magnifies it by the largest whole number of
  device pixels. On a 1512×982-point Retina screen, 1008×654 (two thirds, offered in the list) is three device pixels
  a Mac pixel and fills it, 752×491 (half) four, 512×384 five. The Display page's list says the same, in output
  points (×1,5 for three device pixels at a density of 2: `THostOutputFacts::nDensity`), and *Full screen* when the
  result covers the output to within two device pixels a side.
- **Control-F** (File > Full Screen) still goes full screen and back at once, without a new output: the same picture
  at the largest whole factor that fits, centred and sharp, or shrunk to fit. The mode is remembered for the next
  start.
- **The menu bar in full screen**: full screen covers the desktop rather than taking a Space of its own
  (`SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES` off), so the host's menu bar is Okapia's to show. SDL lifts such a window
  above the menu bar (main menu level + 1), so showing the bar alone left it under the window: the window comes down
  to the normal level while the bar is shown (`DesktopMenuBarVisible`). Over a running Macintosh, Control-Command also gives
  the pointer back, and leaving relative mode hid the bar again: it is shown after the pointer is released, and once
  more 200 ms later. The host's pointer follows one rule everywhere: shown over the menus, which draw
  none of their own on a desktop (`HostPointer`: the system's arrow, or its text beam over a text field; only a
  board's menu draws one, and a headless run's, so a screenshot shows it), hidden while the Macintosh has the
  pointer, shown as soon as Control-Command gives it back. Control-Command, which
  gives the pointer back, shows it too, in the boot menu as over the Macintosh; a click in the window hides it again
  (and gives the pointer back to the Macintosh). In a Space, the menu bar only appears when the pointer touches the
  top of the screen, which a captured pointer never does.
- **Why the Mac's resolution does not follow the window live**: as VMware's window did, that needs a driver inside
  the guest that changes the guest's mode when the host asks (VMware Tools). Classic Mac OS changes resolution only
  through Monitors, which the Macintosh asks for itself. A small extension or control strip module calling the
  Display Manager could do it, a project of its own (Status).

**The test mode.** Same binary, run without a window, driven by options on the model of `snow-runner`; times are
milliseconds since start.

```bash
build/desktop/okapia --headless --work-dir /tmp/card --option --click 593,82,1500 \
    --screenshot info.png --seconds 3 qemu/sd-contents
```

| Option | Effect |
|---|---|
| `--headless` | no window; the picture is still drawn into memory |
| `--work-dir DIR` | copy the folder there first and run on the copy |
| `--size WxH` | the window, in points (default 1280×800 or less) |
| `--option` | hold Option through the power-on window |
| `--keys MS,SEQ` | type at MS: letters, digits, `{return}` `{tab}` `{escape}` `{up}` `{down}` `{left}` `{right}` `{space}` `{delete}` `{backspace}` `{home}` `{end}` `{pageup}` `{pagedown}` `{cmd-X}`; to the menu or to the Macintosh, whichever has the keyboard |
| `--click X,Y,MS` | press and release in the menu, in its points |
| `--screenshot FILE`, `--screenshot-at MS` | PNG of the screen at the end, and at MS (`FILE-MS.png`) |
| `--datetime "YYYY-MM-DD HH:MM:SS"` | the clock, fixed at start and running from there |
| `--seconds N` | end the run after N seconds |
| `--quiet` | only warnings and errors on the terminal |
| `--sound-file FILE` | the sound into FILE, raw 44.1 kHz 16-bit stereo (SDL's `disk` driver), instead of the speakers |
| `--welcome` | the welcome page first, on the folder named if there is one |
| `--release PATH` | the release a card is prepared with (a folder, its manifest, a board's zip) instead of GitHub's latest |
| `--as-release YY.MM.N` | this app pretends to be that release, so a newer one (`--release`) is announced |
| `--cards-images` | a mounted disk image counts as an SD card (to test *SD Card…* with `hdiutil`) |
| `--density N` | device pixels per point a headless run pretends (2 for Retina), for the Display page's scales |
| `--pick PATH` | what the welcome page's next chooser answers, in order (`""` cancels) |
| `--resize W,H,MS` | the welcome window W×H points at MS, as the mouse would (headless: the output is then as tall as the page needs) |

Exit codes: 0 the run ended as asked, 1 an error, 3 Shut Down, 4 quit while the Macintosh ran.

### Against upstream Basilisk II on a Mac

What a user of the upstream macOS build (kanjitalk755's SDL Basilisk II) sees change:

- **Retina: pixel-perfect, not blurred.** Upstream magnifies the Mac's screen with linear filtering at whatever
  scale fills the window: `scale_nearest` and `scale_integer` both default to `false` (`prefs_items.cpp:76-77`,
  `video_sdl3.cpp:874-881`). On a Retina display every Mac pixel is smeared across its neighbours. Okapia draws the
  Mac's output at its size in points and lets the GPU magnify it by an integer factor, nearest neighbour
  (`SDL_LOGICAL_PRESENTATION_INTEGER_SCALE`, `SDL_SCALEMODE_NEAREST`, `okapia_host_desktop.cpp`). The Mac's
  compositor then scales by an integer as well, centred. Each Mac pixel is a sharp square of 2×2 physical pixels or
  more, as on a CRT. Seen on the user's Retina display, 2026-11-21.
- **A boot menu instead of a preferences editor.** Volumes, startup disk, engine, display modes, shared folder and
  network are chosen in the Macintosh's own window, before it starts, with Option, as on the Pi.
- **Both engines in one application.** The startup volume's System decides between 68k and PowerPC.
- **A card folder**: the same files as an Okapia SD card, so a card and the desktop share one set of volumes and
  preferences.
- **The chime** of the ROM the volume will use, and the 68k translator on Apple Silicon (`jit true`), where upstream
  offers no JIT on arm64.

## How it works

**Two threads.**

- **The main thread** owns SDL. It takes the window's events, runs the script's steps, and presents the surface at
  the display's pace (`DesktopMainLoop`, `okapia_host_desktop.cpp`).
- **The emulation thread** runs what the board runs, `okapia_boot.cpp`'s loop: `OkapiaRun68k()`, then
  `OkapiaRunPowerPC()` when the startup volume asks for it, so `CKernel` and `CKernelPPC` from `src/kernel/`,
  unchanged. The boot menu and the Macintosh are on this thread.

**The picture.** Two outputs, `0x00RRGGBB` as on the Pi's (2026-11-22). The menu's (`HostOutputClaim`) is in the
screen's device pixels, the window's points times their density, so its text is rasterised at the size it is
seen; its scale is the one the points give times the density (`PageFixScale`), the menu being no larger on a 5K
screen than before, only sharper. The Macintosh's (`FwOutputClaim`, `DesktopSurface`) stays in points: Monitors is
offered the window's size and its fractions in points (`HostWindowDescribe` gives the Display page those, not the
menu's pixels), and the compositor draws into it. The main thread shows the Macintosh's while it runs, magnified
by the GPU by nearest neighbour, the menu's otherwise, 1:1. Before, the menu drew in points too and the GPU
doubled it: every antialiased edge became a 2×2 block.

**Input.**

- **While the menu is up**, events become `TEvent`s in a locked queue. SDL's scancodes are USB usages, so keys go
  through `KeyOfUsage()` (`src/common/okapia_keys.h`), which the Pi's `okapia_input.cpp` also uses.
- **While the Macintosh has the keyboard**, events become a HID report (a modifier byte and six keys) and relative
  mouse motion. They are handed to the handlers `input_okapia.cpp` registered (`board_desktop.cpp`), so the table and
  the acceleration curve are the Pi's.

**What builds unchanged.** All of `src/firmware/` and `src/common/`, the platform files (`*_okapia`) and
`src/common/sheepshaver/` among them; `src/kernel/`'s two `CKernel`s; Basilisk II's core with `uae_cpu_2021` and the
80-bit softfloat FPU; SheepShaver's core with `kpx_cpu`; upstream's `sys_unix.cpp`, `extfs_unix.cpp` and slirp;
libhfs. They compile against `src/hosts/desktop/shims/`:

- **The Circle classes they use**: `CTimer`, `CMemorySystem`, `CSpinLock`, `CBcmFrameBuffer`, the keyboard and mouse
  devices.
- **A `hal_circle.h`** declaring the board's functions.
- **`board_desktop.cpp`**, which plays the board: a tick thread on an absolute schedule for `BoardFineTick()`, the
  host's clock for `Clock*()`, and the Mac RAM block with its heap.
- **`sound_desktop.cpp`**: the board's sound device as an `SDL_AudioStream` on the default output, for the Pi's pump
  (`audio_okapia.cpp`), and the chime, rendered by `rom_chime.cpp` and queued whole.
- **`board_net_desktop.cpp`**: the board's network interface as slirp, for the Pi's Ethernet driver
  (`ether_okapia.cpp`), polled from the same 1 kHz seam, on the emulation thread.
- **`circle/startup.h`**: `halt()` ends the process; `reboot()` starts it again on the same folder.

**The card's paths.** Preferences and the menu keep the card's own form, `/boot71.img`. `CardPath()`
(`src/common/okapia_card.h`) names the file under the folder at the places that open one. It is the identity on the
Pi. Upstream's file opens are reached by the hooks below.

**The `--wrap` hooks, on a linker without `--wrap`.**

- **The renaming**: upstream's definition is renamed by a `-D` on the one file that holds it
  (`src/hosts/desktop/Makefile:282-286`, and the PowerPC engine's below its rule). `wraps_desktop.cpp` then gives the original name to the Pi's hook and the
  `__real_` name to upstream's code.
- **The hooks covered**: `EmulOp` (68k), `extfs_write`, the two name encodings, Open Transport's three entry points
  (PowerPC), and `Sys_open` and the shared folder's root, which go through `CardPath()`.

**Two engines, one process.** Both cores define `InitAll`, `PatchROM`, `Execute68k`… On the board every symbol the
PowerPC object defines is renamed `ppc__` with `objcopy`. Here the PowerPC objects are joined by
`ld -r -exported_symbol _OkapiaRunPowerPC` (`src/hosts/desktop/Makefile:198`). A Mach-O partial link turns every
other global it defines into a local one, which leaves the same seam, one function wide. Its references to the board,
the host and the menu stay undefined and meet the shared objects at the final link.

**The build.** `make -C src/hosts/desktop`:

- **Compiler and optimisation**: host clang, `-O3`. `-O2` on the Pi is for its Device memory.
- **The translator**: on an arm64 host the 68k→AArch64 translator is built in (`OKAPIA_JIT_ARM64 ?= 1`), with
  patch 0012's own `MAP_JIT` memory. The `jit` preference decides whether a Macintosh uses it; the log says
  *Translator running*.
- **The 68k opcode tables** are the kernel's, in `build/generated/`, from `scripts/gen-cpu.sh`.

## Status

- [x] Host interface for the boot menu; the Pi's menu behind it, unchanged under QEMU (2026-11-21)
- [x] The menu on macOS, windowed and headless, from a card folder; pages adapted by `HostKind ()` (2026-11-21)
- [x] Test mode: headless, a copy of the folder, scripted keys and clicks, screenshots (2026-11-21)
- [x] The 68k engine in the process: System 7.1.2 to the Finder, the shared folder mounted (2026-11-21)
- [x] A process killed during a session: the volumes in use repaired at the next start (2026-11-21)
- [x] The translator on macOS: `jit true` to the Finder (2026-11-21)
- [x] Sound: the Pi's pump on an `SDL_AudioStream`; the chime from the ROM, checked in a file (2026-11-21)
- [x] Network: slirp under the Pi's Ethernet driver, `ether eth`; the page says *Shared with this computer* (2026-11-21)
- [x] PowerPC engine in the same process, the switch from the 68k one; Mac OS 8.6 to the Finder (2026-11-21)
- [x] The shared files in `src/common/` (`*_okapia`), the Circle host's in `src/hosts/circle/` (2026-11-21)
- [x] Okapia Workspace: File menu, path shown; Shift asks at launch (2026-11-22)
- [x] Welcome page: open or create a workspace, add a ROM and a disk, copied in; footer button (2026-11-22)
- [x] Card preparation from a release manifest, archives checked and installed by the welcome page; folder-based
      host tests pass, with a real removable card still to validate ([Releases](../contributing/release.md), 2026-11-22)
- [x] App update announcements open the release page; an older Pi card offers Update in welcome and the boot menu
      (2026-11-23–24). App self-replacement is still planned, not part of this announcement.
- [x] Sound: *Off* and *Built-in (this Mac)* only; network on the Settings page, no Network tab; Date & Time help for a desktop (2026-11-22)
- [x] Window size from the Display page: sizes, Fill the screen, full screen, typed; no mouse resizing (2026-11-22)
- [x] Full screen's menu bar shown by Control-Command, hidden by a click (2026-11-22)
- [x] Engine switch after a PowerPC restart: the new process starts the menu's answer (2026-11-22)
- [ ] Restart from Mac OS back to the menu, tried by hand (68k: `MacRestarted()`; PowerPC: `reboot()`)
- [ ] Mac OS 8.6's sound: no source ever opened (`0 sources`) while 7.6.1 on the PowerPC plays
- [ ] The Mac's resolution following the window live: a guest-side extension (Display Manager), a study
- [ ] The Macintosh's own sound heard (only the chime was checked), and traffic through slirp from a Mac OS with TCP/IP
- [x] A flush policy for the disk images (2026-11-26): `sys_unix_desktop.cpp` compiles upstream's `sys_unix.cpp`
      with `Sys_write` and `Sys_close` renamed, a barrier (`F_BARRIERFSYNC`) after every write to a writable image
      and `F_FULLFSYNC` at its close; a write the host does not confirm is a failed write for the guest. A volume
      repaired at startup is synced before the Mac writes on it
- [ ] The cost of that policy measured (Speedometer's Disk, a Finder copy), and a power-cut test on a FAT card
- [ ] The zone override; Linux build
- [ ] ThreadSanitizer over the tick, input and picture threads

## Pitfalls

- **Pointer events are already in the output's coordinates.** `HandleEvent` passes every event through
  `SDL_ConvertEventToRenderCoordinates`; when the welcome page went to device pixels, multiplying its events by the
  density as well put every click and the pointer at twice their place (2026-11-22). Only the hit test's areas and
  the window's sizes come in points.
- **A page run outside `FirmwareRun()` must give the pointer its bounds.** The welcome page's clicks all landed
  inside 640 × 480, the input's default, until `FirmwareWelcome()` called `HostInputBounds()` as `FirmwareRun()` does.
- **A resizable window lies about the output.** Dragging the window's edge changed nothing the menu could use: the
  output is fixed per process, so the picture was only scaled, blurred when shrunk. The size is now a menu choice that
  starts the process again; the window is not resizable.
- **A dialog belongs to the main thread.** The welcome page runs on the engine thread; `HostPick()` hands the chooser
  to the main thread with `SDL_RunOnMainThread()` and waits on a semaphore, and the window title follows the same way.
- **A shim cannot shadow a header included by quotes from beside it.** `#include "hal_circle.h"` in a file that
  sat beside `hal_circle.h` found that one before any `-I` directory; that is why the shared platform files moved
  to `src/common/` and the Circle headers to `src/hosts/circle/`. The shim `hal_circle.h` still defines the real
  header's guard (`OKAPIA_HAL_CIRCLE_H`), and the force-included `shims/okapia_circle.h` includes it first.
- **The Mac's heap must lie above its RAM.** With `DIRECT_ADDRESSING`, a Mac address is a host address minus
  `MEMBaseDiff`, the RAM's base. `malloc` placed the frame buffer *below* it, its Mac address wrapped, and Monitors'
  grey fill (`video.cpp:630`) wrote 4 GB away. `MacRamClaim()` now reserves the RAM and a 512 MB arena after it, and
  `CMemorySystem::HeapAllocate()` serves from there (`board_desktop.cpp`). On the board, Circle's heap does this by
  itself.
- **SDL's offscreen video driver wants OpenGL.** It refused to make a window with *Could not initialize OpenGL /
  GLES library*. Headless means no window at all here: the surface is the picture, and it is written out as PNG.
- **`exit()` with the Macintosh running crashes** (SIGSEGV in the translator), because the destructors pull its
  world away. The test mode's deadline ends with `_exit()` (`EndRun`, `main_desktop.cpp`). It works on a copy, so
  that is the plug pulled on purpose, as `run-test.sh` does.
- **Two files of one name in two directories meet in a pattern rule.** `main_okapia.cpp`, `video_okapia.cpp` and
  `cpu_ticks_okapia.cpp` exist in `src/common/` and in `src/common/sheepshaver/`. A rule `p_%.o: $(COMMON)/%.cpp` gave
  the PowerPC engine the 68k engine's `main`. The desktop build names the PowerPC's `ps_`, and the kernel's VPATH
  puts the engine's own directory first.
- **kpx_cpu wants C++14**: `std::unary_function`, gone from libc++ in C++17. The PowerPC objects build with
  `-std=gnu++14`.
- **SheepShaver's `uintptr` is `uint64_t`**, which Darwin spells `unsigned long long`, not Basilisk's `unsigned long`.
  The shim's `circle/types.h` follows the engine.
- **Starting the process again must not ask the same question twice.** A PowerPC restart onto another volume, or a
  setting that needs a restart, starts a new process. With *Show this menu at startup* ticked, that process opened
  the menu again, and the chosen volume never started: it read as a loop. `reboot()` now sets
  `OKAPIA_MENU_ANSWERED`, and the new process skips the menu once.
- **A test run must not become the user's workspace.** Headless runs remembered their folder as the last opened, and
  the application then opened a test's card. Only a window's folder is remembered now.
- **The 27.2 SDK does not link on this machine**: the Makefile picks a 26.x `SDKROOT`, as `tests/host` does. The
  Homebrew SDL3 is built for 27.2, which the linker only warns about.

## Development notes

- **2026-11-21 — From the study to the Finder in one session.** The order was:
  1. the menu's driver behind `okapia_host.h`, the Pi checked under QEMU;
  2. the menu on macOS, by mouse clicks in headless runs;
  3. the shim and `board_desktop.cpp`;
  4. the link with five symbols missing (sound and network pumps, a disk factory);
  5. a crash on the frame buffer's address (Pitfalls);
  6. the Finder.

  Boot measured at 2.8 s from power-on to *Macintosh idle* on an Apple M-class machine, interpreter or translator.
- **2026-11-21, evening — Sound, network, PowerPC, the move.**
  - **Sound**: the pump needed four methods of a device (`Write`, `GetQueueSizeFrames`, `GetQueueFramesAvail`,
    `IsActive`), which an SDL stream gives.
  - **Network**: `ether_okapia.cpp` needed only the `BoardNet*` functions.
  - **PowerPC**: the link was right the first time (`ld -r -exported_symbol`). The ROM was first not found, because
    its loader opened the card path as it was; with that fixed, Mac OS 8.6 reached the Finder.
  - **The move**: the shared files went to `src/common/` as `*_okapia`, because without a suffix they meet upstream's
    `prefs.cpp`, `video.cpp`, `timer.cpp` in the kernel's VPATH. Pi checked again after it: QEMU to the Finder, the
    specimen identical to the host render, the host tests.

## References

- [Okapia on a desktop](../notes/research/desktop-host.md): the study, the decisions taken in its brainstorm, the
  milestones.
- [Boot menu](boot-menu.md), [Display](display.md), [Input](input.md), [Storage](storage.md).
- SDL3 (`/opt/homebrew/include/SDL3`, 3.4.20).
