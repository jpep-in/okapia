# Okapia on a desktop, and what it would keep

> Reading study, 2026-11-21. Question: could Okapia also be a desktop application (macOS first, then Linux and
> Windows)? It would be one optimised executable that opens **a folder laid out like an Okapia card**, shows **the same
> boot menu**, and behaves the same way: startup volume, engine chosen from the System, 68k ↔ PowerPC switch, chime,
> HFS repair, settings. It would replace and complete upstream's launchers and preference editors
> (`MacOSX/PrefsEditor.mm`, `Unix/prefs_editor_gtk.cpp`, BasiliskIIGUI). Which code stays, which is rewritten, and what
> becomes a build variant? **Read, not built, not run, not measured.** The line counts are `wc -l` of the tree
> at `004882e`. Every effort figure is an estimate, and is called one.

## Verdict

1. **The boot menu is kept whole.** `src/firmware/*.cpp` includes no Circle header: only `<string.h>` and
   `../circle/video_sizes_okapia.h` (`okapia_display.h:10`), which is pure itself. It draws into a `TSurface`
   (`okapia_gfx.h:44`) and takes `TEvent`s (`okapia_event.h:59`). `tests/host` already builds it with the host
   compiler and renders it pixel for pixel. **One file of glue** is what has to be rewritten:
   `src/firmware/circle/okapia_firmware.cpp` (2,030 lines, now `src/common/okapia_menu.cpp`). It mixes the menu's logic (`RunPage`, `RunScreens`, the page
   adapters, `Load*`/`Store*`) with access to the card and the board (FatFs `f_*`, `CTimer`, `CScheduler`, `BoardNet*`,
   `Wlan*`, `Clock*`, `FwInput*`, `FwOutput*`).
2. **About a third of `src/circle/` builds unchanged** behind a desktop `okapia_circle.h` shim (`CLogger`, `CTimer`,
   `CSpinLock`), as `tests/host/shims/` already does. Every hardware file has a desktop counterpart upstream, except the
   network clock and Wi-Fi, which a host makes unnecessary.
3. **The open question is the link, not the code.** Apple's `ld64` has no `--wrap`, and the `ppc__` rename is done
   today by `objcopy --redefine-syms` on ELF. That must be settled by a trial before anything else.
4. **What would give the most shared code is not SDL on Circle.** It is an **Okapia host interface**
   (`okapia_host.h`): narrow, in C, implemented once for Circle and once for the desktop. Everything above it becomes
   common to the Pi and the desktop. The desktop host also has a windowless **test mode** that would replace QEMU for
   most checks.

## Where it stands (2026-11-21, evening)

The study was followed by a build the same day; [Desktop](../../topics/desktop.md) is its page.

- **J0, in part.** The boot menu's driver is `src/common/okapia_menu.cpp`, behind `src/common/okapia_host.h`, and
  the Pi's answers are `src/hosts/circle/firmware/okapia_host_circle.cpp`. The Pi was checked unchanged: QEMU to the
  Finder, the menu opened by `bootmenu`, the host tests. Files have not moved to `src/common/` and
  `src/hosts/circle/` yet.
- **J1, done.** The menu runs on macOS from a card folder, windowed or headless, scripted. Its pages say *Computer*,
  *Card folder*, *Window* and *Option* there (`HostKind ()`).
- **J2, 68k done.** System 7.1.2 reaches the Finder in the process, through `src/kernel/kernel.cpp` and 21 files of
  `src/circle/` (now `src/common/`) compiled unchanged against a shim. A killed session is repaired at the next start.
- **The link question**, answered for one engine: per-file `-D` renaming plus `wraps_desktop.cpp` stands in for
  `--wrap`. The two-engine question waits for J3.
- **J4, in part.** `-O3` and the translator are built in on arm64; `jit true` reaches the Finder. Not measured.
- **Later the same evening**: sound (the chime checked), network (slirp), PowerPC (J3: Mac OS 8.6 to the Finder,
  the switch in one process, `ld -r -exported_symbol` in place of the rename), and the move. The shared files are now
  `src/common/*_okapia`: the planned names without a suffix meet upstream's files in the kernel's VPATH. The Circle
  glue is in `src/hosts/circle/`.
