<!-- Copyright (C) 2026 Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

## Basilisk II + SheepShaver, reimagined

Think of Okapia as a new generation of the Basilisk II and SheepShaver experience: their proven Macintosh engines,
brought together in one application with a shared interface and a host layer for modern Macs and bare-metal Pi.
The emulators’ authors and upstream projects remain the foundation; Okapia adds the integration around them.

- **Two Macintosh generations in one app.** The startup disk selects the 68K or PowerPC engine, through one boot menu.
- **A display that belongs on a modern Mac.** Retina-aware menus and crisp, integer-scaled classic pixels.
- **A welcome page instead of a separate preferences editor.** Create a workspace, identify a ROM, add disks and start.
- **A Pi that boots directly into Mac OS.** The same application prepares its card; the Pi runs without Linux or a host OS.
- **Shared improvements below the interface.** An 80-bit softfloat FPU, a 68K-to-AArch64 translator on arm64 desktop
  builds, a shared compositor and startup volume inspection. Experimental Pi translation remains a separate build choice.

Linux and Windows remain future desktop targets. The [desktop comparison](../topics/desktop.md#against-upstream-basilisk-ii-on-a-mac)
records the implementation details and current validation limits.

## One foundation, two hosts

The [architecture illustration](architecture.md) shows the layers: the two emulators, Okapia’s shared code, Circle
for Raspberry Pi, and the desktop host for macOS, with Linux and Windows shown as coming soon.

## A workspace is a folder

Okapia keeps ROMs, disk images, preferences, parameter RAM and the shared folder together. The desktop host opens the same layout used by a Pi card. Shut down before moving that workspace between hosts, and run only one Okapia against it at a time.

The preferences file is **Okapia_Prefs**. Many settings can be changed in the boot menu; the [preferences reference](../preferences.md) documents the rest.

## Install a Pi release by hand

Download the archive for **Raspberry Pi 4** from the official release. Extract its contents onto a FAT32 card with 32 KB clusters, then add your ROMs and disk images. Use the Pi 4 archive rather than a QEMU development build.

The archive contains Okapia and the Pi boot files. It does not contain a Macintosh ROM or Mac OS. See the [manual card layout](../getting-started.md#the-card).

## Build the desktop application

From a prepared source checkout, the desktop build is:

```sh
make -C src/hosts/desktop
./scripts/make-desktop-app.sh
```

The result is `build/desktop/Okapia.app`. Dependencies, supported host behaviour and the headless test mode are described in [Desktop](../topics/desktop.md).

## Build for Raspberry Pi

The [build guide](../contributing/build.md) covers the toolchain, pinned submodules and patches. The [getting started documentation](../getting-started.md) explains staging the card.

QEMU is useful for development, but it does not reproduce all Pi hardware behaviour. Validate a Pi build on the actual board.

## Explore the project

- [Architecture](architecture.md): an illustrated view of the shared layers, hosts and platforms.
- [Storage](../topics/storage.md): disk access, repair and data safety.
- [Releases](../contributing/release.md): manifests, packaging and the release procedure.
- [Testing and debugging](../contributing/testing-and-debugging.md): host checks, QEMU and hardware.
- [Contributor guide](../contributing/guide.md): conventions, licensing and working rules.

Okapia is GPLv3 or later, built on Basilisk II, SheepShaver and Circle. The [source repository](https://github.com/jpep-in/okapia) holds the code, documentation, issues and release files.

## Behind Okapia

Read [Making Of](making.md) for the development process, AI assistance, contributors and licences, or
[About Okapia](about.md) for Jonathan Pepin’s personal account of why it began.
