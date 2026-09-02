# Okapia — agent instructions

Bare-metal classic Macintosh on Raspberry Pi: Basilisk II ported to Circle.

Single source of truth: **`planification.md`** — decisions, architecture, phases, verified facts with
`file:line` references. Use it instead of re-deriving or re-searching.

## Token budget

- No preamble, no restating the request, no summary of what you just did.
- Write nothing that wasn't asked for: no extra docs, no comments restating the code.
- `rg -n 'pattern' path/` before any `Read`. Never read >500 lines without `offset`/`limit`.
- **Never open whole**: `codegen_x86.h`, `compstbl*`, `comptbl*`, `cpuemu*.cpp`, `cpustbl*`, `gencomp*.c`,
  any `*_arm*.cpp` in the JIT forks. Generated tables, 100 KB to 1.5 MB. Grep them, never read them.

## Resource budget

Target is Quadra-class, not a workstation.

- **No allocation** (`new`, `malloc`, growing container) in the emulation loop, video compositor, audio
  callback or frame path. Allocate at startup.
- The 257 MB Mac RAM block is allocated **first**, before drivers: Circle's heap serves large blocks
  linearly from remaining space.
- No virtuals, no `std::function`, no exceptions in hot paths.
- Precomputed tables over per-pixel work (`ExpandMap[256]` is the model). Process video by row or tile.
- Everything outside the 257 MB must fit a 1 GB board. Kernel < 4 MB.
- Measure before optimising — but don't write obvious waste while waiting.

## Data safety

**This is what separates an emulator people use daily from a proof of concept.** One corrupted volume
ends that trust and no amount of speed buys it back, so this constraint outranks performance and
features: when in doubt, lose speed, never data. It must hold across every edge case — power cut,
`kill -9`, reset, full card, cable pulled mid-write — not just the happy path.

The model is BlueSCSI: real SCSI-emulating hardware keeps disk images on an SD card and survives the plug
being pulled, because every write it acknowledged is already on the card. **Okapia keeps the data and
loses the boot**, and the gap is now measured down to one bit.

From a volume Disk First Aid certifies healthy, one SIGKILLed session leaves exactly what a pulled plug
leaves on a real Mac: `drAtrb` 0000 and "MDB needs minor repair", nothing else — no orphaned blocks, no
catalogue damage. Writes are not lost either: `--wrap=Sys_write` counts 39 writes to reach the Finder,
every one complete. **The data survives.** But the Mac then shows the question-mark floppy, and stays on
it for two minutes, so it is a refusal and not slowness.

Setting `drAtrb` bit 8 back by hand — offset 1034, nothing else touched — makes the same card boot, and
MacOS still shows "this computer was not shut down properly", which it detects by some other route. So
that bit does not drive the warning; its only role is gating **the boot**.

**And that is Mac behaviour, not our bug.** A Mac refuses to start from a volume marked in use, but
mounts it happily as a secondary disk — and that mount runs the HFS scavenge that repairs it. Years of
field experience with Basilisk on macOS show the whole cycle: a crash leaves the 7.6 volume dirty, the
next launch boots the 8.1 volume instead, 8.1 mounts and repairs 7.6, and after a clean shutdown 7.6
boots again untouched. Okapia reproduces this exactly. What we lack is not correctness but a **second
bootable volume**: with one disk there is no fallback and nobody to run the repair.

So the fix is period-correct, not a patch: carry a small rescue System as a second `disk` preference —
`disk.cpp:161` already loops over them, and a `*` prefix mounts read-only, which makes a rescue volume
that no crash can ever dirty. **Never mark the volume clean ourselves**: that hides real corruption and
is exactly the trade this project refuses.

Earlier readings that blamed catalogue destruction were taken on a baseline that was already corrupt:
check `qemu/sd-contents/machd76.image` with Disk First Aid **inside the guest** before trusting any
durability measurement, and note that macOS's `fsck_hfs` speaks HFS standard poorly enough to condemn a
volume it cannot then repair.

