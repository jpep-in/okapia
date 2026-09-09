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
- **The PRAM is written the moment it changes** (`xpram_circle.cpp`) — not from the tick handler, which
  runs at IRQ level where blocking on the SD card is not allowed. On the 68k engine the moment is an event:
  `M68K_EMUL_OP_CLKNOMEM` is the one opcode by which XPRAM changes, and `emul_op_hook_circle.cpp` listens
  to it. **Anything periodic that has to touch the card, or to touch state the emulator reads, belongs in
  the engine's own seam and never in the tick**: `cpu_do_check_ticks()` for the 68k
  (`cpu_ticks_circle.cpp`, every 65 536 opcodes, about 4 ms) and `powerpc_check_ticks()` for the PowerPC
  (`sheepshaver/cpu_ticks_circle.cpp`, every `PPC_CHECK_TICKS` instructions — a seam kpx_cpu did not have,
  added by `patches/macemu/0004`, because upstream always had a spare thread and we have none).
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
`uae_cpu_2021` · `fpu_ieee` in binary128 · circle-stdlib `STDLIB_SUPPORT=3` · Quadra 650 ROM · fixed output
framebuffer + compositor · multicore S1 · network by sharing the Pi's MAC · no JIT · GPLv3.

`fpu_uae` was on that list until the evidence arrived. It keeps a 68881 register in a **`double`**
(`uae_cpu_2021/fpu/types.h:67`), so eleven of the Macintosh's sixty-four mantissa bits are dropped and its
exponent, which reaches 1e4932, is clamped to 1e308. On AArch64 `long double` *is* IEEE binary128 — a
15-bit exponent with the Mac's own bias of 16383, 112 fraction bits for the Mac's 63 — so the C99 core
holds the register exactly, with no library and no allocation. `patches/macemu/0008` enables it and fixes
what upstream had disabled it for. The alternative upstream's own configure picks on ARM is MPFR: correct
too, and a `malloc` and a `free` per floating-point instruction, which this project does not do.

## Pitfalls

- **IRQs run on core 0** only, and so does the cooperative scheduler. Hence S1: emulation on a secondary core.
- **Heap is not executable**: Circle sets `PXN=1` past `_etext`. Blocks any JIT.
- **Kernel size**: 2 MB default in Circle, 4 MB via circle-stdlib `--kernel-max-size` — which is what
  circle-stdlib's own `configure` passes, not something this project chose. It is not a hardware limit:
  `KERNEL_MAX_SIZE` (`sysconfig.h:38`) is the hole reserved between the load address and the stacks, the
  page table and the heap (`memorymap.h:47`), so raising it is a flag and a rebuild of the libraries.
  The merged image carrying both emulators measures 2 799 KB, so the 4 MB stands. Overflow shows as an
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
- **File names cross an alphabet on the way to the shared folder.** FatFs gives long names as UTF-8 and
  a Macintosh names its files in MacRoman; upstream's `extfs_unix.cpp` returns the pointer it was given,
  which is right between two UTF-8 systems and wrong here. `mac_encoding_circle.cpp` converts, hooked with
  `--wrap` rather than by patching `external/`, and the 128-entry table is **generated** by
  `scripts/gen-macroman.py` from Python's own `mac_roman` codec — one mistyped code point is one accented
  letter, in one language, wrong for years. A name it cannot convert comes back **whole and unconverted**:
  a name the Mac cannot read is a nuisance, half a name is a bug. It also asks the guest which script it
  writes in, through a 68k stub calling `ScriptUtil()` — which means it may only ask once a processor is
  running, and `MacIsExecuting()` is what says so.
- **The two Macintosh do not share a parameter RAM.** SheepShaver's is 8192 bytes, Basilisk's is 256, and
  the fields inside are at different offsets — so `xpram_circle.cpp` keeps `/BasiliskII_XPRAM` and
  `/SheepShaver_XPRAM` apart, and reads one byte more than it wants so that a *longer* file is refused
  rather than accepted as a short read. One file would have let each machine boot with the other's
  settings, silently, in the direction where the read succeeds. The *preferences* stay engine-agnostic
  (plan §19.7): that is a choice about Okapia, this is a fact about the Macintosh.
