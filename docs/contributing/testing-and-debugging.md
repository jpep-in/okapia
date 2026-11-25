<!-- Copyright (C) 2026  Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Testing and debugging

> How to check a change: host tests, QEMU scripts, the durability test, logs on the card, traces and GDB. Main
> files: `tests/host/`, `scripts/run-live.sh`, `run-test.sh`, `screenshot.sh`, `specimen.sh`, `run-cd.sh`,
> `check-large-volume.sh`, `run-qemu.sh`, `src/hosts/circle/trace_*.cpp`.

## Using it

| Question | Command |
|---|---|
| Does the logic still hold, with no board? | `make -C tests/host` |
| Does the desktop Macintosh boot? | native headless run on a copied workspace; see [Desktop](../topics/desktop.md#using-it) |
| Does the public site build and link correctly? | `cd site`, then `npm test`, `npm run build`, `npm run check-links` |
| Does the Mac boot, and what is on its screen? | `./scripts/screenshot.sh [seconds] [width height]` |
| Does a session damage the volume? | `./scripts/run-test.sh [seconds] [image]` |
| Is an image read past 2 GiB? | `./scripts/check-large-volume.sh [seconds]` |
| I want to use it | `./scripts/run-live.sh [seconds]` |
| How does the boot menu look and feel? | `./scripts/specimen.sh [page]`, `./scripts/specimen.sh live` |
| Install Mac OS 8.6 from its CD | `./scripts/run-cd.sh` |
| A bare kernel, GDB | `./scripts/run-qemu.sh [kernel] -s -S` |

**Test against `qemu/sd-contents/boot71.img`**, not the 500 MB System 7.6 volume: 7.1 reaches the Finder in about
three seconds under QEMU where 7.6.1 takes much longer, and `run-test.sh 40 qemu/sd-contents/boot71.img` builds its own
card in a second. A loop that costs minutes per round does not get run.

## How it works

### Host tests

`tests/host/` compiles platform and firmware sources for the Mac running the build, against shims of Circle's headers
(`tests/host/shims/`). `make -C tests/host` renders the firmware specimen (`specimen.ppm`) and runs every check; the
measurements are part of the build.

| Check | What it decides |
|---|---|
| `check_geometry` | every firmware page measured **in every language** at every scale: alignment, clipping, spacing |
| `check_screen` | the firmware's screen primitives |
| `check_chooser`, `check_pages` | behaviour under synthetic key and mouse sequences: focus, lists, default buttons |
| `check_platform` | unknown preference lines kept, System flavour, the PowerPC memory layout, the resolution lists, the ROM chime (fabricated ROMs, plus the local ROMs when present), compositor output writes at odd origins and pitches |
| `check_blit` | upstream's pixel expanders at every depth |
| `check_encoding` | MacRoman ↔ UTF-8 file names |
| `check_rom`, `check_card` | ROM identification/compatibility and Pi card diagnosis, without installing onto a real card |
| `check_install` | release JSON, SHA-256, archive checks and installation across releases into temporary folders; a real removable-card test remains separate |
| `check_fpu_core` | `fpu_softfloat.cpp` itself, built for the host with the few symbols of the 68k core it reaches: a million random FPU instructions (every operation and operand format, packed decimal, `FMOVECR`, every FPCR mode and precision, zeros, infinities, NaNs, denormals, unnormals), the FPU state hashed after each and the hash pinned; each instruction under a five-second watchdog; the `FSIN` of an unnormal that once never returned |
| `check_fpu` | the 68K softfloat under `fpu_softfloat.cpp`: sixteen answers computed independently (FPCR precision, directed rounding to integers, ties, overflow, exponents a double cannot hold), the fast paths against softfloat over six million operations, and the digest of 60,000 operations that the traced kernel must reproduce on the board. `./check_fpu digest` prints it, to pin a deliberate change |

The firmware's screens are pure (handed values, answering values; everything knowing about files or frame buffers
stays behind `okapia_host.h` in each host), which is what makes them measurable here. A fixed reproducible bug comes with a test,
on the host where possible.

`tests/smoke`, `core-probe`, `link-probe` and `trace-build` are historical probes from the port's first days (a bare
Circle kernel, a compile probe of the core, the list of undefined symbols).

### Native test runs