Until then: **`run-live.sh` sessions must end with Finder → Shut Down.** Anything else costs the card, and
`scripts/make-sd-image.sh` is the recovery. Do not read a boot failure as a code regression before
rebuilding the card and looking at the screen — that mistake has now cost this project two long
investigations.

- **No write-back cache between the guest and the card.** If an optimisation ever adds one, it owes a
  flush policy and a test in the same change.
- **Acceptable after a pulled plug**: a volume marked in use, which Disk First Aid repairs — that is what
  a real Mac does too, and no emulator can save the guest's own RAM cache. **Not acceptable**: lost
  writes, structural damage, or — as today — a card the Mac can no longer start from.
- **Every path that writes guest data must survive an abrupt stop at any instruction.** Ask it of new code
  before it lands, not after someone reports a broken disk.
- **Prove it, don't assume it**: `scripts/run-test.sh [seconds] [image]` ends the guest with SIGKILL,
  boots the card again so `HfsRepair` runs, and only then judges the volume. Its verdict rests on the
  state **after** the repair, because a volume marked in use is what a pulled plug leaves on a real Mac
  too and `fsck_hfs` condemns it on that basis alone. It also reports how many bytes the guest wrote, so
  a green verdict cannot come from a run that exercised nothing. Pass an image to regression-test a second
  System without touching the card you use. Verified for System 7.1 and 7.6: flag `0100` and `fsck` clean
  after repair, both times.
- **`fsck_hfs` on macOS is a weak instrument here.** It supports HFS standard poorly: it calls the
  reference image corrupt and then cannot repair it either, B-tree rebuild included. Treat "corrupt" from
  it as a hint, not a verdict, and prefer what the Mac itself says — a volume that boots is worth more
  than a clean bill from a host tool that barely speaks HFS. Disk First Aid inside the guest is the
  period-correct check.
- **The guest writes everything we are asked to write.** Measured with `--wrap=Sys_write` under
  `OKAPIA_TRACE=1`: a boot to the Finder issues 39 writes, every one complete, none short. So an
  interrupted session does not lose writes in our layer — what it loses is whatever MacOS still held in
  its own RAM cache, which is the same exposure a real Mac has. Do not go looking for a bug in the file
  layer before re-checking that.
- **The clock never goes backwards.** It starts at `max(build time, the most recent drLsMod on the card)`
  (`CKernel::RefineClock()`). A clock that recedes between boots produces volumes whose `drLsMod` precedes
  `drCrDate`, which is exactly what `fsck_hfs` calls "MDB needs minor repair" — so any source added later
  (RTC, NTP) goes through the same floor. Beware the frames: `drLsMod` is **local** time and
  `OKAPIA_BUILD_TIME` is **UTC**; comparing them raw is an hour or two out.
- **The PRAM is written the moment it changes** (`xpram_circle.cpp`), from `VideoInterrupt()` — not from
  the tick handler, which runs at IRQ level where blocking on the SD card is not allowed. That hook is the
  only periodic call running in the 68k thread, the same context as `Sys_write`. Anything else that needs
  to touch the card periodically belongs there too, not in the tick.
- **The shared folder writes through** (`extfs_sync_circle.cpp`). FatFs keeps the tail of a write in the
  file object and only records the new size at `f_sync`/`f_close`, so a pulled plug would leave a
  directory entry saying zero bytes for a file whose clusters are already on the card — a loss that looks
  like a success. The disk image has no such window: the Mac writes whole 512-byte sectors at sector
  boundaries, which FatFs passes straight through.
- Still owed as features land: full card, a write error from the SD layer, removal mid-write, and
  shutdown during a write.

## Work locally

After `scripts/bootstrap.sh`, no network needed:

- `external/macemu/` — Basilisk II + SheepShaver (`kanjitalk755` upstream)
- `external/circle-stdlib/` — newlib + libstdc++; **Circle lives in `libs/circle/`**, docs in
  `libs/circle/doc/` (`qemu.txt`, `multicore.txt`, `memorymap.txt`, `stdlib-support.txt`, `issues.txt`)