- **The interface's text is generated, never typed twice.** `assets/strings.tsv` holds every label in
  every language, and `scripts/gen-strings.py` turns it into both the tables and the `TStringId`
  enumeration — so a key renamed or removed stops the build instead of leaving an empty label on a screen
  nobody opened that day. `tests/host/check_geometry.cpp` then measures every page **in every language**,
  which is the whole reason the translations came before the chooser: a layout laid out against one
  language and translated afterwards is a layout that comes apart, and French runs longer than English
  almost everywhere. The firmware's language is the `language` preference, a two-letter code; an unknown
  one falls back to the first rather than failing, so a card written by a later version still boots.
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
  **Both kernels have to log it**, and for a while only one did: `kernel.cpp` names the volume its
  inventory chose, while `kernel_ppc.cpp` has no inventory and said nothing at all — so a card that
  starts the PowerPC Macintosh sent `run-test.sh` back to guessing, and it printed OK. It now names the
  first configured disk, which is the one `disk.cpp:161` hands the Mac first, and the test reports
  INCONCLUSIVE rather than OK when no volume is named.
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
- **The Macintosh says when it has finished starting, and it is the only one who knows.** The
  `idlewait` preference — upstream's default, and now ours — makes `patch_idle_time()` replace
  `SynchIdleTime` in the System being booted, so the first call to `idle_wait()` is the guest announcing
  that it has run out of work. `main_circle.cpp` logs it once; `screenshot.sh` captures a second later
  instead of after a number of seconds guessed from a different System, and `run-test.sh` refuses to
  call a run green without it. `HasIdleTime()` (`patches/macemu/0005`) says whether the patch went in
  at all, because a System with no `SynchIdleTime` never idles and waiting for it would hang the script.
  **SheepShaver has no such patch**, so the PowerPC engine has no boot signal — judge it by the screen.
- **Judge the boot by the screen, not by proxy metrics.** A high opcode rate and "guest buffer has
  content" are equally true of the question-mark floppy, so neither can tell a booted Finder from a
  stalled Mac — reading them as success cost this project a long detour. `scripts/screenshot.sh
  [seconds] [width height]` boots a throwaway copy and captures what is actually on the Mac's screen.
- **Damage to a System shows up as the wrong fonts long before it shows up as a boot failure.** A 7.1
  volume that had been through a few killed sessions drew its menu bar correctly and every icon label in
  a huge serif face, clipped at the right — which reads exactly like a compositor or a scaling bug and is
  not one. The same card with the volume restaged from `qemu/sd-contents/` was perfect. Before suspecting
  the video path, restage the volume: it costs one `mcopy` and it settles the question.
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
- **An interpreter has a decode cache, and it goes stale exactly like an icache.** `MakeExecutable()`
  looks like nothing to do under an interpreter — there is no instruction cache to flush — and leaving
  it empty is a bug: kpx_cpu keys its decoded instructions by address, so code the guest loads over
  addresses something else was decoded at runs as its predecessor. Upstream makes the call
  (`main_unix.cpp:1473`, `FlushCodeCache`), ROM excepted. The symptom reads backwards and cost a day:
  the Macintosh idles perfectly on the question-mark floppy — it loads no code — and comes apart about
  fifteen seconds into starting a System from disk, which is nothing but loading code. Anything that
  ever caches a translation of guest memory owes an invalidation on the same change.
- **`ld -r` kills `--wrap`, and silently.** The flag only redirects references the linker still has to
  resolve, and a partial link resolves everything inside the set it is given — so once the merged build
  collapsed 76 translation units into one object per engine, both product hooks linked, exported their
  symbols and were never called. `MacRestarted()` and `QuitRequested()` then stayed false for ever: a
  Restart from the Finder became a hard reset with no boot menu, and Shut Down never closed the disk
  image. The wraps therefore go on each engine's **partial** link (`ENGINE_WRAPS`), not on the final one.
  `objdump -d engine-68k.o | grep __wrap_` says in a second whether a hook is alive.
