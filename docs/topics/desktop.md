# Desktop

> Okapia as a desktop application: one executable opens a folder laid out like an Okapia card, shows the same boot
> menu and starts the same 68k Macintosh, on macOS today (Linux has what it needs; Windows needs a file host). It
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

```bash
make -C src/hosts/desktop                  # build/desktop/okapia (scripts/gen-cpu.sh once before)
build/desktop/okapia qemu/sd-contents      # a card folder, or a card mounted on the Mac
```

**The folder.** The first of these that applies is opened:

1. the folder named on the command line;
2. the folder holding the executable, when it is a card folder (one with `BasiliskII_Prefs`);
3. the last folder opened;
4. otherwise, a folder picker.

The layout is the card's: `BasiliskII_Prefs`, the ROMs, the disk images, `BasiliskII_XPRAM`, `shared/`.
`okapia.log` and `okapia-previous.log` are written there as on the card. The Pi's own files (`kernel8*.img`,
`config.txt`, its firmware) are ignored. The folder is locked while Okapia runs, so a second Okapia on the same
folder is refused.

**The menu.** It is the Pi's, page for page.

- **How to open it**: hold Option while the window is grey, or tick *Show this menu at startup*.
- **What changes on a desktop** (`HostKind ()`):
  - *Information* says *Computer* and *Card folder* instead of *Board* and *SD card*.
  - *Display* reports the window and the screen instead of the Pi's output, EDID and HDMI.
  - *Date & Time* shows the host's time only: the zone and the time server are the host's.
  - The hint under the tick box says Option, not Alt.
- **Network**: offers nothing yet; the Macintosh has no network.
- **Sound**: the Macintosh is silent for now.

**The Macintosh.** The startup volume's System starts as on the Pi, in the window, through the same compositor (an
integer scale, centred).

- **Mouse**: a click in the window gives the pointer to the Macintosh; Control-Command gives it back.
- **Keyboard**: goes to the Macintosh as a USB keyboard's reports, through the Pi's table (`BasiliskII.keycodes`
  included).
- **Closing the window while it runs** asks first: quitting then is pulling the plug, and the volumes in use are
  repaired at the next start. Shut down from the Special menu instead.
- **A volume asking for the PowerPC engine** brings the menu back: that engine is not in this build yet.

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

Exit codes: 0 the run ended as asked, 1 an error, 3 Shut Down, 4 quit while the Macintosh ran.

## How it works

**Two threads.**

- **The main thread** owns SDL. It takes the window's events, runs the script's steps, and presents the surface at
  the display's pace (`DesktopMainLoop`, `okapia_host_desktop.cpp`).
- **The emulation thread** runs what the board runs: `OkapiaRun68k()`, so `CKernel::Initialize()` and
  `CKernel::Run()` from `src/kernel/kernel.cpp`, unchanged. The boot menu and the Macintosh are on this thread.

**The picture.** The menu and the Mac's compositor draw into one surface, the window's size in points,
`0x00RRGGBB`, as on the Pi's output. The main thread copies it into a texture that the GPU doubles to Retina pixels
by nearest neighbour.

**Input.**

- **While the menu is up**, events become `TEvent`s in a locked queue. SDL's scancodes are USB usages, so keys go
  through `KeyOfUsage()` (`src/common/okapia_keys.h`), which the Pi's `okapia_input.cpp` also uses.
- **While the Macintosh has the keyboard**, events become a HID report (a modifier byte and six keys) and relative
  mouse motion. They are handed to the handlers `input_circle.cpp` registered (`board_desktop.cpp`), so the table and
  the acceleration curve are the Pi's.

**What builds unchanged.** All of `src/firmware/` and `src/common/`, and from `src/circle/`:

- `card`, `compositor`, `cpu_ticks`, `emul_op_hook`, `extfs_sync`, `hfs_volume`, `input`, `main`, `mac_encoding`;
- `net_config`, `prefs`, `quickdraw_blit`, `quickdraw`, `rom_chime`, `tick`, `timer`, `user_strings`, `video`,
  `video_shared`, `video_sizes`, `xpram`.

Also unchanged: `src/kernel/kernel.cpp` and `okapia_main.cpp`, Basilisk II's core and `uae_cpu_2021` with the 80-bit
softfloat FPU, upstream's `sys_unix.cpp` and `extfs_unix.cpp`, and libhfs. They compile against
`src/hosts/desktop/shims/`:

- **The Circle classes they use**: `CTimer`, `CMemorySystem`, `CSpinLock`, `CBcmFrameBuffer`, the keyboard and mouse
  devices.
- **A `hal_circle.h`** declaring the board's functions.
- **`board_desktop.cpp`**, which plays the board: a tick thread on an absolute schedule for `BoardFineTick()`, the
  host's clock for `Clock*()`, and the Mac RAM block with its heap.

**The card's paths.** Preferences and the menu keep the card's own form, `/boot71.img`. `CardPath()`
(`src/common/okapia_card.h`) names the file under the folder at the places that open one. It is the identity on the
Pi. Upstream's file opens are reached by the hooks below.

**The `--wrap` hooks, on a linker without `--wrap`.**

- **The renaming**: upstream's definition is renamed by a `-D` on the one file that holds it
  (`src/hosts/desktop/Makefile:177-181`). `wraps_desktop.cpp` then gives the original name to the Pi's hook and the
  `__real_` name to upstream's code.
- **The hooks covered**: `EmulOp`, `extfs_write`, the two name encodings, and `Sys_open` and the shared folder's
  root, which go through `CardPath()`.

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
- [ ] Restart from Mac OS back to the menu, tried by hand (the path is the Pi's: `MacRestarted()`)
- [ ] Sound (`audio_circle.cpp`'s pump on an `SDL_AudioStream`), the chime
- [ ] Network (slirp or vmnet), and the network page's choices for a desktop
- [ ] PowerPC engine and the switch (milestone J3): two-level namespace or `llvm-objcopy`
- [ ] A flush policy for the disk images (`F_FULLFSYNC` or `F_BARRIERFSYNC` in the seam), measured
- [ ] Move the shared files to `src/common/` and the Circle glue to `src/hosts/circle/` (milestone J0, the files)
- [ ] Full screen, window resizing, the zone override; Linux build; a `.app` with the `allow-jit` entitlement
- [ ] ThreadSanitizer over the tick, input and picture threads

## Pitfalls

- **A shim cannot shadow a header included by quotes from beside it.** `#include "hal_circle.h"` in
  `src/circle/foo.cpp` finds `src/circle/hal_circle.h` before any `-I` directory. The desktop's
  `shims/okapia_circle.h` is force-included and includes the shim `hal_circle.h`. That shim defines the real
  header's guard too (`OKAPIA_HAL_CIRCLE_H`), so the real header becomes empty.
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

## References

- [Okapia on a desktop](../notes/research/desktop-host.md): the study, the decisions taken in its brainstorm, the
  milestones.
- [Boot menu](boot-menu.md), [Display](display.md), [Input](input.md), [Storage](storage.md).
- SDL3 (`/opt/homebrew/include/SDL3`, 3.4.20).