- `reference/` — study material, **cloned on demand, not by default**. `scripts/fetch-reference.sh` with no
  argument lists what exists and what is local. If you need one that isn't there, **ask before cloning** —
  don't go read it on the web instead.

**Never modify `external/` or `reference/`.** Upstream changes go to `patches/macemu/`, minimal, documented.

## Invariants

Settled (`planification.md` §2). Don't reopen without new evidence: 256 MB Mac RAM · `DIRECT_ADDRESSING` ·
`uae_cpu_2021` · `fpu_uae` · circle-stdlib `STDLIB_SUPPORT=3` · Quadra 650 ROM · fixed output framebuffer +
compositor · multicore S1 · network by sharing the Pi's MAC · no JIT · GPLv3.

## Pitfalls

- **IRQs run on core 0** only, and so does the cooperative scheduler. Hence S1: emulation on a secondary core.
- **Heap is not executable**: Circle sets `PXN=1` past `_etext`. Blocks any JIT.
- **Kernel size**: 2 MB default in Circle, 4 MB via circle-stdlib `--kernel-max-size`. Overflow shows as an
  obscure link error.
- **Pi 5**: display resolution **cannot be set by the application**, and `config.txt` won't configure it.
  Never assume a mode was granted — print what the firmware actually returned.
- **Pi 1/2/3**: Ethernet is on USB and starves keyboard/mouse under sustained traffic.
- **`uae_cpu_2021`, not `uae_cpu`** — that's what macemu builds on AArch64.
- **One MAC address**: `CNetDevice` has no promiscuous mode.
- **QEMU lies**: `raspi3b` accepts 8 bpp + palette that the Pi 5 refuses. Validate on hardware.
- **The shared folder needs the File System Manager 1.2**, which is **built into Mac OS 7.6 and later and
  ships as a system extension for earlier Systems** (`BasiliskII/TECH` §6.10). Without it `extfs.cpp:491`
  prints "No FSM present, disabling ExtFS" and there is no shared volume — which is what a bare System 7.1
  does, and it makes the feature look broken. **The limit is the extension, not the System version**:
  verified working on French System 7.1.2 with the FSM 1.2 extension from Apple's SDK
  (macintoshrepository.org/2070-file-system-manager-1-2-sdk), because `extfs.cpp:487` tests it with
  Gestalt and an extension answers as well as a built-in. The FSM is **not** File Sharing: that is
  AppleShare over the network and does not provide it. ExtFS is also single-volume by construction — one
  `RootPath`, one VCB — so several shared folders would mean rewriting a file in `external/`.
- **The key table is generated, never typed.** `scripts/gen-keycodes.py` derives it from
  `BasiliskII/src/Unix/keycodes`, section `sdl cocoa`, at build time — **SDL2 scancodes are USB HID usage
  IDs**, so that section already is the table a USB host needs. The hand-written version it replaced had
  all four arrows wrong: `ADBKeyDown()` takes *raw ADB* codes, which agree with the far more familiar Mac
  virtual key codes for letters and digits and differ for the arrows, so Up went out as 0x7E (virtual;
  ADB wants 0x3E), landed beside the Power key and opened the shutdown dialog on every press. It also
  omitted the numeric keypad entirely. A card may override the table through `keycodefile`, in the same
  format. Related trap: a lookup table whose initialiser is shorter than its declared size leaves the
  tail zeroed, and **ADB 0x00 is the letter A**, so every unmapped key types "a".
- **QEMU has no RTC to offer.** Its machines stop at `raspi4b`, all BCM283x/2711, and none of those SoCs
  has a real-time clock; there is no `raspi5` machine, which is where the Pi's built-in RTC would be. So
  the RTC branch of the clock chain cannot be exercised without hardware, and everything under QEMU falls
  through to the build time and the card (plan §10).