- **`ExitAll()` cannot be called from inside the emulator once the CPU is gone.** It closes thirteen
  drivers and several of them go through the Macintosh; after `exit_emul_ppc()` there is no Macintosh to
  go through, and it hangs — measured, with the board silent afterwards. Upstream survives the same order
  because it calls `exit()` next and nothing has to work after that. A port that must reach `halt()` or
  `reboot()` calls `XPRAMExit()` and `DiskExit()` and stops there: those two are what data safety is
  about, and they complete from that context. The rest is tidying for a program about to stop existing.
- **A PowerPC restart is only visible through `ether_reset()`.** SheepShaver has no equivalent of
  Basilisk's reset opcode, so a Restart from the Finder resets the nanokernel *inside* the emulator and
  reloads the same System without ever returning. `OP_RESET` (`emul_op.cpp:286`) calls `ether_reset()`,
  which is ours, and that is the whole hook. An ordinary 7.6 boot produces exactly **one** reset —
  measured — so the first is the cold start and every later one is the guest going round.
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
- **Objects in `src/kernel/emu-<engine>/` do not depend on the Makefile**, so changing a `-D`, a flag or a
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
- **One screen, two Macintosh, one set of rules.** The two engines present the display to their cores in
  genuinely different ways — Basilisk hands the platform a `monitor_desc`, SheepShaver runs the Mac's own
  video driver and wants a `VModes` table — but everything between those contracts and the compositor is
  the same question asked twice. It *was* written twice, and the copies drifted: only one of them repeated
  the palette across all 256 entries, and the one that did not drew 4 and 16 colours as coloured noise
  while 256 was perfect. `video_shared_circle.cpp` now states each rule once — which modes fit, which
  converter a depth needs, the palette, the refresh rate, the reporting — and each `video_circle.cpp` is
  its engine's contract and nothing else. **A display fix goes in the shared file unless it is about one
  core's vocabulary.**
- **The output frame buffer is Device memory, and vectorised copies fault on it.**
  `translationtable64.cpp:145` gives `ATTRINDX_DEVICE` to every page at or above the ARM's share of RAM,
  and the GPU's frame buffer is exactly there. Device memory requires every access to be naturally
  aligned, so a wide NEON store at an arbitrary offset is an alignment fault — `EC 0x25`, `DFSC 0x21`,
  and a board that dies about a second after the boot menu appears. That is what `-O3` produced:
  it vectorised `GfxBlit()` (`okapia_gfx.cpp:113`), whose copy starts at whatever offset the damage
  rectangle gives it. **Anything new that writes the frame buffer must be alignment-safe**, and the
  compositor's `memcpy` per row is only safe because rows start aligned and are whole. `src/kernel/Makefile`
  keeps `-O2` and says why; -O3 also measured *slower*, so there is nothing to recover by trying again.
- **The compositor only redraws what changed** (16x16 grid, shadow copy, `memcmp` per tile). That is what
  makes a window affordable: 452 us instead of 74 637 under `-display cocoa`. Anything that changes what
  the output should show **without changing the guest bytes** must set `bFullRedraw` — a mode switch and
  a palette change already do; a gamma ramp for direct modes would too.
- **A screen at rest used to cost the whole comparison and produce nothing**: 0/256 boxes and 1 225 us a
  frame, seven per cent of wall time for no pixels. It is now about 200 us and 1.1 %, and the guest got
  the difference — 2 880 to 3 031 k opcodes/s. Neither change costs anything in the emulation loop.
  **A row of adjacent dirty tiles is drawn as one span**, because converting 640 pixels once beats
  converting 40 of them sixteen times.
