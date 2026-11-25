<!-- Copyright (C) 2026  Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Clock and PRAM

> Where the Macintosh's time comes from — the network, the Macintosh's own Date & Time, the card, the build — how
> it reaches the Macintosh, and how its parameter RAM survives a pulled plug. Main files:
> `src/hosts/circle/clock_circle.cpp` (the board's clock and its sources), `src/hosts/circle/net_clock.cpp` (SNTP in raw frames),
> `src/firmware/okapia_clock.cpp` (zones, summer time, the date in words), `src/common/card_okapia.cpp`
> (`CardRefineClock`), `src/common/tick_okapia.cpp` (the Mac's `Time` global),
> `src/common/hfs_volume_okapia.cpp` (`HfsDateIsPlausible`), `src/common/timer_okapia.cpp`,
> `src/common/xpram_okapia.cpp`, `src/common/emul_op_hook_okapia.cpp`, `src/common/sheepshaver/cpu_ticks_okapia.cpp`,
> `src/common/okapia_menu.cpp` (`ForgetPram`).

## Using it

**On desktop**, the Macintosh follows the computer's clock and zone; Date & Time reports that host time. Okapia
does not run the Pi's NTP client there (`src/hosts/desktop/okapia_host_desktop.cpp:2074`). Parameter RAM still lives
per engine in the workspace. Desktop zone override is not implemented ([Desktop status](desktop.md#status)).
The network time, saved board clock and zone controls below describe **Pi** operation.

- **The Date & Time group of the boot menu's Settings** shows the time, where it came from and the offset in
  force, and sets the two things a Macintosh of this era cannot be told ([Boot menu](boot-menu.md#using-it)).
- **Time zone**: `zone`, one of 22 cities with their summer time (`paris`, `london`, `newyork`…,
  [Preferences](../preferences.md)). A Mac of this era has no time zone — its clock *is* local time — while the
  network answers in UTC. The older `timezone` (minutes east of UTC, no summer time) still works when `zone` is
  empty, and the menu offers it as itself until a city replaces it.
- **Time from the network**: `ntp`, a server name or address, `pool.ntp.org` by default, empty for none. Asked a few
  seconds after the link comes up — in the boot menu or while the Macintosh starts, never holding it up — and once
  a day after that. The log says what came of it:
  `Clock: Nov 12 16:05:31, from pool.ntp.org (162.159.200.1, stratum 2, +4 s)`.
- **Setting the clock by hand**: untick the time from the network and use the Macintosh's Date & Time control panel,
  on either engine. Okapia keeps it on the card (`/Okapia_Clock`) and starts from it again next time:
  `Clock: Jan  1 00:00:00, set on the Macintosh (-216660036 s), kept on the card`. With the network ticked, the next
  answer from the server wins.
- **Without the network**, the time at startup is the latest of the kernel's build time and the most recent
  modification date of any volume on the card. The log says which: `Clock: Aug 29 12:17:55, from /boot71.img —
  later than the build time`.
- **Parameter RAM** (startup disk, sound volume, mouse tracking, desktop pattern) is kept per engine on the card and
  written the moment it changes. **Command-Option-P-R** at power-on forgets the parameter RAM of the Macintosh about
  to start — the engine the startup volume needs — and leaves the other's alone. **Forget the parameter RAM** in the
  [boot menu](boot-menu.md) asks which: 68k, PowerPC or both.

## How it works

### One chain, one decision point

One clock for the board, Circle's, kept in **local** time (`CTimer::m_nTime`), and one module that decides it,
`clock_circle.cpp`, compiled once for both engines (`SHARED_SRCS`). Sources, in order of trust:

1. **The network** (NTP), once the interface is up: the truth. It sets the clock and nothing on the card is asked
   after it (`ClockSynced()`).
2. **The clock set on the Macintosh**, kept on the card, as long as nothing on the card is later (below).
3. **The latest plausible `drLsMod`** on the card (`CardRefineClock()`, below).
4. **`OKAPIA_BUILD_TIME`**, set by the Makefile (`date +%s`, a UTC instant). A Pi 4 has no RTC.

- **Startup**: `ClockInitialize()` in `CKernel::Initialize()` sets the zone **then** the time (`SetTime` stores local
  seconds, so setting the zone after would move the log's time and not the Mac's). A zone the timer refuses puts
  it back to UTC. `CardRefineClock()` then raises the time, on whichever engine starts.
- **Readers**: the Mac through `TimerDateTime()` (`timer_okapia.cpp`) and its `Time` global (below), FatFs through
  `get_fattime()` (`ffsystem.cpp`). All read Circle's clock, so the menu bar and the dates on the card cannot
  diverge. Without a set clock, `get_fattime()` returned zero and every file the Mac created was dated
  `1980-00-00`, not even a valid date.
- **Where it came from** is remembered (`ClockNoteSource()`, `ClockSource()`) for the boot menu's page.

### The Macintosh's `Time` global

Mac OS reads the date from the low-memory global `Time` (`$20C`): `GetDateTime` is that global and nothing more. A
real Macintosh advances it from the RTC's one-second interrupt; Basilisk II and SheepShaver emulate no such chip,
so every upstream port writes it from the host's clock once a second (`Unix/main_unix.cpp:1295`, SheepShaver
`Unix/main_unix.cpp:1577`, `WriteMacInt32(0x20c, TimerDateTime())`). **Okapia did not** until 2026-11-12, and
nothing else in either emulator writes it (a search of both trees finds only those host ports); SheepShaver reads no
RTC through Basilisk's path at all. How each System behaved without it was not looked at before the fix.
`OneSecond()` in `tick_okapia.cpp` now writes it, through
`ClockMacSecond()` — at IRQ level, a 32-bit aligned store and nothing else.

The same look says when the Macintosh wrote it itself. Date & Time sets this global (and the RTC, whose write
upstream drops, `emul_op.cpp:181`): a value more than two seconds away from the last one written, plausible
(`ClockMacPlausible()`: from 1990, below `$FFFFFFFF`) and seen after the first ten seconds of a start (a Macintosh
starting clears its low memory), is a clock somebody set. The interrupt only records it; `ClockPoll()`, from each
engine's seam, sets Circle's clock to it and writes the record. One test serves both engines, where decoding the
RTC's bytes through `ClkNoMem` would have served the 68k one alone.

### The clock set on the Macintosh, kept

Without an RTC, the clock at startup is the latest date the card knows — and a clock set back on purpose would lose
to that at every start. So `/Okapia_Clock` keeps one line, `<set> <floor>\n`: what the clock said (local seconds),
and the latest date the card could hold when it was set — the clock just before. At startup, `CardRefineClock()`
believes the record as long as no volume is later than that floor; a later volume means somebody has lived in a
later time since, and the record is forgotten. The record is brought up to the current time when the board stops
or restarts (`ClockSessionEnd()`, from `BoardPowerOff()` and the reboot path), so a session does not start again
from the moment the clock was set; a pulled plug loses that session's time and nothing else. A short or malformed
file is refused, as the PRAM's is. An answer from the network forgets it.

### Time zones and summer time

`okapia_clock.cpp` (pure, host-tested): 22 zones, each a standard offset and one of four rules — the European Union
(01:00 UTC, last Sundays of March and October), the United States and Canada (02:00 local, second Sunday of March,
first of November), south-east Australia (first Sundays of October and April) and New Zealand (last Sunday of
September, first of April); zones without summer time today have none. `ClockPoll()` looks twice a minute and moves
the offset, keeping the instant (`SetOffset()`: the UTC instant is read with the old offset, written back with the
new one). The Macintosh sees its clock jump an hour, as a Macintosh whose owner changed it did.

### Time from the network

Circle's own client (`CNTPClient`, `CNTPDaemon`) needs Circle's TCP/IP stack, which would take the Macintosh's frames
for itself: the Macintosh shares the Pi's MAC and Okapia runs no stack ([Network](network.md)). So `net_clock.cpp`
asks in raw frames, the fewest that can ask: a DHCP lease (DISCOVER, REQUEST, broadcast answers), ARP for the next
hop, a DNS query for the server's name, and one SNTP exchange (RFC 4330). It is pure — the caller hands it the time
in milliseconds and the frames — so `check_net` runs a whole exchange against a fake router.

- **Told apart from the Macintosh's frames** by a DHCP transaction number, a random UDP port of its own for DNS and
  NTP, and a nonce in the transmit timestamp that the server echoes back. `BoardNetPoll()` offers each frame to
  `ClockNetTake()` before the Macintosh's ring; ARP is answered (the router asking who has our address) but always
  passed on. The report counts them: `rx … (… to the Mac, … EAPOL, … clock)`.
- **The lease is never released**: the Macintosh's DHCP uses the same MAC, so its lease is this one.
- **An answer is believed** when it echoes the nonce, comes from the address asked, says mode 4, version 3 or 4, a
  stratum from 1 to 15, and is not unsynchronised (leap 3). A kiss of death (stratum 0) moves on to the next address
  the name gave. The time is the server's transmit time plus half the round trip.
- **Era**: an NTP second with its top bit clear is taken to be in era 1 (from 7 February 2036), which holds until 2104
  — past the Macintosh's own end in 2040.
- **Timing**: asked three seconds after the link says it is up (a Wi-Fi link finishes its handshake after), then
  once a day; a failure waits one minute, then two, up to thirty. One line in the log per attempt that fails.
- **Default server**: the NTP Pool (`pool.ntp.org`), run by volunteers for exactly this kind of device, chosen over
  a company's servers on purpose.

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

`xpram_okapia.cpp` replaces `xpram_dummy.cpp`, which named its file relatively (it landed at the root by accident).

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
    `--wrap` on `EmulOp` in `emul_op_hook_okapia.cpp` listens for it.
  - PowerPC: from the periodic seam `powerpc_check_ticks()` (`sheepshaver/cpu_ticks_okapia.cpp`).
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
- [x] **The Mac's `Time` global written once a second**, as upstream does; verified under QEMU on System 7.1, the
      global advancing a second a second (2026-11-12)
- [x] **Clock set on the Macintosh kept**: detected in `Time` on both engines, applied to the board's clock, kept in
      `/Okapia_Clock` with its floor. Verified under QEMU by writing 1 January 2020 into `Time` through the GDB stub:
      one detection, the clock running on from it, the record on the card, and the next start `as set on the
      Macintosh` (2026-11-12). Not yet tried from the control panel itself, on either engine
- [ ] **RTC**: Circle's `addon/rtc` has `CRealTimeClock` (`Get`/`Set`, UTC), `CFirmwareRTC` (the Pi 5's own, finds
      `/soc/rpi_rtc` in the device tree and answers `FALSE` elsewhere; needs a battery on J5) and `CMCP7941X` (an I²C
      module, the only way on a Pi 3 or 4). `librtc.a` would join `LIBS` like `libsound.a`. Preference `rtc`
      (`auto`/`off`)
- [x] **NTP** in raw frames (DHCP, ARP, DNS, SNTP), in the background, never on the boot's path; preference `ntp`,
      `pool.ntp.org` by default; a whole exchange checked on the host against a fake router (2026-11-12)
- [x] NTP tried on the Pi 4 over Wi-Fi: the time from the pool, the zone and summer time right (2026-11-12)
- [ ] NTP by cable on the Pi 4; QEMU's USB network delivers no frame, so it cannot be tried there
- [x] Time zones with summer time (`zone`), every transition of 2026 checked to the second on the host; the boot
      menu's Date & Time group (2026-11-12); tried on the Pi 4 (2026-11-12)
- [ ] Every future source goes through the same floor

## Pitfalls

- **Two mechanisms for one decision drift.** `TimerDateTime()` once kept its own fallback to the build time,
  inherited from when Circle's clock stayed at zero; it now only reads.
- **QEMU has no RTC to offer**: its machines stop at `raspi4b` (BCM283x/2711, none with an RTC) and there is no
  `raspi5`. The RTC branch cannot be exercised without hardware; the chain must fall through and say so.
- **The Mac reads its clock before any network stack exists**, so the startup time always comes from Okapia; the
  built-in NTP client only arrives with Mac OS 8.5 (earlier Systems need Vremya, Network Time or similar).
- **Anything periodic that touches the card belongs in the engine's seam, never in the tick** (IRQ level).
- **Nothing in either emulator advances `Time`**: every upstream host port writes it once a second, and a port
  that forgets inherits no error and no warning, only a Macintosh whose clock is not kept.
- **A detected change must become the reference at once.** The first version compared each second with the value
  written before the change, found the same setting again every second, and held the clock on that one second for
  good — visible only by running it (`Jan  1 00:00:00.09 … 00:00:00.26`, a dozen settings in the same second).
- **Circle's NTP needs Circle's stack**, and Circle's stack would take the Macintosh's frames: on a shared MAC the
  question has to be asked by hand.
- **Do not release the lease**: the Macintosh shares the MAC, so a DHCPRELEASE would give its address away.
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
- **2026-09-15** — The clock floor moved to `card_okapia.cpp` and runs on both engines; the 2040 guard refuses the
  saturated date; the PRAM zap chooses its engine.
- **2026-11-12** — `clock_circle.cpp`: the network, the clock set on the Macintosh and summer time. `ApplyTimeZone()`
  became `ClockInitialize()`. The Mac's `Time` global, never written until then, is written once a second, and the
  same look detects Date & Time. Decoding the RTC bytes through `ClkNoMem`, the plan until then, was dropped: it
  served the 68k engine alone.

## References

- `BasiliskII/src/emul_op.cpp:181`, `rom_patches.cpp:1188`, `main.cpp:106`
- libhfs `data.c:456`, `:467`
- Inside Macintosh: *Operating System Utilities*, "The Date-Time Record"
  (`developer.apple.com/library/archive/documentation/mac/OSUtilities/OSUtilities-94.html`)
- `apple.fandom.com/wiki/Date_%26_Time_control_panel`; `archive.org/details/macos_8_2020_bug`
- Circle `addon/rtc/`, `include/circle/net/ntpclient.h`, `sample/18-ntptime`
- `BasiliskII/src/Unix/main_unix.cpp:1295` and `SheepShaver/src/Unix/main_unix.cpp:1577` (the `Time` global)
- RFC 4330 (SNTP v4), RFC 2131 and 2132 (DHCP), RFC 826 (ARP), RFC 1035 (DNS); the NTP Pool, `www.ntppool.org`
- Summer time: EU Directive 2000/84/EC; US Energy Policy Act of 2005, section 110