- **The QEMU monitor can drive the Mac's mouse and keyboard**, which is how the Finder gets tested with
  nobody at the screen: `mouse_move dx dy`, `mouse_button 1|0`, `sendkey`. Four traps: commands sent as a
  burst down one `nc -U` connection are dropped — send one per connection with a pause; two
  `mouse_button` pairs do **not** make a double-click, because the monitor round trip is slower than the
  Mac's double-click time, so select then `sendkey meta_l-o` (Command-O) to open; the Mac applies pointer
  acceleration, so a delta of 40 moves about 29 pixels — aim by screenshot, not by arithmetic; and
  System 7 menus are **not** sticky, so a menu needs press, move, release rather than click, click.
- **A PRAM zap moves the startup disk, and the next boot may stop on an alert.** `BasiliskII_Prefs` lists
  `disk /boot608.hda` first, so a Mac with no startup disk in its parameter RAM falls back to it and a
  IIci ROM in 32-bit mode answers "System 6.0.8 does not work with 32-bit addressing" and waits for a
  click. `run-test.sh` then reports a volume never mounted and no writes — which reads exactly like a
  code regression and is not one. Look at the screen first, as AGENTS.md already says, and remember that
  `run-test.sh [seconds] qemu/sd-contents/boot71.img` builds its own card and is unaffected.
- **A test that picks its subject by guessing can go green on the wrong volume.** `run-test.sh` used to
  inspect the first `*.image` on the card; once a second System was staged, that was no longer the volume
  the Mac had booted, and the verdict meant nothing. The kernel now logs `Boot volume: <path>` and the
  test reads it from the log. Anything that judges "did this survive" must name what it judged.
- **A script that replaces a file must build beside it and move it into place.** `make-sd-image.sh`
  defaulted to a 64 MB card long after the disk images moved into `qemu/sd-contents/`, and it deleted the
  old card before writing the new one — so an argument-less run destroyed a working 1 GB card and left a
  truncated one carrying neither the ROM nor the System. The message was `Disk full` and nothing else.
  It now sizes the card from what is staged, refuses an explicit size that cannot hold it **before**
  touching anything, and stages the build in `sd.img.new`.
- **Never run two emulators on the same card.** Two QEMUs writing `qemu/sd.img` destroys the volume, and
  afterwards it looks exactly like random corruption — which is most of what the boot "non-determinism"
  really was. `run-live.sh` now refuses to start when the image is already open; `run-test.sh` and
  `screenshot.sh` work on copies and are always safe to run alongside anything.
- **Judge the boot by the screen, not by proxy metrics.** A high opcode rate and "guest buffer has
  content" are equally true of the question-mark floppy, so neither can tell a booted Finder from a
  stalled Mac — reading them as success cost this project a long detour. `scripts/screenshot.sh
  [seconds] [width height]` boots a throwaway copy and captures what is actually on the Mac's screen.
- **Killing QEMU corrupts the disk image**, because killing it is pulling the plug on a running Mac:
  MacOS caches HFS blocks in RAM and only sets the "unmounted cleanly" bit (MDB `drAtrb` bit 8, at image
  offset 1034) when it unmounts during Shut Down. The damage accumulates run after run until the Mac
  mounts the volume, reads the catalogue, gives up and falls back to the question-mark floppy. That was
  the whole of the boot "non-determinism": the image was decaying, not the emulator. Check a suspect
  image with `dd ... skip=1034 count=2` — `0100` is clean, `0000` is dirty — and repair with `fsck_hfs`.
- **Shutting the Mac down properly does work, and the whole chain must be intact.** Finder → Shut Down
  calls `PowerOff()` (trap `0xA05B`), which `rom_patches.cpp:1621` replaced with `EMUL_OP_SHUTDOWN`, which
  calls `QuitEmulator()`. That must call **`m68k_emulop_return()`** (`newcpu.h:281`), which is the only
  correct way out: leaving the interpreter takes two steps, and doing one is a silent hang. `quit_program`
  alone is tested by the outer loop (`newcpu.cpp:1562`), while `m68k_do_execute()` spins in an inner
  `for(;;)` that only `SPCFLAG_BRK` interrupts — so the Mac powers off and the emulator runs on, never
  reaching `ExitAll()`, the only thing that closes the disk image.
  Circle then halts, and `LEAVE_QEMU_ON_HALT` exits QEMU via semihosting, **which needs `-semihosting`
  on the QEMU command line**; without it the kernel halts and QEMU just sits there.
