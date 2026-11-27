<!-- Copyright (C) 2026  Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Roadmap

> Where Okapia stands, as of 2026-11-24. This page summarises; the checkboxes, with dates and details, live in each
> topic's **Status** section and nowhere else. Update the topic first, then the line here.

## Milestones

| | Milestone | State |
|---|---|---|
| M0 | Complete local workstation, repository that builds | **done** (2026-08-25) |
| M1 | Circle under QEMU: frame buffer, input, SD, GDB | **done** (2026-08-26) |
| M2 | The same binary on real hardware | **done** on a Pi 4 (2026-09-12); Pi 3 and Pi 5 not tried |
| M3 | ROM loaded, 68040 running | **done** (2026-08-26) |
| M4 | Mac disk visible | **done** (2026-08-26) |
| M5 | First Mac pixels | **done** (2026-08-26) |
| M6 | **Usable Finder, under QEMU and on hardware** | **done** (QEMU 2026-08-26, Pi 4 2026-09-12) |
| M7 | Shared folder working | **done** (2026-08-29) |
| M8 | Clock, PRAM, clean shutdown | **done** (2026-08-29; shutdown display blanking 2026-09-14) |
| M9 | Reference measurements | **done**: 68k Quadra-class with margin, PowerPC close to a 6100/60 (2026-09-13) |
| M10 | Optimised video, no tearing | partly: dirty tiles and whole-frame drawing done; vertical sync and a double buffer open |
| M11 | Network and sound | sound **done** (2026-09-14); network by Wi-Fi working on the Pi 4, Mamba at about 140 KB/s (2026-11-11); cable not yet tried on a Pi |
| M12 | Macintosh sequence: startup chime | **done** (2026-09-14); Happy Mac and Sad Mac out of scope (2026-09-17) |
| M13 | Okapia firmware: configuration with the mouse, no text file | **done** (2026-09-09) |
| M14 | Guest provisioning: usable without a prepared guest disk | catalogue and automatic guest provisioning not started; desktop workspace/card preparation is implemented separately |
| M15 | Pi appliance polish and full hardware validation | open; direct boot without a host OS already works |
| — | Second engine: SheepShaver in the same image | **done** (2026-09-07); Mac OS 8.6 CD install milestone open |
| — | 68k translator (`OKAPIA_JIT_ARM64=1`) | **experimental**: 5.4× the interpreter's CPU score on a Pi 4, FPU translated (2026-11-16); default once proved safe |
| — | Desktop host: the same menu and both Macintosh engines on macOS | **implemented** (2026-11-21–24); guest audio/network and manual restart validation remain open ([Desktop](../topics/desktop.md#status)) |
| — | Integrated card installer and release announcements | **implemented**, folder tests pass; real-card preparation, macOS signing, first public release and app self-replacement remain open ([Releases](../contributing/release.md)) |
| — | User-facing website on GitHub Pages | **built locally**, Mac/Pi paths and guides checked; integration and publication pending ([Website](../contributing/website.md#status)) |

## By topic

| Topic | Done | Open | Most important next |
|---|---:|---:|---|
| [Startup and shutdown](../topics/startup-and-shutdown.md) | 7 | 4 | The 7.6.1 → 7.1.2 restart freeze; appliance start |
| [Boot menu](../topics/boot-menu.md) | 17 | 7 | Open the menu with the reason when the Macintosh cannot start; repair dialog |
| [68k engine](../topics/engine-68k.md) | 16 | 10 | The translator's remaining safety checks (System 7.1, five restarts, `run-test.sh` on the Pi); the System 7.1 crash |
| [PowerPC engine](../topics/engine-powerpc.md) | 11 | 7 | Mac OS 8.6 installs from its CD; a `kpx_cpu` bottleneck study (`Graf` at 63% of a 6100/60) |
| [Display](../topics/display.md) | 15 | 10 | Vertical sync; compositor on core 2 once measured |
| [Sound](../topics/sound.md) | 9 | 8 | Understand the System 7.1 crash; hear the Quadra and IIci chimes on a Pi |
| [Input](../topics/input.md) | 7 | 5 | `mousedpi` measured for a real mouse on the Pi 4 |
| [Storage](../topics/storage.md) | 9 | 11 | Error cases (full card, SD write error, removal); FatFs fast seek |
| [Shared folder](../topics/shared-folder.md) | 6 | 4 | A hint when the System needs the FSM 1.2 extension |
| [Clock and PRAM](../topics/clock-and-pram.md) | 14 | 3 | RTC; NTP by cable; validate clock setting from the guest control panel |
| [Network](../topics/network.md) | 12 | 12 | Why Wi-Fi drops while the Mac starts; DHCP by cable; a group rekey |
| [Build](../contributing/build.md) | 5 | 3 | Continuous integration without Apple ROMs |
| [Testing and debugging](../contributing/testing-and-debugging.md) | 5 | 5 | Automated QEMU boot check in CI; a PowerPC boot signal |
| [Benchmarks](../contributing/benchmarks.md) | 7 | 10 | Five runs per configuration; OkapiaBench's FPU tests on the Pi 4 |
| [Desktop](../topics/desktop.md) | 18 | 7 | Validate guest sound/network, restarts and the disk flush policy; Linux build |

## Distribution

One application serves desktop users and prepares Pi cards; the installer is not a separate download in the new
user path. R1, R2, R4 and R5 are implemented; R3 (signing/notarisation), R6 (app replacement) and R7 (emulator CI)
remain open. The website's prepared CI only checks/builds the site. No public application release or website
publication is recorded yet. Details live in [Releases](../contributing/release.md) and [Website](../contributing/website.md).

## Known defects

Found and not yet fixed — each is tracked in its topic:

- **System 7.1: one crash with odd sound from the jack** ([Sound](../topics/sound.md),
  [68k engine](../topics/engine-68k.md)); System 7.0 and 7.1 have no sound without the Sound Manager 3
  extension.
- **A restart from System 7.6.1 to 7.1.2 froze in the ROM** ([Startup and shutdown](../topics/startup-and-shutdown.md)).

## Proposals under consideration

- Sound output chosen automatically from the display's EDID.
- `NO_CALIBRATE_DELAY` to save about 0.66 s at power-on (Circle calibrates its delay loop at boot).
- Keep the early boot and chime lines readable after a restart when the log wraps.
- System 6 in 24-bit addressing as a third engine configuration ([study](../notes/research/system6-24bit.md)).
- Multicore S1 then S2 ([Architecture](architecture.md#multicore)).

## Out of scope unless measurements demand it

NVMe, a Happy Mac or Sad Mac drawn by Okapia (the ROM already draws
its own; a Sad Mac would mean simulating a hardware failure), Mini vMac for System 1 to 7.5, a CRT mode. Later candidates:
A shared clipboard, a ready-to-flash SD image and full Pi 5 support. The experimental 68k translator already
exists; it belongs to validation and measurement, not to this unimplemented list. See
[68k engine](../topics/engine-68k.md#status) and the
[Pi translator measurements](../contributing/benchmarks.md#raspberry-pi-4-with-the-68k-translator-2026-11-15).
QuickDraw acceleration is set aside on `wip-quickdraw` ([Decisions](decisions.md#quickdraw-acceleration-set-aside)).