- **Not started**: packaging (J5).

The test mode needs no SDL driver after all: headless means no window, and the surface is written out as PNG
(Pitfalls of the Desktop page).

## What is kept, what is rewritten, what is a build variant

### Kept, built for the host

- **The boot menu**: all of `src/firmware/` (`okapia_gfx`, `okapia_theme`, `okapia_widgets`, `okapia_layout`,
  `okapia_screen`, `okapia_page`, `okapia_chooser`, `okapia_settings`, `okapia_network`, `okapia_display`,
  `okapia_info`, `okapia_journal`, `okapia_newvolume`, `okapia_confirm`, `okapia_clock`, `okapia_datetime`), with its
  generated tables (`gen-font.py`, `gen-icons.py`, `gen-strings.py` from `assets/strings.tsv`).
- **Portable platform logic**, in `src/circle/`:
  - display and QuickDraw: `compositor_okapia` (479 lines), `quickdraw_blit` (1,082), `quickdraw_okapia` (774),
    `video_sizes_okapia` (166);
  - card and disks: `hfs_volume_okapia` (677), `card_okapia` (435, less its use of volume dates to set the clock),
    `prefs_okapia` (599), `xpram_okapia` (165);
  - the rest: `rom_chime` (388), `net_config_okapia` (274), `mac_encoding_okapia` (257), `user_strings_okapia` (71),
    `emul_op_hook_okapia` (102), `sheepshaver/mac_layout` (74), and the trace files.

  The card paths are absolute (`/Okapia_Prefs`), so a `chdir` into the folder at start is enough.
- **The key table.** SDL scancodes *are* USB HID usages. That is why `s_HIDToMac` (`input_okapia.cpp:144`) is
  generated by `gen-keycodes.py` from the SDL2 section of Basilisk's own keycodes file. It serves the desktop as it
  stands, `BasiliskII.keycodes` included.
- **The macemu patches**, nearly all of them:
  - 0003: the 1-bit palette;
  - 0006: removable disks;
  - 0008 and 0013–0015: the 80-bit FPU;
  - 0009 and 0010: interpreter speed;
  - 0011: screen sizes;
  - 0012: the AArch64 translator, which already has an `__APPLE__` branch with `MAP_JIT` and
    `pthread_jit_write_protect_np`.

  0002 and 0004 become optional.

### Mixed: the logic kept, the glue rewritten

- **`okapia_firmware.cpp`** splits into a portable menu driver and the host interface below.
- **`main_okapia.cpp` (449) and `sheepshaver/main_okapia.cpp` (448)**: ROM loading and checking stay. Locks, interrupt
  flags and memory come from upstream's `Unix/main_unix.cpp`.
- **`video_shared_okapia.cpp` (531), `video_okapia.cpp` (280), `sheepshaver/video_okapia.cpp` (450)**: the mode list,
  its IDs, the palette and the converter choice stay. Monitors stores its choice by ID, so the list must be identical.
  Only the output (`FwOutputClaim`) becomes a buffer that the shell presents. **Okapia's compositor stays in place of
  `video_sdl3.cpp`**: it is what gives the same modes, `macmode`, `machidemode` and the same picture.
- **`cpu_ticks_okapia.cpp` (185) and `sheepshaver/cpu_ticks_okapia.cpp` (167)**: the 1 kHz seam stays (input drain, PRAM,
  performance report), paced by the host's monotonic clock.
- **`input_okapia.cpp` (922)**: the table, the acceleration curve and `mousedpi` stay. USB reception becomes the shell's
  events.

### Rewritten, or replaced by upstream