- **QEMU invents a durability hole that hardware does not have**: its drive defaults to `cache=writeback`,
  so guest writes stop in the host page cache. Both run scripts pass `cache=writethrough` to close it.
- **A timed run has nobody to click Shut Down**, so use `scripts/run-test.sh [seconds]` and never point
  one at `qemu/sd.img` (written after doing exactly that and corrupting a freshly rebuilt image). It runs
  on a copy **in order to see damage, not to look away from it**: from a known-good copy, whatever
  `fsck_hfs` reports was caused by that one run, whereas on the master the damage accumulates and nothing
  is attributable. It ends in SIGKILL — the harshest case — and reports how many bytes the guest actually
  wrote, so a green verdict cannot come from a run that exercised nothing. A failing copy is kept for
  inspection. Rebuild a card from `qemu/sd-contents/` with `scripts/make-sd-image.sh`, which now sizes the card
  from what is staged and builds it beside the old one, replacing it only once complete.
- **A guest that reads its disk and still won't boot is a volume problem, not a driver problem.** The
  trace to run first is `OKAPIA_TRACE=1` (`src/kernel/Makefile`): successful reads with no short reads,
  followed by a catalogue scan and then a second driver init, means the file layer is fine.
- **Trace upstream with the linker, not with a patch.** `--wrap=<mangled symbol>` hooks a Basilisk
  function without touching `external/` — see `trace_disk_circle.cpp` and `exception_trace.cpp`. Circle
  invokes `ld` directly, so it is bare `--wrap`, never `-Wl,--wrap`. Only calls crossing a translation
  unit are wrapped.
- **The QEMU window costs about a third of guest speed** (97% of the Mac's 60 Hz nominal headless, 61%
  with a window), so run automated regression checks headless and keep the window for watching.
- **Circle tracks no headers at all, and every rule in this repo must do it itself.** `Rules.mk:271` works
  out `DEPS` and then never includes it, and its own `%.o: %.cpp` carries no `-MMD`. So an object built by
  Circle's rule, or by a private rule that forgot the flags, is rebuilt when its `.cpp` changes and never
  when a header does — the link then joins objects built against two different layouts of the same struct
  and succeeds. It has cost this project twice: a field added to a theme struct sent the kernel into the
  font tables (instruction abort, PC past `_etext`, where `PXN=1`), and a field added to `TEvent` left the
  input bridge reading `nKey` at the wrong offset, so the down arrow arrived as Escape. Neither symptom
  points anywhere near the cause. Every Makefile here now compiles with `-MMD -MP` and reads the `.d` files
  back — `src/kernel`, `src/firmware/circle` (which builds even its own sources through `obj/` for this
  reason) and `tests/host`. **A rule without them is a bug, not a style.** And after adding the flags to a
  rule, `make okapia-clean` once: an object built before them still carries no dependencies.
- **Objects in `src/kernel/emu/` do not depend on the Makefile**, so changing a `-D`, a flag or a
  `#define` in `external/` rebuilds nothing: `make` links stale objects and you test a kernel that no
  longer matches the sources. This silently cost 4.8x guest speed — objects compiled while Basilisk's
  `D(bug())` tracing was on flooded the serial port (20 MB and 2.8M lines per run, 3000 k opcodes/s
  instead of 14400 k). After any flag change: `make okapia-clean && rm -f kernel8.img kernel8.elf`.
  `strings kernel8.img | grep 'EmulOp %04x'` tells you in one second whether debug tracing is linked in.
