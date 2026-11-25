<!-- Copyright (C) 2026 Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

## Two engines, one Okapia

Basilisk II provides the 68K Macintosh; SheepShaver provides the PowerPC Macintosh. Both are included in the
desktop application and the Pi kernel. The selected startup disk determines which one runs.

Okapia’s shared layer handles Macintosh-facing video, sound, input, disks and timing. The common firmware supplies
the welcome and boot-menu pages. Hardware-specific work stays in the host implementations.

## Circle and Desktop

**Circle** runs on the Raspberry Pi, with circle-stdlib supplying the C and C++ runtime. It gives Okapia access to
the board’s display, USB devices, card, audio and network. There is no Linux or host operating system underneath it.

**Desktop** uses SDL3 and host services to run Okapia as an application. The firmware asks for host services through
`okapia_host.h`; the shared emulator platform also uses board-compatible interfaces. This boundary lets the shared
Macintosh experience support additional hosts as their platform-specific implementations are developed.

## Available and coming soon

The current targets are **Raspberry Pi 4** and **macOS**, developed on Apple silicon. Linux has partial host support
but still needs a validated build. Windows needs host-specific work, including file access. Linux and Windows are
shown in grey as **Coming soon**; no release date or downloadable build is implied.

## Go deeper

The [technical architecture](../project/architecture.md) covers symbol separation, memory, timing and Pi multicore
plans. [Desktop](../topics/desktop.md) describes the current host implementation and its validation status.