- **Never draw the screen from a partial scan.** Upstream spreads the comparison over eight ticks
  (`update_display_dynamic`, `video_x.cpp:2343`) and draws whatever that eighth found. Doing the same
  here was measurably faster and **visibly wrong**: a change covering many tiles is then noticed a few
  tiles at a time and therefore drawn a few tiles at a time, so a menu comes down as a mosaic and a
  window opens in scattered blocks — an iMovie wipe nobody asked for. It was reported by eye, not by any
  number, which is the whole lesson: a compositor is judged on the screen. So `CompositorRun()` does two
  passes. The first looks only at the cheap set — wherever the screen has been moving lately plus a
  one-tile margin, and a rotating eighth of the rest — and if it finds nothing, **nothing is drawn and
  nothing can tear**. The moment it finds anything at all, the second pass compares every remaining tile
  before a single pixel goes out. A still screen therefore costs an eighth; a frame in which anything
  moves pays the full comparison, exactly as it always did, and shows the change whole. `nFullScans` in
  the video report is how many frames took the second pass.
- **The cheap set cannot have one frame of memory**, and this cost a day. `Moving[]` holds the tiles
  dirty in *this* frame, so overwriting `Watched` with it means a single still frame erases what the
  compositor knew about where the screen was moving. Everything on screen tolerates that except the one
  thing always moving slowly: the pointer, which at the low end of the Mouse control panel's range — or
  on any magnified mode — advances a pixel every second or third frame. Its own steps therefore emptied
  the set between them, and the next step fell outside the cheap set and waited its turn in the eighths
  rotation: **up to 133 ms, at random**. Reported by eye as a pointer that catches for an instant and
  then goes on, and **present in no measurement**, because those are frames in which the compositor
  correctly decided nothing had changed. `WatchFor[16][16]` now keeps a tile in the set for twelve
  frames after it stops. An ablation put this one change at about eighty per cent of a pointer problem
  whose other three causes were all found by measuring — see
  `developer_notes/2026-09-08-pointeur-saccade.md`.
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
- **A tick derived from `HZ` beats, and the average hides it.** Circle's periodic handler fires at `HZ`,
  which is **100** (`timer.h:30`) and which no project setting changes, so a Mac tick can only ever be
  emitted on a 10 ms grid — and 16625 does not divide 10000. The spacing that comes out is 20, 10, 20,
  20, 10 ms: the rate is exactly right and **one frame in three is half the length of its neighbours**,
  which the Macintosh redraws its pointer on. Measured invariant at 103 short intervals to 202 long ones
  in every five-second window, at rest and under load alike. `CUserTimer` (`usertimer.h`) takes a delay
  in microseconds and fixes it; the accumulator stays as the fallback for `RASPPI > 4`, since nothing is
  assumed of a Pi 5, and the kernel says which is in force. **A late tick is dropped, never repaid** —
  Circle rearms its own comparator by one period whatever the latency (`timer.cpp:577`), so a catch-up
  of our own on top turned one delay into sixteen ticks at 0 ms followed by a 140 ms hole. Upstream's
  own thread has the same rule (`main_unix.cpp:1348`). **`CUserTimer` has never run on real hardware**:
  the `VBL spacing` histogram in the log is what will say whether it does.
- **A CD image is partition 1, a disk image is partition 0**, and libhfs refuses the wrong one outright —
  "not a Macintosh HFS volume" one way, "invalid partition map" the other. `MountReadOnly()` tries both.
  Worse, a Toast image's driver descriptor announces **2048-byte blocks while its partition map is written
  at 512**: the emulator reads it correctly only because `find_hfs_partition()` assumes 512 throughout
  (`cdrom.cpp:194`, `disk.cpp:120`), and a reader that trusts the descriptor finds nothing. Verified on
  `installppc86fr.toast`, whose HFS volume starts at byte 170 496 = block 333 x 512.
