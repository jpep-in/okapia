<!-- Copyright (C) 2026  Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Vision

> What Okapia is for, what it targets, and what it deliberately is not.

## Goal

Run classic Mac OS **on a Mac or directly on a Raspberry Pi**, with one coherent Macintosh experience. On macOS,
Okapia is an application; on a Pi, the kernel image on the SD card is the emulator, with no host operating system.
Both show the same boot menu and start the System on the chosen disk.

The desktop application also prepares and updates Pi cards. A workspace is the folder holding the ROMs, disks,
preferences and shared files; a Pi card uses that same layout. See [Desktop](../topics/desktop.md) and the
user guides for [Mac](../using/mac.md) and [Pi](../using/pi.md).

Two engines share `src/common/` and the firmware, with host implementations for SDL3/POSIX and
[Circle](https://github.com/rsta2/circle) ([Architecture](architecture.md#layers)):

| Engine | Emulates | Systems |
|---|---|---|
| Basilisk II (`uae_cpu_2021`) | a 68040 Macintosh | System 7.0 → Mac OS 8.1 |
| SheepShaver (`kpx_cpu`, interpreted) | a PowerPC Macintosh | Mac OS 7.5.2 → 9.0.4 |

The virtual machine it presents:

| | |
|---|---|
| CPU | 68040 or PowerPC. Pi builds interpret by default; an experimental 68k translator exists (`OKAPIA_JIT_ARM64=1`). The desktop arm64 build includes it, enabled at runtime with `jit true` ([Desktop](../topics/desktop.md#how-it-works)) |
| RAM | 256 MB, the native ceiling of a Quadra 900/950 |
| Video | a virtual display card, several resolutions and all six depths, switchable live from Monitors |
| Input | keyboard and mouse presented as ADB devices: USB on Pi, SDL events on desktop |
| Storage | disk images in a workspace or on the SD card, plus a shared folder |
| Sound | Pi: HDMI, headphone jack or USB. Desktop: host audio through SDL3; guest sound validation remains open ([Desktop](../topics/desktop.md#status)) |
| Network | Pi: a node on the local network; Wi-Fi tested, cable still to validate. Desktop: shared host connection through slirp; guest traffic validation remains open |
| Startup | Pi: power on → chime → Mac OS. Desktop: choose or create a workspace, then the same boot menu and Macintosh |
| Configuration | the Okapia firmware, a boot menu in a plausible Apple style — not only a text file |

## Hardware targets

**Desktop:** macOS first, developed on Apple silicon. Linux has partial host support but no validated build;
Windows and Intel Mac distribution are future work. Building an application is distinct from distributing it:
Developer ID signing, notarisation and the first public release remain open ([Releases](../contributing/release.md)).

| Board | Role |
|---|---|
| **Raspberry Pi 4** | the reference: Circle's feature table is complete there. Verified on hardware since 2026-09-12 |
| **Raspberry Pi 3** | the model QEMU emulates (`raspi3b`), hence the development and regression target |
| **Raspberry Pi 5** | compatibility target: it removes control from the application (no display mode choice, no FIQ, no VCHIQ), so everything is designed to work through the generic path |

The Pi resource budget is unchanged: 256 MB of Mac RAM plus the rest must fit a 1 GB board.

## Principles

- **Write as little new code as possible.** Before writing a file, check macemu, Circle and circle-stdlib;
  adapt instead of reinventing. Measured payoff: the shared folder and all disk-image access were obtained
  by reusing upstream Unix files unchanged.
- **Stay close to upstream.** Okapia builds upstream Basilisk II / SheepShaver with a shared platform layer and
  a host implementation. Changes to the cores are minimal patches in `patches/`, candidates for upstreaming.
- **Data safety outranks speed and features.** One corrupted volume ends the trust that makes an emulator
  something people use daily. See [Storage](../topics/storage.md).
- **Discover, never assume.** Print what the firmware *granted*, not what was asked for; newer boards give
  the application less control, and a missing capability flips a flag instead of forcing a rewrite.
- **Measure before optimising** — but don't write obvious waste while waiting.

## Not goals

- A cycle-accurate emulation of a specific Apple machine. The ROM supplies code, not identity: the model
  announced to Mac OS is chosen to suit the System (see [68k engine](../topics/engine-68k.md)).
- A JIT by default on the Pi. The interpreter already outruns a Quadra. The 68k translator (`OKAPIA_JIT_ARM64=1`) is
  measured, 5.4× the interpreter on a Pi 4, and stays opt-in until it is proved as safe for data as the
  interpreter (see [Decisions](decisions.md#no-jit-by-default-two-experimental-ones)).
- "No proprietary binary": Circle needs the Raspberry Pi firmware. Running without a host operating system is
  the Pi host's property, not a restriction on the desktop application.
- Shipping Apple software. No ROM, System or disk image is ever committed or re-hosted; the user supplies
  them.

## Longer term

In rough order, and each only when the one before is stable and measured:

- **Public distribution:** validate real-card preparation, complete macOS signing/notarisation, then publish the
  first release. The website and its GitHub Pages workflow are prepared but not published.
- **Desktop validation:** guest audio and network traffic, shutdown/restart paths, and a measured disk flush policy.
- **Pi networking:** finish cable and Wi-Fi validation; network time is already implemented.
- **Guest provisioning:** a persistent disk created on first boot, an online catalogue and a software library.
  This remains a separate proposal from the implemented desktop card installer.
- **Appliance experience**: power on → Mac OS, logs only on the card and the UART.
- Multicore: the compositor, then audio and disk, on secondary cores — see
  [Architecture](architecture.md#multicore).
- Linux and Windows desktop hosts, Mini vMac for System 1 → 6, shared clipboard, AppleTalk, a ready-to-flash card
  image, full Pi 5 support, and the 68k translator by default on the Pi once it is proved safe.

The priority does not move:

> **a simple, stable, fast and maintainable classic Macintosh, on either host.**
