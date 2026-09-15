# Clock and PRAM

> Where the Macintosh's time comes from, why it never goes backwards, and how its parameter RAM survives a pulled
> plug. Main files: `src/kernel/kernel.cpp` (`ApplyTimeZone`), `src/circle/card_circle.cpp` (`CardRefineClock`),
> `src/circle/hfs_volume_circle.cpp` (`HfsDateIsPlausible`), `src/circle/timer_circle.cpp`,
> `src/circle/xpram_circle.cpp`, `src/circle/emul_op_hook_circle.cpp`, `src/circle/sheepshaver/cpu_ticks_circle.cpp`,
> `src/firmware/circle/okapia_firmware.cpp` (`ForgetPram`).

## Using it

- **Time zone**: `timezone`, in minutes east of UTC ([Preferences](../preferences.md)). A Mac of this era has no
  time zone — its clock *is* local time — while Circle keeps UTC, so without it a Pi in Paris shows two hours behind.
- **The time at startup** is the latest of the kernel's build time and the most recent modification date of any
  volume on the card. The log says which: `Clock: Aug 29 12:17:55, from /boot71.img — later than the build time`.
- **Setting the date in the Date & Time control panel** holds for the session only (see Status).
- **Parameter RAM** (startup disk, sound volume, mouse tracking, desktop pattern) is kept per engine on the card and
  written the moment it changes. **Command-Option-P-R** at power-on forgets the parameter RAM of the Macintosh about
  to start — the engine the startup volume needs — and leaves the other's alone. **Forget the parameter RAM** in the
  [boot menu](boot-menu.md) asks which: 68k, PowerPC or both.

## How it works

### One chain, one decision point

1. **Source**: `OKAPIA_BUILD_TIME`, set by the Makefile (`date +%s`, a UTC instant). A Pi 4 has no RTC and NTP needs
   the network.
2. **Decision**: `ApplyTimeZone()` in `CKernel::Initialize()` sets the zone **then** the time (`SetTime` stores local
   seconds, so setting the zone after would move the log's time and not the Mac's). A zone the timer refuses puts
   it back to UTC. `CardRefineClock()` then raises the time, on whichever engine starts.
3. **Readers**: the Mac through `TimerDateTime()` (`timer_circle.cpp`), FatFs through `get_fattime()`
   (`ffsystem.cpp`). Both read Circle's clock, so the menu bar and the dates on the card cannot diverge. Without a set
   clock, `get_fattime()` returned zero and every file the Mac created was dated `1980-00-00`, not even a valid date.

### The clock never goes backwards

A clock that recedes between boots produces volumes whose `drLsMod` precedes `drCrDate` — measured once as
`drLsMod` 2082844811 against `drCrDate` 3612702325, when the clock started at zero and the Mac stamped volumes in
1904 — which is exactly what `fsck_hfs` calls "MDB needs minor repair". So `CardRefineClock()` takes
**max(build time, the clock already running, the latest plausible `drLsMod` on the card)**:

- `drLsMod` comes from the inventory (`hfs_vstat`'s `mddate`). Inside libhfs `d_ltime` is only an offset of
  2082844800, because `HAVE_MKTIME` is never defined here and `tzdiff` stays 0 (`data.c:456`, `:467`).
- **Frames**: `drLsMod` is **local** time, `OKAPIA_BUILD_TIME` is **UTC**; the build time is brought to local
  with the zone the timer holds (`GetTimeZone()`) before the comparison, or the result is an hour or two out.
- **Plausible** (`HfsDateIsPlausible()`): below the saturated value, and no more than ten years past the build. A
  32-bit `drLsMod` of `$FFFFFFFF` is 2040-02-06 06:28:15, the last second of the Macintosh's clock (Inside
  Macintosh: *Operating System Utilities*), and it reads as a date. Since the clock never goes back, one bad date
  accepted would hold it there for good; one good date refused only costs a clock that starts at the build time.