- **A Macintosh boot proves nothing about the FPU.** Measured with `--wrap=_Z16fpuop_arithmeticjj`
  (`trace_fpu_circle.cpp`, `OKAPIA_TRACE=1`): System 7.1 reaches the Finder in **four** floating-point
  instructions. So the whole path can be wrong and every boot still green — which is how upstream's
  binary128 case sat broken for twenty years with 1.0 coming back as 1.5. Anything that touches the FPU
  is judged by `fpu_selftest_extended()` (`patches/macemu/0008`, logged at startup under `OKAPIA_TRACE`),
  never by a boot.
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
- **An argument is evaluated before the call it is an argument to**, and a screen that records "the
  index the next component will have" on the line above is recording the wrong one as soon as the
  rectangle it passes adds a component of its own. `Row()` in `okapia_settings.cpp` places a label and
  answers the space left for the control, so `s_nMemory = nCount; PageAdd (..., Row (...), ...)` named
  the *label*: every menu on the page then answered for its neighbour, and fifteen measurements went red
  at once with nothing in the layout to see. **The rectangle first, the index second, the component
  last.**
- **The firmware's screens are pure, and that is what makes them measurable.** A screen is handed values
  and answers values; everything that knows what a preferences file, an HFS volume or a frame buffer
  looks like stays in `src/firmware/circle/`. `tests/host/check_pages.cpp` and `check_chooser.cpp` then
  drive them with synthetic events on this machine — geometry in every language at every scale, and
  behaviour — with no card, no emulator and no screen. Adding a screen that reads a preference directly
  would end that, so don't.
- **The Macintosh goes round more than once, so "once per boot" is not "once per start".** A restart
  from Mac OS re-enters the ROM's reset path, where Basilisk's own `M68K_EMUL_OP_RESET` sits
  (`rom_patches.cpp:1069`, handler at `emul_op.cpp:87`), so `CKernel::Run()` unwinds the emulator and
  runs the firmware's window again — the only way the boot menu is reachable after the first power-on.
  Measured on 7.1 and 7.6.1, headless and windowed: **an ordinary boot produces exactly one reset**, so
  the first is the cold start and every later one is the guest going round. Everything that used to run
  once now runs once *per start*, and three things could not stand it:
  `CTimer::RegisterPeriodicHandler` (four slots, no way to return one — the fifth start asserts and a
  Circle assertion halts, which under QEMU ends the session); `VideoMonitors.push_back` (a second entry
  is a **second screen**, and `InitAll()` reads `VideoMonitors[0]`, so the Mac draws where nothing
  composites); and the frame buffer, claimed and released at each handover, which lost 4.8 MB a round.
  All three are now taken once for the life of the board — the frame buffer through
  `FwOutputClaim()` (`okapia_output.h`), on the model of the one mouse registration. **Before adding
  any registration, claim or `push_back` to a start path, ask what the fifth restart does to it**, and
  log the free heap per round: a number that does not move is the cheapest proof there is.
- **`quit_program` stays set after the interpreter leaves** (`newcpu.cpp:1562`): upstream exits the
  process next and never has to start again. A second `Start680x0()` therefore returns at once, which
  reads exactly like "the Macintosh would not start". `MacRestartArm()` clears it.
- **Never cut a feature out to guard against a loop nobody has seen.** A guard that disabled the restart
  window after three quick rounds "in case the Mac restarts itself" fired on the person using it
  instead: System 7.1 boots in about three seconds, so somebody restarting a few times in a row is
  indistinguishable from a runaway. And a runaway would not have been a brick — the firmware's two
  seconds come round every time, so Option still reaches the chooser. The log names the pattern; nothing
  disengages.
- **Test against `boot71.img`, not the 500 MB System 7.6 volume.** System 7.1 reaches the Finder in
  about three seconds under QEMU where 7.6.1 takes two to three minutes, and a card carrying it is built
  in a second by the recipe in `run-test.sh` (`mformat`, then the image copied in as `machd76.image`).
  A verification loop that costs minutes per round does not get run.
