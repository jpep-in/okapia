# Roadmap

> Where Okapia stands, as of 2026-09-15. This page summarises; the checkboxes, with dates and details, live in each
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
| M11 | Network and sound | sound **done** (2026-09-14); network not started |
| M12 | Macintosh sequence: chime, Happy Mac, Sad Mac and its codes | chime **done** (2026-09-14); Happy Mac and Sad Mac open |
| M13 | Okapia firmware: configuration with the mouse, no text file | **done** (2026-09-09) |
| M14 | Provisioning: usable with nothing prepared | not started |
| M15 | Appliance: power on → Mac OS | open |
| — | Second engine: SheepShaver in the same image | **done** (2026-09-07); Mac OS 8.6 CD install milestone open |

## By topic

| Topic | Done | Open | Most important next |
|---|---:|---:|---|
| [Startup and shutdown](../topics/startup-and-shutdown.md) | 7 | 5 | Happy Mac and Sad Mac with documented error codes; appliance start |
| [Boot menu](../topics/boot-menu.md) | 10 | 4 | Repair dialog instead of a silent scavenge |
| [68k engine](../topics/engine-68k.md) | 7 | 6 | Understand the System 7.1 crash (unredirected ASC write hypothesis); remaining bottleneck ranks |
| [PowerPC engine](../topics/engine-powerpc.md) | 11 | 7 | Mac OS 8.6 installs from its CD; a `kpx_cpu` bottleneck study (`Graf` at 63% of a 6100/60) |
| [Display](../topics/display.md) | 9 | 7 | Vertical sync; compositor on core 2 once measured |
| [Sound](../topics/sound.md) | 8 | 8 | System 7.1 sound (Sound Manager 3); hear the Quadra and IIci chimes on a Pi |
| [Input](../topics/input.md) | 7 | 5 | `mousedpi` measured for a real mouse on the Pi 4 |
| [Storage](../topics/storage.md) | 9 | 11 | Error cases (full card, SD write error, removal); FatFs fast seek |
| [Shared folder](../topics/shared-folder.md) | 6 | 4 | A hint when the System needs the FSM 1.2 extension |
| [Clock and PRAM](../topics/clock-and-pram.md) | 9 | 4 | Clock writes from the Mac; an RTC |
| [Network](../topics/network.md) | 0 | 7 | `ether_circle.cpp` sharing the Pi's MAC address |
| [Build](../contributing/build.md) | 5 | 3 | Continuous integration without Apple ROMs |
| [Testing and debugging](../contributing/testing-and-debugging.md) | 4 | 5 | Automated QEMU boot check in CI; a PowerPC boot signal |
| [Benchmarks](../contributing/benchmarks.md) | 3 | 7 | Five runs per configuration; attribute the seven changes |

## Known defects

Found and not yet fixed — each is tracked in its topic:

- **System 7.1: no sound, and one crash with odd sound from the jack** ([Sound](../topics/sound.md),
  [68k engine](../topics/engine-68k.md)).

## Proposals under consideration

- Sound output chosen automatically from the display's EDID.
- `NO_CALIBRATE_DELAY` to save about 0.66 s at power-on (Circle calibrates its delay loop at boot).
- Keep the early boot and chime lines readable after a restart when the log wraps.
- System 6 in 24-bit addressing as a third engine configuration ([study](../notes/research/system6-24bit.md)).
- Multicore S1 then S2 ([Architecture](architecture.md#multicore)).

## Out of scope unless measurements demand it

A 68k JIT ([study](../notes/research/jit-68k.md)), NVMe, Mini vMac for System 1 to 7.5, a CRT mode. Later candidates:
QuickDraw acceleration on the 68k, a shared clipboard, a ready-to-flash SD image, full Pi 5 support.