- **A QEMU window makes frame buffer writes 12x more expensive.** The same composite measures 1.5 ms
  headless at 640x480, 6 ms headless at 1280x960, and **74 ms with `-display cocoa`** — QEMU tracks dirty
  pages on the frame buffer once a display is attached. At `frameskip 1` that is 98% of wall time, the
  guest gets 4% of the machine and the boot never finishes: a Happy Mac that looks frozen but is only
  crawling. **Never tune the compositor headless and assume it holds with a window**; that mistake sent
  this project bisecting code that was never at fault. `scripts/run-live.sh` now opens a monitor socket,
  so a live session can be captured: `echo "screendump /tmp/x.ppm" | nc -U /tmp/okapia-monitor.sock`.
- **The compositor only redraws what changed** (16x16 grid, shadow copy, `memcmp` per tile). That is what
  makes a window affordable: 452 us instead of 74 637 under `-display cocoa`. Anything that changes what
  the output should show **without changing the guest bytes** must set `s_bFullRedraw` — a mode switch and
  a palette change already do; a gamma ramp for direct modes would too.
- **`frameskip 0` is Dynamic and now works**: the compositor holds itself to about an eighth of wall time,
  re-measured every second, capped at one refresh per 12 VBLs. That is the default. It settles on every
  VBL headless and on 3 Hz under a cocoa window — a slideshow, but the guest runs. The fix for the rate
  itself is phase 12's dirty regions, not a slower clock.
- **A decimated screen reads as a laggy mouse.** The Mac draws its own cursor into its own framebuffer,
  so the pointer can never move more often than the compositor runs. Upstream's `frameskip` default is 6
  — the "10 Hz" rung of Basilisk's Window Refresh Rate menu (60 Hz = 1, 30 = 2, 15 = 4, 10 = 6, 7.5 = 8,
  5 = 12, and **0 = Dynamic**) — which showed up here as a 9 Hz display and a mouse that dragged. Honour
  the preference, never hardcode the rate, and report the composite rate rather than the VBL rate: calling
  55 VBL/s "fps" hid a 9 Hz screen behind a reassuring number.
- **`gencpu`/`gencomp`** are built **for the host** and run during the build.
- **`config.h` declares, it never includes.** It is pulled in ahead of everything else; adding a system
  header there breaks the include order across the whole core.
- **Prefer the path upstream actually walks.** `EXCEPTIONS_VIA_LONGJMP` exists in the core but no upstream
  platform enables it, so nobody tests it and nothing defines its `JMP_BUF`/`SETJMP` macros. C++ exceptions
  are measured working under Circle, so the core uses them — add `-fexceptions` per file, since Circle
  builds with `-fno-exceptions` (`Rules.mk:188`).
- **Serial first, always.** Initialise the serial port and logger before anything else: a failure
  before the log exists is indistinguishable from a hang. Every other init failure should warn and
  continue, not abort.
- **A failed assertion inside a member constructor is a silent hang.** Member constructors run before
  `Initialize()`, so before serial exists. When a kernel produces no output at all, suspect a
  constructor argument, not the boot. (Cost us an hour: `CConsole(0, &m_Serial)` violates
  `assert (m_pInputDevice != 0)`. Use `CConsole(&m_Serial, &m_Serial)` — console on serial, since the
  screen belongs to the Mac.)
- **`DEPTH` is compiled into `libcircle.a`** (`lib/screen.cpp`), so `-DDEPTH=8` in an application
  Makefile does nothing. An indexed mode needs our own `CBcmFrameBuffer` — which Okapia wants anyway,
  since the screen belongs to the Mac, not to `CScreenDevice`.
- **Never claim a device the guest cannot actually use.** Sound was reported open while QEMU models no
  output at all: the Mac played its alert, waited for a completion that never came, and froze — and the
  hard stop that followed cost a card. A device that will not start must leave the platform hook exactly
  as the `src/dummy/` version leaves it, and the decision belongs at init, not in a hook the 68k calls.
- **A USB keyboard reports changes, never state.** A key held from power-on sends one report at the
  moment it goes down and nothing more until it is released, and `CUSBKeyboardDevice` offers no way to
  ask what is currently down. So a window that starts listening when it opens sees Option when a script
  sends it and never when a person holds it — which is exactly how a real user tries it. `FwInputWatch()`
  is therefore called from `CKernel::Initialize()` as soon as USB is up, and the window only reads the
  latch. A Macintosh had no such gap: it read the keyboard's state register.