- **`CActLED::Blink()` is not a hint, it is a pair of blocking delays.** `actled.cpp:95` turns the LED on,
  waits 200 ms, turns it off and waits 500 ms — synchronously. One call per second in the firmware's event
  loop stopped it dead for seven tenths of every second, so the pointer stuttered and keystrokes arrived
  late. It reads exactly like an emulator running out of time and it is one line of decoration. Fine in a
  constructor, never in a loop.
- **Circle's cooked mouse silently drops every report** until `Setup()` gives it screen dimensions, so a
  working keyboard alongside a dead mouse says nothing about USB, ADB or interrupts. Okapia wants raw
  deltas anyway: `RegisterStatusHandler()` hands over `dx/dy` for `ADBMouseMoved()`, whereas the cooked
  `RegisterEventHandler()` reports absolute coordinates that must not be passed as relative motion
  (`input_circle.cpp`).
- **Nothing calls `adb.cpp` from core 0.** `ADBKeyDown()` writes `key_buffer[]` and then `key_write_ptr`
  with no lock at all (`adb.cpp:302`); upstream survives it because x86 publishes stores in order, and
  AArch64 does not, so the Mac can read a key code that has not been written yet. Circle's USB handlers
  run on core 0 and the emulator does not, so the handlers only record — a ring for keys and buttons,
  an accumulator for motion, published with a release store — and `InputDrain()` hands everything to
  `adb.cpp` from the emulation core. The mouse is the exception that proves it: `ADBMouseMoved()` does
  take `mouse_lock`, which is why it never misbehaved and why the keyboard's silence was not evidence.
  And every `ADB*()` call raises `INTFLAG_ADB` and triggers the interrupt on its own — the drain adds
  neither, which is the same "don't double it" the cooked-mouse note above was already about.
- **The drain is armed by the clock, not by an opcode count.** `cpu_do_check_ticks()` fires when
  `emulated_ticks` wraps (`newcpu.h:332`), which is a rate only if the engine's speed is fixed — and it
  is not: measured between **209 and 1076 visits per five-second window**, a drain every 5 to 24 ms
  where the comment claimed four. The quantum is now measured against the clock every 64 visits and
  corrected to hold 1 kHz, exactly as infinite-mac does for the same reason (`main_unix.cpp:342`), it
  being the other port with no thread to spare. **A kilohertz is not excess**: the mouse reports at
  about 100 Hz, and below that order the reports merge — a merged report is one ADB packet carrying up
  to 275 counts where a 200 cpi mouse never sent a dozen, so the Mac's acceleration extrapolates a speed
  no hand can produce and the pointer leaps. At 1 kHz it is **one ADB packet per USB report**, measured
  228→228, 530→530, 179→179, the sole exception being the window where the drain had fallen to 668/s.
  SheepShaver's seam is still a fixed `PPC_CHECK_TICKS`, and it runs at ~135 Hz.
- **The Mouse control panel writes `SPVolCtl`, not `CrsrThresh`, and only Basilisk can hear it.** The
  ROM offers two plausible homes and System 7.1 uses one: over eight changes of the slider, `CrsrThresh`
  (0x8EC) never left 6, its startup value (`StartInit.a:2944`), and neither did parameter RAM — but
  **`SPVolCtl` (0x208), bits 5:3** followed every one. Swept in both directions: seven positions, the
  tablet at 0, "Slow" at 1, "Fast" at 6, and 7 never reached. None of that is needed while the engine
  feeds `adb.cpp` **relative** deltas, because the Macintosh's own driver then does the accelerating —
  which is the whole argument for relative over absolute, and why an absolute port has to reimplement a
  curve it cannot read. SheepShaver has no such choice: its absolute path calls `CursorDeviceDispatch`
  with **`MoveTo`, selector 1, which does not accelerate** — only `Move`, selector 0, runs deltas
  through the tables (`CrsrDev.a:137`, `adb.cpp:405`). So the Mouse control panel is inert on that
  engine by construction, and making it work means calling selector 0 ourselves.
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