| Pi file | Desktop |
|---|---|
| `hal_circle` (606), `mac_ram_circle`, `execmem_circle` | `main_unix.cpp`'s start-up, `vm_acquire`, `MAP_JIT` |
| `audio_okapia` (463), `board_sound_circle` (492) | the shell's sound output (model: `SDL/audio_sdl3.cpp`); the chime goes through it |
| `tick_okapia` (317), `timer_okapia` (137) | a tick thread on an absolute schedule, `Unix/timer_unix.cpp` |
| `board_net_circle` (532), `ether_okapia` (481) | `Unix/ether_unix.cpp`: slirp (NAT, no privileges) or vmnet (bridged, through `MacOSX/etherhelpertool.c`) |
| `quickdraw_worker` (90) | a `std::thread` |
| `okapia_output`, `okapia_input` | the shell |
| `wlan_*`, `net_clock` (910), `clock_circle` (538), `net_bench_circle` | **dropped**: the host owns the network and the time |

### Build variants

- **The target**: `src/hosts/desktop/Makefile`, host clang, the kernel's source lists, `-MMD -MP` on every rule as on the
  Pi.
- **Optimised build**:
  - `-O3 -mcpu=apple-m1` (or `native`) and LTO. `-O2` is imposed by the Pi's Device memory only.
  - The `softfloat` FPU stays: same results as the Pi, and already exercised on the host.
  - The translator (`OKAPIA_JIT_ARM64`) runs a twenty-test benchmark ten times faster than the interpreter on macOS
    ([Decisions](../../project/decisions.md#no-jit-by-default-an-experimental-one-behind-okapia_jit1)). It stays
    behind the `jit` preference until the durability tests pass with it on the desktop.
- **Existing flags**: `OKAPIA_TRACE`, `OKAPIA_FPU` and `OKAPIA_JIT_ARM64` keep their meaning. `OKAPIA_NETBENCH` and
  `OKAPIA_QD_MULTICORE` have none on a desktop.
- **`scripts/build-native-basilisk.sh`** (a bare SDL Basilisk II for MacBench) would be superseded: the desktop binary
  measures Okapia itself.

## SDL on Circle: considered, not chosen

Circle has no SDL port, neither 2 nor 3: none was found in Circle or in SDL. Writing one would mean:

- threads over Circle's cooperative scheduler, which runs on core 0 only;
- sound to the PWM, HDMI and USB devices;
- video to `CBcmFrameBuffer`;
- USB keyboard and mouse.

That is thousands of lines, to share `video_sdl3.cpp` and `audio_sdl3.cpp`, which Okapia leaves out on purpose: the
compositor, fixed-ID modes, the audio pump and the chime are what make it identical across machines. The keyboard is
shared already, through HID. The Pi would also lose the microsecond-deadline tick and the rule "record in the
interrupt, act in the seam" ([Input](../../topics/input.md)), and would gain a layer in hot paths. The one argument for
it: other SDL emulators on the bare Pi (Mini vMac, on the [vision](../../project/vision.md)'s longer list). Not
costed here.

## The Okapia host interface

The boundary already exists in pieces:

- `Board*` (`hal_circle.h:158-255`);
- `FwOutput*` (`okapia_output.h:29-37`);
- `FwInput*` (`okapia_input.h:40-79`);
- `MacRamClaim` (`mac_ram_okapia.h:35`);
- `Clock*` (`clock_okapia.h`).

The work is to name it, complete it and route everything through it. A sketch, not a design:

| Group | Functions | Circle | Desktop |
|---|---|---|---|
| Log | `HostLog`, `HostLogFlush` | `CLogger`, `BoardLogFlush` | `okapia.log` and stderr |
| Time | `HostMicros`, `HostSleep`, `HostFineTick(period, handler)` | `CTimer`, `FwSleep`, `BoardFineTick` | monotonic clock, a tick thread on an absolute schedule |
| Wall clock | `HostWallClock`, `HostClockSource` | the `Clock*` chain (NTP, `/Okapia_Clock`, volume dates) | the host's time and zone |
| Memory | `HostMacRam`, `HostExecArena` | `MacRamClaim`, `execmem` | `vm_acquire`, `MAP_JIT` |
| Picture | `HostOutputClaim`, `HostOutputPresent(rect)`, `HostOutputDescribe` | `CBcmFrameBuffer`; presenting is nothing, the hardware scans it out; EDID | a buffer and a texture; presenting copies the dirty rectangle on the main thread |
| Input | `HostPostKey(HID usage, down)`, `HostPostMouse` — called *by* the host | USB handlers | SDL events |
| Sound | `HostSoundOpen`, `HostSoundWrite`, `HostSoundQueued` | `BoardSoundClaim`, `CSoundBaseDevice::Write` | `SDL_AudioStream`, push model like today's pump |
| Card | `HostCardFree`, `HostFileReserve`, `HostFileSync` | FatFs `f_getfree`, `f_expand` | `statfs`, `F_PREALLOCATE`, `F_FULLFSYNC`, `flock` |
| Network | `HostNetDescribe(TNetwork *)`, `HostNetApply`, frames | `BoardNet*`, `Wlan*` | slirp or vmnet (`ether_unix`) |
| Capabilities | `THostCaps`: sound outputs, network kinds, JIT, `config.txt`, multicore QuickDraw | the board | the host |

- **File access is POSIX on both sides already.** newlib over FatFs (`STDLIB_SUPPORT=3`) serves `Unix/sys_unix.cpp` and
  `Unix/extfs_unix.cpp` unchanged on the Pi. Only `okapia_firmware.cpp` calls `f_*` directly.
- **No POSIX type crosses the interface** (`int fd`, `struct stat`, `off_t`), so that Windows is a host variant, not a
  rewrite.
- **Above the interface, common to both hosts:**
  - the menu driver;
  - input: the ring, the acceleration and the HID table;
  - sound: the audio pump and the chime's mixing;
  - display: `video_shared` and the compositor;
  - time: the tick's logic (60 Hz derived from the fine tick) and the `cpu_ticks` seams;
  - storage: `card`, `hfs_volume`, `prefs`, `xpram`;
  - `okapia_boot`'s loop and the kernels' `Run()` loops.
- **Below, Circle only:**
  - the board: `hal_circle`, `execmem`, `mac_ram`, the tick's IRQ;
  - devices: the device half of `board_sound`, `okapia_output`, the USB half of `okapia_input`;
  - network and clock: `board_net`, `wlan_*`, `clock_circle`, `net_clock`;
  - `quickdraw_worker`.
- **Layout:**
  - `src/common/` holds the shared code without the `_circle` suffix, and `okapia_host.h`;
  - `src/hosts/circle/` holds what remains of `src/circle/` and `src/hosts/circle/firmware/`;
  - `src/hosts/desktop/` holds the desktop host and its test mode;
  - `src/firmware/` stays the UI toolkit, and its menu driver moves to `src/common/`.

  "core" was ruled out as a name: it already means the emulation cores, the Pi's CPU cores and `tests/core-probe`.
  `src/common/` is also not the [architecture](../../project/architecture.md#what-exists-once-and-what-exists-twice)'s
  "shared half" (`obj-shared`, built once), which is another cut.
- **The picture between threads.** The compositor writes one output buffer, as on the Pi, and accumulates the dirty
  rectangle under a short lock. The main thread takes it and copies it into the texture on each refresh. This is the
  same semantics as the Pi's hardware scan-out: tearing is possible but rare.

### What the Pi gets from it

- **S1.** The desktop forces what [S1](../../project/architecture.md#multicore) asks for. Emulation runs on its own
  thread, input is recorded by another and the picture is presented by a third, so shared state must be correct across
  threads: atomics where the IRQ made do with `volatile` (8 in `input_okapia.cpp`, 1 in `tick_okapia.cpp`).
  ThreadSanitizer is free on a desktop and unavailable on the Pi.
- **A test mode.** It is the desktop binary run without a window, on the model of the
  [Mamba](https://github.com/jpep-in/mamba) project's `snow-runner`, which drives `snow_core` headless.

## The test mode

"Headless" means no window, not no screen. The Mac's screen exists in memory, and the test mode writes it out as PNG,
as `render_pages` already does for the menu. The boot is still judged by the screen, as `AGENTS.md` requires, but
without QEMU, which has no sound, no RTC, and costs a third of the guest's speed
([Testing](../../contributing/testing-and-debugging.md)). As built, headless means no window at all: SDL's `offscreen` driver
wanted OpenGL, and the surface the menu and the compositor draw into is the picture. Sound, once it exists, could
go to SDL's `disk` audio driver (not tried).

| snow-runner | Okapia test mode | What it rests on |
|---|---|---|
| `--hdd`, copied into `--work-dir` | the whole card folder, copied into the work folder | the rule of `run-test.sh` and `screenshot.sh` (copies) |
| `--datetime` | a fixed time through `HostWallClock` | the host interface |
| `--seconds`, `--cycles` | wall time, or emulated time (`Ticks`) | the tick |
| `--screenshot`, `--screenshot-at MS` | PNG of the Mac's screen and of the composed output | the Mac's frame buffer, the compositor |
| `--click`, `--double-click`, `--drag`, `--keys MS,…` | the same, in emulated time, through `HostPostKey` and `HostPostMouse`; the mouse in ADB absolute mode to aim at X,Y | the input ring |
| — (Snow has no boot menu) | the same in the boot menu: Option held, navigation, choice of volume, in host time | `TEvent`, `ScreenEvent` |
| `--until text` (modem port) | the Mac's serial port redirected to a file, or a file in the shared folder (MacBench already writes `MacBench Results` there) | `seriala`, `extfs` |
| `--traps` | trap history | `trace_traps_circle.cpp` (`OKAPIA_TRACE`) |
| `--dump address,length` | dump of Mac RAM at the end | the single RAM block |
| `--break`, `--watch` | later: needs a hook in the interpreter | the trace build |
| `--pcap`, NAT | slirp with a capture | `ether_unix` |
| exit codes 0/2/3/1 | the same | — |
| — | `--kill-at MS`, then repair and verdict | `run-test.sh` |
| — | sound written as WAV: the chime and the beep, checked | SDL3's `disk` audio driver |

- **Determinism is the real difference.** Snow is cycle-exact, so a run replays identically. Okapia paces its tick and
  seams by the real clock, on purpose ([Architecture](../../project/architecture.md#timing)). Because time would go
  through `HostMicros`, the test mode can inject a **virtual clock** advanced by emulated instructions, so that a click
  "at 3,200 ms" lands at the same instant every time. Sound and network stay outside that determinism.
- **What it would replace:**
  - `screenshot.sh` and `run-test.sh`, run natively, in seconds;
  - `tools/macbench/run.py` would aim at this binary instead of a bare Basilisk;
  - the agents' trials, which move from QEMU to native.
- **What it cannot cover:** what lies below the interface on the Circle side (USB, HDMI, SD, Wi-Fi). The Pi stays the
  judge.

## The card folder

- **Same layout as the card**, so a mounted card and a copy of one open alike:
  - `Okapia_Prefs` and the ROMs;
  - the images (`.image .img .hda .dsk .hfv .iso .toast`);
  - `BasiliskII_XPRAM` and `SheepShaver_XPRAM`;
  - `shared/`, `okapia.log` and `BasiliskII.keycodes`.

  The Pi's own files (`kernel8*.img`, `config.txt`, the Raspberry Pi firmware) are ignored. `/Okapia_Clock` becomes
  unnecessary. The installer already prepares "a folder standing in for a card" (`okapia-installer`,
  `core/src/card.rs:42`).
- **Which folder opens**, first match wins:
  1. one passed as an argument or dropped on the icon;
  2. the folder holding the application (portable mode);
  3. the last one opened;
  4. otherwise, a picker.

## Data safety on a host

The rules of [Storage](../../topics/storage.md) stay, but the threat changes:

- **A `kill -9` of the process loses nothing**: the OS page cache survives it. The risk becomes a host power cut.
- **`fsync` does not reach the medium on macOS.** An explicit policy is owed (`F_FULLFSYNC`, or `F_BARRIERFSYNC` per
  batch in the seam) in `Sys_write` and the shared-folder sync, measured for its cost. Data wins.
- **The durability test**: SIGKILL, libhfs repair and verdict, on a copy of the folder.
- **Never two emulators on one card**: a `flock` on the folder.
- **Closing the window is not pulling the plug**: it sends the Power key (the Mac's Shut Down dialog) and asks for
  confirmation.

## The boot menu on a desktop

The pages already size what they offer from **capability masks**:

- `nSoundAvailable` and `bJitAvailable` (`okapia_settings.h`);
- `nAvailable` (`okapia_network.h:80`);
- `bConfigAvailable` and `bHdmiDirectives` (`okapia_display.h`).

The desktop passes other values instead of `#ifdef`s, in the spirit of "discover, never assume"
([Vision](../../project/vision.md#principles)).

| Page | Desktop |
|---|---|
| System (startup volume, CD, engine, new volume) | the same; free space from `statfs` |
| Settings | the same (memory, mouse, language, boot menu at every start, `jit`, `qdaccel`, shared folder); sound outputs become "default output" or off; Date & Time defaults to the host's zone, NTP hidden |
| Network | new content: NAT (slirp), bridged (vmnet), off; no Wi-Fi search; new `TNetworkKind`s |
| Display | the same modes and IDs; "output, EDID, `config.txt`" becomes window or full screen |
| Information | the host's model and OS (`sysctl hw.model`) instead of the board |
| Logs, alerts | the same |

- **To complete what BasiliskIIGUI offers**, three things are missing:
  - choosing the ROM: the folder's ROMs, identified as `check-rom.py` does, with a *Listen* button for the chime as in
    the installer;
  - window or full screen;
  - recent folders.

  The rest (disks, CD, memory, shared folder, JIT) is already in the menu.
- **When the menu opens**: the same rule (Option held during the grey window, the `bootmenu` preference, nothing
  bootable), plus a menu command "Boot menu at next restart".

## Window, full screen, resolution, Retina

The Pi's principle carries over as it is: a fixed output and a compositor
([Display](../../topics/display.md)). On a desktop the output is the window's drawable in real pixels, or the whole
screen in full screen.

- **Windowed and full screen.**
  - **Full screen is SDL3's "desktop" kind**, with no display mode change: no flicker, and Mission Control and
    Cmd-Tab keep working.
  - **The Mac is centred** at the largest integer scale that fits, with bars around it, exactly as on the Pi on a TV.
  - **Toggled** with ⌘↩ or from the menu.
- **The output changes size.** On the Pi it is claimed once for the life of the board. On a desktop it changes with
  the window: `HostOutputDescribe` reports it and the compositor re-plans (`CompositorPlan`).
- **Who magnifies.**
  - **On the Pi**, the compositor does, on the CPU. On a 5K display a full frame would be 5120 × 2880 × 4 ≈ 59 MB,
    copied and then uploaded.
  - **On the desktop**, the compositor keeps the conversion (depths, palette → 32 bits) at the Mac's size, and the GPU
    magnifies by an integer factor with nearest-neighbour sampling (`SDL_LOGICAL_PRESENTATION_INTEGER_SCALE`). The
    picture is the Pi's, and the upload falls to about 3 MB at 1024 × 768.
  - **The menu** is drawn at the output's size, as on the Pi. So there are two textures, the menu and the Mac.
- **The Mac's resolution.**
  - **The same 13 sizes and the same IDs**, with `macmode` and `machidemode`.
  - **"The display's size" becomes the host screen's size in points.** On a 14-inch MacBook Pro, the Mac gets a desktop
    that fills the screen at 2×.
  - **Windowed**, the window follows the Mac: its mode times an integer scale. A user resize settles on the nearest
    integer scale, with bars in between.
  - **What does not change**: Mac OS does not change resolution because the window is dragged. It chooses, in
    Monitors, as on the Pi.
- **Retina.**
  - **The Mac**: one Mac pixel covers 2 × 2 physical pixels at scale 1 (in points), nearest-neighbour, sharp.
  - **The menu** cannot simply be drawn in physical pixels. `ThemeScaleFor` caps the scale at 4×
    (`okapia_theme.cpp:764`), and the largest face is Helvetica 24 pt at 100 dpi (`assets/fonts/helvR24-100.bdf`). On a
    5K display the text would be too small for the rest.
  - **The Pi has the same problem** on a 2160p TV.
  - **Preferred answer, for both**: draw the menu at the size in points (on the Pi, at half the output), then double
    it with nearest-neighbour sampling. The picture is exactly the Pi's, and there is nothing new to write.
  - **Set aside**: an outline font on both sides, as the installer's Liberation Sans. Finer text, but a new renderer to
    validate everywhere.

## Linking two engines on Mach-O

- **Two engines in one process.** Two candidates, for a trial to decide between:
  - **(a) Each engine as a dylib.** macOS's two-level namespace keeps the two `InitAll`, `PatchROM`… apart without a
    rename. The shared menu binds `PrefsFindString` to the 68k engine, as it does on the Pi today.
  - **(b) `llvm-objcopy --redefine-syms`** (Homebrew's LLVM): the current chain unchanged, if its Mach-O support allows
    it. Not checked.

  The fallback is two processes, where the switch becomes an `exec`: simpler, at the cost of a break at every change
  of engine.
- **`--wrap`.** The ordinary build has seven hooks (`src/kernel/Makefile:427-431`): `extfs_write`, `EmulOp`,
  `host_encoding_to_macroman`, `macroman_to_host_encoding`, `InitStreamModule`, `TerminateStreamModule` and
  `ether_open`. The trace build adds its own (`Makefile:438-440`). The candidates are a per-object symbol rename (same
  tool as (b)) or a `-D` per compilation unit. Neither touches `external/`.
- **Restarts and switches.** `okapia_boot.cpp`'s loop (`OkapiaRun68k`, `OkapiaRunPowerPC`, the Switch, Reboot and Halt
  exits) carries over unchanged. It already proves each engine can be entered more than once.

## Linux and Windows

- **Linux is nearly free.** It is POSIX and SDL3, like macOS. `F_FULLFSYNC` and `F_PREALLOCATE` become `fdatasync` and
  `fallocate`. The AArch64 translator serves Linux on ARM too, Raspberry Pi OS included.
- **Windows is feasible, with a file host of its own.**
  - **What does not change**: SDL3 covers the window, sound and input. Upstream's `vm_alloc.cpp` already has a
    `VirtualAlloc` branch. `std::thread` covers the threads.
  - **What changes**: the file layer (`FlushFileBuffers`, `LockFileEx`, `GetDiskFreeSpaceEx`,
    `SetFileInformationByHandle`). Upstream has `Windows/sys_windows.cpp` and `Windows/ether_windows.cpp` (slirp or
    TAP). The tool chain is clang or MinGW-w64.
- **The shared folder carries across.** `Windows/extfs_windows.cpp` keeps Finder info and forks in the same
  `.finf/` and `.rsrc/` subfolders as `Unix/extfs_unix.cpp` (`extfs_windows.cpp:86-88`, `extfs_unix.cpp:79-81`), so a
  card folder moved between the Pi, macOS and Windows keeps types, creators and forks. Name encoding across the three
  is not checked.
- **The Pi's card is FAT32**, with Windows' forbidden characters and case-insensitivity. The card folder is more at
  home there than on APFS.
- **JIT on x86_64.** Okapia's translator targets AArch64 only. `uae_cpu_2021` carries upstream's x86 JIT
  (`uae_cpu_2021/compiler/codegen_x86.cpp`); whether it works with Okapia's softfloat FPU is not known. Windows and
  Linux on ARM keep the translator.
- **Order**: macOS, then Linux, then Windows.

## The shell, and the door to other platforms

The core would sit behind a C ABI, so that any shell can drive it. A sketch:

- `okapia_open(folder, host)`;
- `okapia_run()`, on the emulation thread;
- `okapia_post_event()`, with keys as HID usages, and the mouse;
- `okapia_frame()`: the latest buffer and its dirty rectangle;
- `okapia_audio_pull()`;
- `okapia_request(power | shutdown)`.

Host callbacks go the other way, for the log, the title and wake-ups.

| Shell | For | Against |
|---|---|---|
| **SDL3, C++ (first)** | scancodes are HID, so the table serves unchanged; native sound, HiDPI and folder picker; macOS, Linux, Windows | a bundled dependency |
| Rust: winit, pixels or wgpu, cpal | the installer's language; in time, one desktop Okapia that prepares a card *or* runs it here | a `KeyCode` → HID table to write; FFI; sound on a Rust thread |
| egui | the surroundings (recent folders, errors), in the installer | not the menu itself, which the firmware draws |

The installer would gain a *Start here* button that opens the prepared folder in this shell. Merging later into one
Rust application stays open because of the C ABI.

## Risks and size

These are estimates, not measurements. Okapia's own code is about 33,400 lines (`src/circle`, `src/firmware`,
`src/kernel`). The variant would keep or move about four fifths of it and write about 3,000 new lines. It is mostly
re-adaptation; the new work gathers in three risky places.

| # | Risk | Kind | Weight | Answer |
|---|---|---|---|---|
| 1 | The Pi regresses while the base moves (J0) | wide re-adaptation | high: the Pi is the product | move without changing code, then extract; `screenshot.sh`, `run-test.sh`, host tests and a real Pi 4 at each step |
| 2 | Linking on Mach-O: no `--wrap`, the rename untried | technical unknown | medium | a trial first; two processes as the fallback |
| 3 | Threads: the code assumes one core and an IRQ | new work, subtle bugs | medium-high | audit shared state, atomics, ThreadSanitizer; the same work as S1 |
| 4 | SheepShaver on arm64 macOS: a 4 GB `__PAGEZERO`, and a `mac_layout` written for the Pi | port | medium | patch 0001 lets the host place the guest; upstream already runs there |
| 5 | The cost of `F_FULLFSYNC` on the Mac's writes | speed against data | medium | a policy in the seam, measured; data wins |
| 6 | About 160 path references in the docs when files move | mechanical | low | a script, in the same change |
| 7 | Two hosts to maintain | ongoing | medium | the test mode in CI; capabilities rather than `#ifdef` |
| 8 | Distribution: Apple notarisation for anyone but oneself; ad-hoc signing and `allow-jit` are enough locally | administrative | low | decided only when distributing |

**Simple parts**:

- the `CLogger`/`CTimer` shim (`tests/host/shims/`);
- the POSIX file host;
- the SDL3 shell: the HID table reused, sound pushed like today's pump;
- the JIT on macOS: patch 0012's `__APPLE__` branch, already measured;
- Basilisk with `DIRECT_ADDRESSING`, which is how upstream runs.

## Decisions a desktop variant would revisit

- **256 MB**: kept (the Quadra 950 profile), adjustable.
- **No JIT by default**: unchanged. The [criterion](../../project/decisions.md) can be measured on the desktop too.
- **`DIRECT_ADDRESSING`**: feasible through `MEMBaseDiff`.
- **The fixed output and compositor**: kept.
- **"One image, two Macintosh"**: the same idea, through another linking mechanism.
- **Unchanged**:
  - no write cache without a flush policy;
  - no volume marked clean by Okapia;
  - preferences in upstream's format;
  - two XPRAM files.

## Milestones (proposed, not started)

- **J0.** Move the shared code to `src/common/` and the Circle glue to `src/hosts/circle/`, with no code change and the
  docs' references updated in the same change. Then extract `okapia_host.h`, including the menu driver taken out of
  `okapia_firmware.cpp`, and put the Circle host behind it. The Pi must stay identical: same boot, same screenshots.
- **J1.** The SDL3 shell and the POSIX host, with the test mode: open a folder, show the real menu, save the
  preferences. This already replaces BasiliskIIGUI for editing.
- **J2.** The Mach-O linking trial, then the 68k engine in-process: System 7.1's Finder, and back to the menu on
  Restart. Native durability tests from here.
- **J3.** PowerPC, and the switch between engines.
- **J4.** The optimised build, the translator, MacBench measurements (`tools/macbench/run.py`).
- **J5.** A `.app` signed ad hoc with `com.apple.security.cs.allow-jit`, SDL3 bundled, folder association, and the
  installer's button.

Rough sizes, as estimates:

- J0 moves about 15,000 lines unchanged and extracts about 1,400 lines of menu driver;
- J1 is about 800 new lines;
- J2 and J3 together are about 2,500 lines of desktop platform code.