The desktop build runs the boot menu and either Macintosh engine without a window, on a copy of a workspace, with
scripted keys and clicks and PNG screenshots ([Desktop](../topics/desktop.md#using-it)). It covers everything
above `okapia_host.h` in seconds, and a `SIGKILL` of the process followed by a second run checks the repair of the
volumes left in use. It is **not** a durability check: the host's page cache outlives the process, so nothing written
is ever lost to a `SIGKILL`. What the desktop owes against a power cut is its flush policy
([Desktop](../topics/desktop.md#status)), which only a host that loses its power can test. It says nothing of what lies below the interface on the board —
USB, HDMI, the card, Wi-Fi — which QEMU and the Pi still judge.

The 68k idle message is a useful milestone, but inspect the screenshot. PowerPC has no equivalent idle patch, so
its screen is the boot verdict. The application's `--work-dir` test mode keeps the original workspace untouched;
never point two running processes at one writable workspace or card. Current desktop audio, network and restart
validation gaps are recorded in [Desktop status](../topics/desktop.md#status).

### Website checks

The website has a separate Node.js build and manifest tests in `site/`. Its Markdown guides are imported from
`docs/using/`, so documentation edits to those pages require a site build and link check. For changes to the
Mac/Pi selector or animation, check both routes in a browser, mobile layout, full laptop-screen coverage, and the
Finder information row's text above its bottom rule. Reduced-motion visitors should see a still Finder.
Click the illustration to pause, click again to resume, and click after completion to restart. The same control
must work from the keyboard, without a visible Pause/Replay button or legend.
Check the floating Mac/Pi selector on the homepage and a startup guide. It should preserve the section,
show the route's host on direct links, and remain visible on mobile without covering text. It must be absent
from the header and shared Help/Advanced/Architecture/Downloads pages. There is no sidebar, breadcrumb or second
selector beside the animation. The legacy `/help/mac/` and `/help/pi/` routes must reach common Help. Check that
welcome-page and Retina advice is in Get started Mac, card checks in Get started Pi, and boot-menu/log advice in Help.

The shared extension requirements must appear in both startup guides and Help, as short Sound and Shared Folder lists with brief installation advice. Check About's author signature, Making Of's fourth Advanced tab and horizontally contained
credit tables, plus Changelog's empty state. History tests cover draft filtering, date ordering, pre-release labels
and safe Markdown notes; real publication remains the source of changelog entries.

The menu preview is regenerated from current firmware by `npm run menu-preview` (also part of `dev` and `build`).
Check all six generated PNG pages and their highlighted tab hit zones. Select Network and Logs, verify the
matching picture and pressed state, then verify automatic advancement resumes after twelve idle seconds.
Keyboard focus and reduced motion must hold the selected page. Check responsive alignment on mobile.
Its data is fictional and it never opens a workspace or disk image.

`.github/workflows/website.yml` checks this site without ROMs or emulator builds; only `external/stb` is fetched
for the real boot-menu renderer. It is not CI for the full C++ host tests,
guest boot or durability; those remain separate work. See [Website](website.md).

### QEMU scripts

Every script checks the tree is built for QEMU ([Build](build.md)); a Pi kernel under QEMU goes quiet.

- **`run-live.sh`** — `raspi3b` in a cocoa window at 1280×960 (twice 640×480, an integer scale; don't add
  zoom-to-fit), serial to a log, `cache=writethrough`, `-semihosting`, and a monitor socket
  (`OKAPIA_MONITOR`, default `/tmp/okapia-monitor.sock`). It is the only script that uses the master card
  `qemu/sd.img`, so it **refuses to start when the image is already open** (`lsof`). End with Special → Shut Down.
- **`screenshot.sh`** — boots a throwaway copy headless and captures the Mac's screen **a moment after the Mac
  logs `Macintosh idle`**, the seconds being a deadline for a System with no idle patch. Only the screen can tell a
  booted Finder from a stalled Mac.
- **`run-test.sh`** — the durability verdict, described in [Storage](../topics/storage.md#proving-it-run-testsh).
- **`check-large-volume.sh`** — a 3 GB volume with its System file past 2 GiB on a throwaway card; OK when the kernel
  reads that file's version ([Storage](../topics/storage.md#volumes-past-2-gib)). About 7 GB of scratch space for a
  few seconds.
- **`specimen.sh`** — a kernel with Circle and the firmware's drawing code only: no card, no emulator, always safe
  alongside anything. Pages are reached with arrow keys through the monitor; `live` opens a window driven by hand
  (Tab/Shift-Tab focus, Space operates, Return the default button, Left/Right turn the page). It links the real
  `hal_circle.cpp`, which is the point — it proves the firmware runs on a board and not on a copy of itself — and
  the price is that **`hal_circle.cpp` may name no emulator header**: no `prefs.h`, no `sysdeps.h`, nothing from
  `src/hosts/circle/firmware/` it does not compile. Anything on the board that needs a Macintosh behind it goes in
  `board_sound_circle.cpp`, which only the Macintosh kernel links. Adding it to `hal_circle.cpp` does not break
  `make -C src/kernel`; it breaks this script, and nothing else says so.
- **`run-cd.sh`** — its own card, `qemu/sd-cd.img`, with the install disc, the PowerMac ROM and no volume; the target is
  made from the boot menu and the disc starts through `bootdriver -62`.
- **`make-sd-image.sh`** — rebuilds `qemu/sd.img` from `qemu/sd-contents/`, sized to what is staged.

### QEMU network

`OKAPIA_QEMU_NET=1` gives `run-qemu.sh`, `run-live.sh` and `screenshot.sh` a USB Ethernet adapter (`usb-net`, which
Circle drives as `CUSBCDCEthernetDevice`) on QEMU's user network; the card must say `ether eth`.
`OKAPIA_QEMU_PCAP=<file>` records every frame (`tcpdump -e -n -r <file>`). Stock QEMU **sends** the Mac's frames but
never delivers one to it ([Network](../topics/network.md#pitfalls)), so the receive path is checked by the trace
build's `NET_SELFTEST`: once EtherTalk has probed its address, an AARP request for it is put in the board's ring and
the Mac's reply is watched for on the way out — `Network self-test: the Macintosh answered an AARP request` in the
log. It needs a System with EtherTalk active (the Network control panel set to EtherTalk).

### Driving the Mac through the QEMU monitor

`mouse_move dx dy`, `mouse_button 1|0`, `sendkey`, `screendump /tmp/x.ppm` — how the Finder is tested with nobody at
the screen:

```bash
echo "screendump /tmp/x.ppm" | nc -U /tmp/okapia-monitor.sock
```

- Send **one command per connection**, with a pause; a burst down one connection is dropped.
- Two `mouse_button` pairs do **not** make a double-click (the round trip is slower than the Mac's double-click time):
  select, then `sendkey meta_l-o` (Command-O).
- The Mac accelerates the pointer: a delta of 40 moves about 29 pixels. Aim by screenshot, not by arithmetic.
- System 7 menus are **not** sticky: press, move, release.
- A long socket path fails (Unix socket paths are short); use a short `mktemp` path when scripting.

### Logs

- **Serial** first, always: the kernel initialises the serial port and logger before anything else, because a failure
  before the log exists is indistinguishable from a hang. On a board it is shut once the preferences are read
  (`serial false`, the default; `BoardSerialLog`, `hal_circle.cpp`): the logger's target is dropped and the lines
  keep reaching its ring and the card. A panic or the halt path opens it again and replays the ring
  (`BoardSerialReplay`), so a stopped board is still heard on the cable. Under QEMU it never shuts.
- **On the card**: `okapia.log`, a 512 KB file preallocated and written in whole sectors, wrapping with a marker line
  when full; the previous session is kept as `okapia-previous.log`. Flushed from the engines' seams every few seconds
  and before halt or reboot, never from an interrupt. On a Pi, read the card on another computer.
- **The boot menu's Logs page** shows the end of `okapia.log` (or the previous session's) without taking the card
  out, and holds the *Serial output* and *Performance counters* boxes.
- **`perfreport true`** prints engine, video, input, sound and clock counters every five seconds (always on under QEMU).
  A polled PL011 character costs about 87 µs, which is why it is off on hardware, and why, on the wire, it wants
  `serial true` as well: with the serial shut the lines go to the card and the Logs at no cost to the Macintosh.
- **Signals to look for**: `Boot volume:`, `Macintosh idle: it has finished starting (N ms)`, `Clock: … from …`,
  `Sound device … claimed`, `Chime: …`, `VBL spacing` histogram, `Display … Monitor …`.

### Traces: hook upstream with the linker

`--wrap=<mangled symbol>` hooks a Basilisk or SheepShaver function without touching `external/`. Under
`OKAPIA_TRACE=1`:

| File | Wraps | Tells |
|---|---|---|
| `trace_disk_circle.cpp` | `Sys_read`, `Sys_write` | every block, short reads and writes |
| `exception_trace.cpp` | `Exception` | 68k exceptions |
| `trace_traps_circle.cpp` | A-line traps (`op_illg_1`) | Toolbox calls, `MountVol` results, `Control`/`Status` csCodes |
| `trace_fpu_circle.cpp` | `fpuop_arithmetic`, `fpuop_bcc`, `fpuop_dbcc`, `fpuop_scc` | FPU instructions executed; every 10 s with FPU use, the share of wall time inside the core, the six operations that took most of it (calls, ns each), source formats and conditionals. Timed with the generic timer (28 cycles a tick on a Pi 4, unbiased over many calls); run one Speedometer FPU test at a time |

Plus `DIRECT_ADDRESSING_GUARD` (stray guest addresses, `patches/macemu/0007`), `FPU_SELFTEST` (the known answers
and the digest of `src/common/fpu_selftest.h`, which must match `tests/host/check_fpu`'s), `SOUND_SELFTEST` and
`NET_SELFTEST` ([QEMU network](#qemu-network)).
Only calls crossing a translation unit are wrapped, and the wraps must be on the engine's partial link
([Build](build.md)).

### GDB and hardware debugging

`run-qemu.sh [kernel] -s -S` waits for GDB on `:1234`: breakpoints, stepping, memory inspection. On hardware: UART
(serial), assertions, the log on the card. Circle's `rpi_stub` covers only the Pi 2 and 3; a Pi 5 is debugged over SWD
(Debug Probe). Circle halts on a failed assertion, and under QEMU with `LEAVE_QEMU_ON_HALT` that looks exactly like
the emulator quitting by itself.

## Status

- [x] Host tests for firmware geometry and behaviour, platform rules, blitters, encoding, chime
- [x] `screenshot.sh` waits for the Mac's own idle signal
- [x] `run-test.sh` verdict after repair, on a named volume
- [x] Log on the card, previous session kept, wrap marker (2026-09-13)
- [x] `check-large-volume.sh`: a read past 2 GiB, failing without `patches/circle-stdlib/0002` (2026-09-15)
- [ ] Automated QEMU run in CI checking expected serial messages with a timeout, producing `kernel8.img`
- [ ] Hardware checklist run on each milestone: HDMI, SD, USB, sound, Ethernet, temperature, long-run stability, power cut
- [ ] Reuse `rcarmo/macemu-jit`'s test corpus (`jit-test/`, `BasiliskII/qa/`) to validate the interpreter, without its JIT
- [ ] A boot signal for the PowerPC engine (SheepShaver has no idle patch)
- [ ] Log rotation that keeps the chime and early boot lines of a session readable after a restart

## Pitfalls

- **Never run two emulators on the same card**, and never point a timed run at `qemu/sd.img`.
- **Judge the boot by the screen, not by proxy metrics.** A high opcode rate and "guest buffer has content" are
  equally true of the question-mark floppy.
- **Look at the screen and rebuild the card before calling a boot failure a regression.**
- **A QEMU window costs about a third of guest speed** (97% of the Mac's 60 Hz nominal headless, 61% windowed) and
  makes frame buffer writes 12× more expensive: run automated checks headless, never tune the compositor headless
  and assume it holds in a window.
- **QEMU lies**: no sound output, no RTC, video modes a Pi refuses, no EDID, no Wi-Fi, and a USB network adapter
  that never delivers a frame. Validate on hardware.
- **A failed assertion inside a member constructor is a silent hang**: nothing has been logged yet. Suspect a
  constructor argument when a kernel produces no output at all.
- **A test must name what it judged** (`Boot volume:`), or it can go green on the wrong subject.
- **`timeout` and backgrounding**: a session killed by anything but Shut Down is a pulled plug; use `run-test.sh`,
  which works on a copy.

## Development notes

- **2026-08-26** — Headless and windowed runs behaved differently at first (question-mark floppy headless, Happy Mac
  windowed); the cause was the decaying image, not the display.
- **2026-08-28** — `run-test.sh` born after a timed run corrupted a freshly rebuilt master image.
- **2026-09-04** — Host behaviour tests for the chooser and pages; the specimen driven by arrow keys.
- **2026-09-13** — Log written to the card, so a Pi session can be read afterwards.

## References

- QEMU monitor documentation; Circle `doc/qemu.txt`