**One image, both Macintosh.** `make -C src/kernel` builds Basilisk II and
SheepShaver, partial-links each into one object, renames every symbol the
PowerPC half *defines* (prefix `ppc__`, list generated at each build), and links
them together. The two cores export the same `InitAll`, `ExitAll`, `PatchROM`
and `Execute68k`, which is why one of them has to be renamed; nothing in
`external/` is touched — the surgery is on the objects, after compilation.

- **The seam is two functions**, `OkapiaRun68k()` and `OkapiaRunPowerPC()`
  (`src/kernel/okapia_boot.h`), and they are the only names left out of the
  rename list. They have C linkage so a build rule can name them without
  knowing how this compiler mangles.
- **The engine handed to does not open the window again.** The firmware is shared and has just run, so
  asking a second time reads as a reboot back to the menu — pick PowerPC, watch the menu come up again,
  pick again. `OkapiaRun*(bSwitched)` skips that one pass and every later time round opens as usual,
  which is the only way back to the menu after a restart from Mac OS.
- **The PowerPC Macintosh returns `OkapiaReboot` when it stops**, not `OkapiaHalt`: reaching there is a
  restart or an unnamed stop, never a shut down — that goes through `QuitEmulator()`, which closes the
  drivers and halts without returning. Halting there left no way to change System at all. The 68k engine
  loops in place instead, because Basilisk's reset opcode unwinds the interpreter and `InitAll()` pairs
  with `ExitAll()`; SheepShaver has no restart path upstream (it exits the process), so a second
  `InitAll()` over a torn-down nanokernel is untested.
- **Switching engines is a call, not a reboot.** `okapia_boot.cpp` owns `main()`;
  a kernel whose startup volume asks for the other emulator returns
  `OkapiaSwitchTo*` and the other one is entered with the board still up. The
  chain boot this replaced read a second 4 MB image off the card and reset.
- **What must exist once, exists once** (`SHARED_SRCS` in the Makefile): the
  board (`hal_circle.cpp`), the Mac RAM block (`mac_ram_circle.cpp`), the entry
  point, and the whole firmware. Duplicating any of them would duplicate
  *state* — a second periodic timer handler, a second mouse claim, a second
  frame buffer, a second 256 MB block — and Circle gives none of those back.
  `COkapiaBoard` is therefore a singleton whose four `Start*()` are idempotent,
  and `MacRamClaim()` answers the same block to the second engine.
  Everything else is compiled twice on purpose: it costs image size and no
  correctness.
- **`--wrap` does not survive a blanket prefix.** The linker looks for
  `__wrap_<name>` for the `<name>` it is given, so the prefix goes *inside* the
  `__wrap_`. And `__real_<name>` is never *defined* — the linker fabricates it —
  so it does not appear in `nm --defined-only` and has to be caught among the
  undefined symbols, or the renamed hook calls the other engine's original.
- `ENGINE=basilisk|sheepshaver` still selects one tree, for the sub-builds and
  for `objects`; it no longer picks what runs.
- Objects live in `emu-<engine>/` and the shared ones in `obj-shared/`;
  `make okapia-clean` removes all three and the merge intermediates.

The preferences file is called `BasiliskII_Prefs` whichever Macintosh runs —
engine-agnostic by design (plan §19.7) — and carries `rom` for the 68k engine
and `romppc` for the PowerPC one. The chooser writes an `engine` line per
universal volume.

```
./scripts/bootstrap.sh      # tools, submodules, references
./scripts/build-qemu.sh     # AArch64 kernel for QEMU raspi3b
./scripts/run-qemu.sh       # serial on stdout, GDB on :1234
./scripts/build-pi.sh 4     # kernel for Pi 3, 4 or 5
./scripts/specimen.sh [n]   # firmware theme specimen under QEMU, page n, no SD card
./scripts/specimen.sh live  # the same, in a window, driven by hand
make -C tests/host          # the same specimen rendered here, plus the geometry checks
```