- The running clock is part of the floor because `CardRefineClock()` runs again at every restart, minutes into a
  session, where a floor of the build time alone would wind it back.
- It runs after the inventory and **before** the repair, the shared folder and `InitAll()`, in both kernels
  (`StartMacintosh()` and `CKernelPPC::Run()`): a repair stamped with the wrong date is what this step exists to
  prevent.

Verified with `OKAPIA_BUILD_TIME=1735689600` (1 January 2025): the clock started at `Jan 1 02:00`, then
`Clock: Aug 29 12:17:55, from /boot71.img`.

### Parameter RAM

`xpram_circle.cpp` replaces `xpram_dummy.cpp`, which named its file relatively (it landed at the root by accident).

- **One file per engine**: `/BasiliskII_XPRAM` (256 bytes) and `/SheepShaver_XPRAM` (8,192 bytes), whose fields are at
  different offsets. It reads one byte more than it wants, so a *longer* file is refused rather than accepted as a
  short read: one shared file would have let each machine boot with the other's settings, silently. The preferences
  stay engine-agnostic; the PRAM is the Mac's.
- **A short file is refused**: the last write did not complete, and starting from an empty PRAM is what a Mac with a
  dead battery does.
- **Written the moment it changes**, not at clean shutdown as upstream does. `XPRAMWatchdog()` compares against what
  is on the card and writes only on a change. It is **never called from the tick**, which runs at IRQ level where
  blocking on the SD card is not allowed:
  - 68k: from the event itself. `M68K_EMUL_OP_CLKNOMEM` is the one opcode through which XPRAM changes, and the
    `--wrap` on `EmulOp` in `emul_op_hook_circle.cpp` listens for it.
  - PowerPC: from the periodic seam `powerpc_check_ticks()` (`sheepshaver/cpu_ticks_circle.cpp`).
- **Zap**: `main.cpp:106` rebuilds the PRAM as soon as the `NuMc` signature is missing, so removing the file *is* the
  zap, and it touches no user data. The firmware uses `f_unlink`, not `remove()`: newlib's `remove()` left something
  behind that wedged the next `fopen` — the ROM never opened and the kernel stopped with no log.
- **Which file**: the firmware serves both engines, so `ForgetPram()` is told. Command-Option-P-R removes the file of
  `FirmwareWantedEngine()`, the engine the chooser's startup volume needs, as a real Macintosh forgot its own PRAM.
  The boot menu's alert marks that engine first and offers the other and both.

## Status

- [x] Build time in Circle's clock; source logged at startup (2026-08-29)
- [x] `timezone`, applied before `SetTime()` (2026-08-29)
- [x] Floor from the card's latest `drLsMod` (2026-08-29)
- [x] PRAM on the card, written on change, short file refused (2026-08-29)
- [x] PRAM written from the Mac's own access on the 68k engine (2026-09-04)
- [x] PRAM written from the PowerPC seam; one file per engine (2026-09-08)
- [x] 2040 guard: a saturated `drLsMod` and any date more than ten years past the build are refused; host test in
      `check_platform.cpp`, verified under QEMU (`implausible (2212122495), ignored`) (2026-09-15)
- [x] Clock floor on the PowerPC path: `CardRefineClock()` in both kernels; verified under QEMU, `card-ppc: Clock:
      … from /machd76.image` (2026-09-15)
- [x] Command-Option-P-R forgets the PRAM of the Macintosh about to start; the boot menu asks 68k, PowerPC or both
      (2026-09-15)
- [ ] **Clock writes from the Mac**: upstream ignores writes to the RTC registers (`emul_op.cpp:181`). The hook
      exists without a patch: `ClkNoMem` ($A053) is patched to `M68K_EMUL_OP_CLKNOMEM` on our ROM
      (`rom_patches.cpp:1188`), already wrapped. Capture `d1`/`d2` **on entry** (the handler ends with
      `r->d[1] = r->d[2]`), rebuild the four bytes written one by one, and store to an RTC, else a file on the card.
      That explicit value must take precedence over the `drLsMod` floor, or setting the clock back on purpose would
      not survive a restart. This is why the boot menu has no date panel
