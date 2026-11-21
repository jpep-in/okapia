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
- **Network**: *Shared with this computer (NAT)* is the page's wired kind (`ether eth`). The Macintosh then gets
  10.0.2.15 by DHCP behind the host, through upstream's slirp, with no privilege asked. There is no Wi-Fi to join.
- **Sound**: every output the page offers is the host's default output; *Off* is silent. The chime plays from the
  ROM the startup volume will use, as on the Pi.

**The Macintosh.** The startup volume's System starts as on the Pi, in the window, through the same compositor (an
integer scale, centred).

- **Mouse**: a click in the window gives the pointer to the Macintosh; Control-Command gives it back.
- **Keyboard**: goes to the Macintosh as a USB keyboard's reports, through the Pi's table (`BasiliskII.keycodes`
  included).
- **Closing the window while it runs** asks first: quitting then is pulling the plug, and the volumes in use are
  repaired at the next start. Shut down from the Special menu instead.
- **A volume asking for the PowerPC engine** is started by it, in the same process, as on the Pi (Mac OS 8.6 from
  `macosrom16.rom`, tried).
- **Restart from Mac OS 8–9** starts the process again on the same folder, where the board resets itself:
  SheepShaver cannot be entered twice in place. Shut Down ends it.

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

Exit codes: 0 the run ended as asked, 1 an error, 3 Shut Down, 4 quit while the Macintosh ran.

## How it works

**Two threads.**

- **The main thread** owns SDL. It takes the window's events, runs the script's steps, and presents the surface at
  the display's pace (`DesktopMainLoop`, `okapia_host_desktop.cpp`).
- **The emulation thread** runs what the board runs, `okapia_boot.cpp`'s loop: `OkapiaRun68k()`, then
  `OkapiaRunPowerPC()` when the startup volume asks for it, so `CKernel` and `CKernelPPC` from `src/kernel/`,
  unchanged. The boot menu and the Macintosh are on this thread.

**The picture.** The menu and the Mac's compositor draw into one surface, the window's size in points,
`0x00RRGGBB`, as on the Pi's output. The main thread copies it into a texture that the GPU doubles to Retina pixels
by nearest neighbour.

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
- [ ] Restart from Mac OS back to the menu, tried by hand (68k: `MacRestarted()`; PowerPC: `reboot()`)
- [ ] The Macintosh's own sound heard (only the chime was checked), and traffic through slirp from a Mac OS with TCP/IP
- [ ] A flush policy for the disk images (`F_FULLFSYNC` or `F_BARRIERFSYNC` in the seam), measured
- [ ] Full screen, window resizing, the zone override; Linux build; a `.app` with the `allow-jit` entitlement
- [ ] ThreadSanitizer over the tick, input and picture threads

## Pitfalls

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