- **`UnregisterKeyStatusHandlerRaw()` exists**, and is the only correct way to detach a raw handler —
  passing 0 to the register call does not detach, see below. Okapia never detaches, because Circle keeps
  one handler and `InputInit()` replacing it *is* the handover.
- **Circle's mouse can be claimed once per boot and never given back.**
  `CMouseDevice::RegisterStatusHandler` asserts `m_pStatusHandler == 0` (`mouse.cpp:85`) and offers no
  withdrawal, so the second claim is a failed assertion and a dead kernel. The keyboard is nothing like
  it — a raw handler simply replaces the last one — so the two devices cannot be handed over the same
  way. `src/firmware/circle/okapia_input.cpp` holds the one registration for the life of the boot and
  forwards the reports onward: `FwInputPassMouseTo()` is what `InputInit()` calls instead of registering.
- **A USB host built as a member of the kernel class is a silent hang.** Member constructors run before
  `Initialize()`, so before serial: `CUSBHCIDevice m_USBHCI` produced a kernel with no output at all, and
  nothing to say why. Built with `new` once the logger exists, the same code enumerates the hub, the
  keyboard and the mouse and says so. Same trap as the console below, different device — anything whose
  construction can fail belongs after the log, not beside it.
- **`RegisterKeyStatusHandlerRaw (0)` does not detach a keyboard, it changes its mode.** A null raw handler
  makes `ReportHandler` fall through to the cooked path (`usbkeyboard.cpp:200`) and hand the reports to
  `CKeyboardBehaviour`. Doing that to "give the keyboard back" after the firmware's window stopped the
  kernel dead — no further log at all, and the Macintosh never started. There is nothing to give back:
  Circle keeps one raw handler, so `InputInit()` replacing it in `StartMacintosh()` *is* the handover.
- **Circle's cooked mouse silently drops every report** until `Setup()` gives it screen dimensions, so a
  working keyboard alongside a dead mouse says nothing about USB, ADB or interrupts. Okapia wants raw
  deltas anyway: `RegisterStatusHandler()` hands over `dx/dy` for `ADBMouseMoved()`, whereas the cooked
  `RegisterEventHandler()` reports absolute coordinates that must not be passed as relative motion
  (`input_circle.cpp`). The ADB button and move calls set the interrupt flag themselves — don't double it.
- **macOS build frictions**, all handled by `scripts/install-tools.sh`: BSD `getopt` ignores `--long`,
  Bash 3.2 has no `mapfile`, BSD `sed` has no `\b`, and zsh aborts a command when a glob matches nothing.

## Code

- French for planning docs; **English for code, comments, log messages and the public README**.
- Follow each layer's convention: `CClassName`/`m_member` in Circle code, `snake_case` in Basilisk code.
  Don't unify.
- Adapted upstream files carry their origin and modification list in the header.
- **No silent stubs**: a stub logs its call and fails cleanly if it matters.
- A fixed reproducible bug comes with a test, on the host where possible. Keep QEMU working.

## Git

- One commit per coherent change. Commit or push **only when asked**.
- **No ROM, no Apple system, no disk image** in the repo. `roms/` is ignored.
- Submodules pinned to a SHA. Never `master`/`main`/`latest`.

## Commands

<!-- filled in at bootstrap; don't invent commands that don't exist yet -->

```
./scripts/bootstrap.sh      # tools, submodules, references
./scripts/build-qemu.sh     # AArch64 kernel for QEMU raspi3b
./scripts/run-qemu.sh       # serial on stdout, GDB on :1234
./scripts/build-pi.sh 4     # kernel for Pi 3, 4 or 5
./scripts/specimen.sh [2]   # firmware theme specimen under QEMU, page 1 or 2, no SD card
./scripts/specimen.sh live  # the same, in a window, driven by hand
make -C tests/host          # the same specimen rendered here, plus the geometry checks
```
