# Vision

> What Okapia is for, what it targets, and what it deliberately is not.

## Goal

Run classic Mac OS **with no host operating system** on a Raspberry Pi. The kernel image on the SD card
*is* the emulator: power on, hear the chime, see the Finder — and recognise a Macintosh before
recognising a Raspberry Pi.

Two engines share one bare-metal platform layer on [Circle](https://github.com/rsta2/circle):

| Engine | Emulates | Systems |
|---|---|---|
| Basilisk II (`uae_cpu_2021`) | a 68040 Macintosh | System 7.0 → Mac OS 8.1 |
| SheepShaver (`kpx_cpu`, interpreted) | a PowerPC Macintosh | Mac OS 7.5.2 → 9.0.4 |

The virtual machine it presents:

| | |
|---|---|
| CPU | 68040 or PowerPC, interpreted — **no JIT** |
| RAM | 256 MB, the native ceiling of a Quadra 900/950 |
| Video | a virtual display card, several resolutions and all six depths, switchable live from Monitors |
| Input | USB keyboard and mouse presented as ADB devices |
| Storage | disk images on the SD card, plus a shared folder with the card itself |
| Sound | HDMI, the headphone jack or USB |
| Network | Ethernet, the Mac as a full node of the local network (not done yet) |
| Startup | power on → chime → Happy Mac → Mac OS, nothing else on screen |
| Configuration | the Okapia firmware, a boot menu in a plausible Apple style — not only a text file |

## Hardware targets

| Board | Role |
|---|---|
| **Raspberry Pi 4** | the reference: Circle's feature table is complete there. Verified on hardware since 2026-09-12 |
| **Raspberry Pi 3** | the model QEMU emulates (`raspi3b`), hence the development and regression target |
| **Raspberry Pi 5** | compatibility target: it removes control from the application (no display mode choice, no FIQ, no VCHIQ), so everything is designed to work through the generic path |

Nothing on a board beyond 2 GB helps this project: 256 MB of Mac RAM plus the rest must fit a 1 GB board.

## Principles

- **Write as little new code as possible.** Before writing a file, check macemu, Circle and circle-stdlib;
  adapt instead of reinventing. Measured payoff: the shared folder and all disk-image access were obtained
  by reusing upstream Unix files unchanged.
- **Stay close to upstream.** Okapia builds as upstream Basilisk II / SheepShaver + our Circle platform
  layer + Circle. Changes to the cores are minimal patches in `patches/`, candidates for upstreaming.
- **Data safety outranks speed and features.** One corrupted volume ends the trust that makes an emulator
  something people use daily. See [Storage](../topics/storage.md).
- **Discover, never assume.** Print what the firmware *granted*, not what was asked for; newer boards give
  the application less control, and a missing capability flips a flag instead of forcing a rewrite.
- **Measure before optimising** — but don't write obvious waste while waiting.

## Not goals

- A cycle-accurate emulation of a specific Apple machine. The ROM supplies code, not identity: the model
  announced to Mac OS is chosen to suit the System (see [68k engine](../topics/engine-68k.md)).
- A JIT. The interpreter already outruns a Quadra, and Circle marks the heap non-executable. A JIT stays a
  later, measured question (see [68k JIT study](../notes/research/jit-68k.md)).
- "No proprietary binary": Circle needs the Raspberry Pi firmware. The goal is no host *operating system*.
- Shipping Apple software. No ROM, System or disk image is ever committed or re-hosted; the user supplies
  them.

## Longer term

In rough order, and each only when the one before is stable and measured:

- **Networking** (Phase 13) and **NTP**.
- **Provisioning**: a persistent disk created on first boot, an online catalogue of Systems, a software
  library — so Okapia can be tried with nothing prepared.
- **Appliance experience**: power on → Mac OS, logs only on the card and the UART.
- Multicore: the compositor, then audio and disk, on secondary cores — see
  [Architecture](architecture.md#multicore).
- Mini vMac for System 1 → 6, shared clipboard, AppleTalk, a ready-to-flash card image, full Pi 5
  support, and only if measurements demand it, a 68k JIT.

The priority does not move:

> **a simple, stable, fast and maintainable bare-metal Macintosh.**
