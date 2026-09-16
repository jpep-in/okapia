# Startup and shutdown

> Power-on to Mac OS, switching between the two Macintosh, restarting, and shutting down without losing
> data. Main files: `src/kernel/okapia_boot.cpp`, `src/kernel/kernel.cpp`, `src/kernel/kernel_ppc.cpp`,
> `src/circle/hal_circle.cpp`, `src/circle/board_sound_circle.cpp`, `src/circle/main_circle.cpp`,
> `src/circle/sheepshaver/main_circle.cpp`, `src/circle/sheepshaver/platform_bits_circle.cpp`.

## Using it

- **Power on**: the startup chime, two seconds of plain grey, then the Macintosh. Hold **Option** during
  the grey to open the [boot menu](boot-menu.md); **Command-Option-P-R** forgets the parameter RAM.
- **Restart** from Mac OS (Special → Restart): the chime plays again and the grey window comes back, so the
  boot menu is reachable after every restart, not only at power-on.
- **Shut down** from Mac OS (Special → Shut Down) or from the boot menu: the disk image is closed, the screen
  goes black and the display is blanked. A Raspberry Pi 4 cannot switch itself off — cut the power then.
- **Never pull the plug on a running Mac.** It costs the boot, not the data: see [Storage](storage.md).

## How it works

### Power-on

`okapia_boot.cpp` owns `main()` and enters the 68k engine first. `CKernel::Initialize()` runs once per
power-on:

1. `COkapiaBoard::Start` — serial and logger first, then interrupts, the timer and its clock. The ARM clock
   is raised to its maximum (`CCPUThrottle`): the firmware leaves a Pi 4 at 600 MHz.