- [ ] **RTC**: Circle's `addon/rtc` has `CRealTimeClock` (`Get`/`Set`, UTC), `CFirmwareRTC` (the Pi 5's own, finds
      `/soc/rpi_rtc` in the device tree and answers `FALSE` elsewhere; needs a battery on J5) and `CMCP7941X` (an I²C
      module, the only way on a Pi 3 or 4). `librtc.a` would join `LIBS` like `libsound.a`. Preference `rtc`
      (`auto`/`off`)
- [ ] **NTP** after the [network](network.md): `CNTPClient`/`CNTPDaemon` (example `18-ntptime`). Its right use is
      writing the RTC, not the display, and **never on the boot's critical path**: in the background after the Mac
      starts, bounded to two or three seconds if ever before. Preference `ntp` (server, empty for none). On a
      Pi 1/2/3 a polling daemon competes with USB input
- [ ] Every future source goes through the same floor

## Pitfalls

- **Two mechanisms for one decision drift.** `TimerDateTime()` once kept its own fallback to the build time,
  inherited from when Circle's clock stayed at zero; it now only reads.
- **QEMU has no RTC to offer**: its machines stop at `raspi4b` (BCM283x/2711, none with an RTC) and there is no
  `raspi5`. The RTC branch cannot be exercised without hardware; the chain must fall through and say so.
- **The Mac reads its clock before any network stack exists**, so the startup time always comes from Okapia; the
  built-in NTP client only arrives with Mac OS 8.5 (earlier Systems need Vremya, Network Time or similar).
- **Anything periodic that touches the card belongs in the engine's seam, never in the tick** (IRQ level).
- **A `BasiliskII_XPRAM` dated with the build time of the kernel that wrote it** was the shortest demonstration of why
  the clock chain mattered.
- **A guard against the unrepresentable lets the saturated value through**: `$FFFFFFFF` is representable, it is the
  last second HFS can hold, and a guard written as `nWhen > HFS_LAST_DATE` accepted it and kept the clock at
  2040-02-06. Refuse the implausible, not only the impossible.
- **Two ceilings, not one**: the Macintosh's clock and HFS dates end at 2040-02-06 06:28:15 (32-bit seconds since
  1904, Inside Macintosh), while the Date & Time control panel of System 7 and Mac OS 8 only lets a year from 1920
  to 2019 be typed, a limit of the control panel fixed in Mac OS 9.0.4 (Apple Wiki, *Date & Time control panel*;
  archive.org, *MacOS 8 - Y2K20 Bug*). How each System displays a clock set past 2019 by Okapia has not been
  looked at here.

## Development notes

- **2026-08-29** — Clock, time zone, `drLsMod` floor and PRAM in one day. A periodic PRAM write from
  `VideoInterrupt()` (the only periodic call in the 68k thread at the time) came first, then moved to the
  `CLKNOMEM` event on 2026-09-04.
- **2026-09-08** — PowerPC seam; separate PRAM files for the two engines.
- **2026-09-15** — The clock floor moved to `card_circle.cpp` and runs on both engines; the 2040 guard refuses the
  saturated date; the PRAM zap chooses its engine.

## References

- `BasiliskII/src/emul_op.cpp:181`, `rom_patches.cpp:1188`, `main.cpp:106`
- libhfs `data.c:456`, `:467`
- Inside Macintosh: *Operating System Utilities*, "The Date-Time Record"
  (`developer.apple.com/library/archive/documentation/mac/OSUtilities/OSUtilities-94.html`)
- `apple.fandom.com/wiki/Date_%26_Time_control_panel`; `archive.org/details/macos_8_2020_bug`
- Circle `addon/rtc/`, `include/circle/net/ntpclient.h`, `sample/18-ntptime`