2. The card, then the log file on it (`okapia.log`, previous session in `okapia-previous.log`).
3. Preferences and time zone; the Mac RAM block ([Architecture](../project/architecture.md#memory)).
4. The startup chime, from a kernel timer ([Sound](sound.md#startup-chime)).
5. USB, and the firmware latches the keyboard immediately (`FwInputWatch`).

`CKernel::Run()` then loops: the firmware window (`FirmwareRun`), then either a hand-over to the other
engine or `StartMacintosh()` → `InitAll()` → `Start680x0()`. Whichever engine starts, the card is prepared the same
way before `InitAll()` — inventory, clock floor, repair, boot volume — by `card_circle.cpp`
([Storage](storage.md#repairing-the-volume-ourselves-libhfs)): the hand-over happens before the 68k kernel prepares
anything, so the PowerPC kernel does it itself.

### Which Macintosh starts

The startup volume's System decides ([Boot menu](boot-menu.md#engines)): a 68k-only System starts Basilisk,
a PowerPC-only one SheepShaver, a universal one (7.5.2 to 8.1) what its `engine` line says. When the 68k
kernel finds the other engine wanted, it returns `OkapiaSwitchToPowerPC` and `okapia_boot.cpp` calls
`OkapiaRunPowerPC(1)`: the board stays up, nothing is re-read, and the firmware window is skipped once
because the shared firmware has just answered.

### Restart from Mac OS

**68k.** Restart re-enters the ROM's reset path, where Basilisk's `M68K_EMUL_OP_RESET` sits
(`rom_patches.cpp:1069`, `emul_op.cpp:87`); `emul_op_hook_circle.cpp` hears it. The interpreter unwinds,
`ExitAll()` closes the drivers (the disk image included), `Exit680x0()` pairs `Init680x0()`, the input
devices return to the firmware, the chime replays, and the loop opens the firmware window again.
`MacRestartArm()` clears `quit_program`, which stays set after the interpreter leaves
(`newcpu.cpp:1562`) — otherwise the next `Start680x0()` returns at once.

**PowerPC.** SheepShaver has no restart path upstream: a Restart resets the nanokernel *inside* the emulator
and reloads the same System. `OP_RESET` calls `ether_reset()` (`emul_op.cpp:286`), which is ours. An ordinary
boot produces exactly one reset, so the first is the cold start and every later one is the guest going round.
On a later one, `ether_reset()`:

1. replays the chime, runs the firmware window **in place** while the Mac waits inside the call;
2. if the answer is the same startup (disk, CD, boot driver, sound output unchanged, same engine), gives the
   input back, restores the video mode and returns — the Mac's own reset carries on, instantly;
3. otherwise leaves through `QuitEmulator()`: a different startup resets the board (`OkapiaReboot` path),
   Shut Down halts it.

This replaced a board reset on every PowerPC restart (seven seconds of the Pi's own firmware) on
2026-09-14. The in-place restart was verified under QEMU on Mac OS 7.6, then a shut down with the volume
flag clean (`0100`).

### Shut down

**68k.** Finder → Shut Down calls `PowerOff()` (trap `0xA05B`), which `rom_patches.cpp:1621` replaced with
`EMUL_OP_SHUTDOWN` → `QuitEmulator()`. That must call **`m68k_emulop_return()`** (`newcpu.h:281`): leaving
the interpreter takes two steps — `quit_program` is tested by the outer loop while `m68k_do_execute()` spins
in an inner loop only `SPCFLAG_BRK` interrupts. Doing one step only is a silent hang where the Mac powers off,
the emulator runs on, and `ExitAll()` — the only thing that closes the disk image — is never reached.

**PowerPC.** `QuitEmulator()` stops the tick and calls `exit_emul_ppc()`, then **`XPRAMExit()` and
`DiskExit()` only**. `ExitAll()` hangs there, measured: it closes thirteen drivers and several go through the
Macintosh, which is the interpreter just taken down. Upstream survives the same order because it exits the
process next. The two calls kept are the ones data safety depends on.

**Both.** `BoardPowerOff()` (`board_sound_circle.cpp`) then cancels the sound device and waits for its DMA to stop,
clears the frame buffer to black, asks the firmware to blank the display (property tag `0x00040002`), writes
the log out, and `halt()` stops the board. Under QEMU, `LEAVE_QEMU_ON_HALT` exits QEMU through
semihosting — which needs `-semihosting` on the QEMU command line, or the kernel halts and QEMU waits.

### The Macintosh says when it has finished starting

The `idlewait` preference (upstream's default, and ours) makes `patch_idle_time()` replace `SynchIdleTime`
in the System being booted, so the first call to `idle_wait()` is the guest announcing it has run out of
work. `main_circle.cpp` logs it once (`Macintosh idle: it has finished starting (N ms)`); `screenshot.sh` and
`run-test.sh` wait for it. `HasIdleTime()` (`patches/macemu/0005`) says whether the patch went in at all — a
System with no `SynchIdleTime` never idles. **SheepShaver has no such patch**: judge the PowerPC boot by the
screen.

`idle_wait()` is also the one moment the Macintosh is known to be between jobs, which is what anything
re-entering the Toolbox needs — telling the ROM the mouse resolution, for instance
([Input](input.md)).

## Status

- [x] Power-on sequence with the board brought up once (2026-09-05)
- [x] One image, engine switch by function call (2026-09-07)
- [x] 68k restart loop with the firmware window after every restart (2026-09-01)
- [x] PowerPC restart in place when the startup is unchanged (2026-09-14)
- [x] Clean shut down on both engines, disk closed, display blanked (2026-09-14)
- [x] Startup chime at power-on and at every restart (2026-09-14)
- [x] `Macintosh idle` boot signal on the 68k engine
- [ ] Happy Mac drawn by Okapia between power-on and the ROM's own Happy Mac
- [ ] Sad Mac with Okapia's documented error codes: ROM missing, ROM not recognised, no bootable disk,
      unreadable card, not enough memory — plus the chime of death
- [ ] Appliance start: power on → black screen → Happy Mac → Finder, no log on HDMI
- [ ] Boot signal for the PowerPC engine
- [ ] A PowerPC engine that can be entered a second time in place, so a different startup does not reset the
      board (a second `InitAll()` over a torn-down nanokernel is untested upstream)

## Pitfalls

- **`CActLED::Blink()` is two blocking delays**, 200 ms on and 500 ms off per blink (`actled.cpp:95`). One call
  per second in an event loop stopped it for seven tenths of every second; `Blink (2)` in the board's
  constructor cost 1.4 s at every power-on until 2026-09-14. Fine nowhere it runs repeatedly.
- **`ExitAll()` cannot run once the PowerPC CPU is gone** (above). A port that must reach `halt()` or
  `reboot()` calls `XPRAMExit()` and `DiskExit()` and stops.
- **A restart is visible on the PowerPC engine only through `ether_reset()`.**
- **Never cut a feature out to guard against a loop nobody has seen.** A guard that disabled the restart
  window after three quick rounds fired on the person using it — System 7.1 boots in about three seconds.
  A loop is not a brick anyway: the firmware's two seconds come round every time. The log names the pattern.
- **A failed assertion inside a member constructor is a silent hang.** Member constructors run before
  `Initialize()`, so before serial exists: `CConsole (0, &m_Serial)` violates `assert (m_pInputDevice != 0)`,
  and a USB host built as a member hung with no output. Serial first, always; anything that can fail at
  construction is built with `new` after the log.
- **The sound device must not be deleted while it plays** — `AudioExit()` runs inside `ExitAll()` *before*
  `DiskExit()`, so a crash there leaves the disk image open. See [Sound](sound.md).
- **Returning `EXIT_REBOOT` from `main()` and calling `reboot()` are different.** Circle's chain boot branch
  (`sysinit.cpp:399`) runs only on the return; `reboot()` is a watchdog reset that skips it. This mattered for
  the old chain boot and still matters for any code that relies on `main()` returning.

## Development notes

- **2026-09-01** — The firmware window runs before every start; claim, release and re-claim of the frame
  buffer through the mailbox verified, then replaced by one claim for the life of the board.
- **2026-09-05** — Engine switch by chain boot: `chain_circle.cpp` read the other kernel image and armed
  `EnableChainBoot`. The first version called `reboot()` instead of returning `EXIT_REBOOT`, so the board
  came back in the same engine, asked again, six rounds in forty seconds.
- **2026-09-07** — Chain boot removed: one image, switching is a call, one kernel on the card instead of
  three.
- **2026-09-14** — Restart freezes traced to the sound device deleted while its DMA ran (HDMI destructor
  assertion); the board now owns it. Shut down "froze" on the last frame because a halted Pi keeps scanning
  out its frame buffer: `BoardPowerOff()` clears and blanks it. PowerPC restart in place. Chime at restart.

## References

- `BasiliskII/src/rom_patches.cpp:1069`, `:1621`; `emul_op.cpp:87`; `newcpu.h:281`; `newcpu.cpp:1562`
- `SheepShaver/src/emul_op.cpp:286`; `Unix/main_unix.cpp` (upstream `Quit()`)
- Circle `lib/actled.cpp:95`, `lib/sysinit.cpp:399`, `lib/chainboot.cpp`
