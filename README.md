![Okapia logo: a compact Macintosh with an okapi's stripes](assets/logo/okapia.svg)

# Okapia

**A Quadra that fits in your hand, and a Power Macintosh in the same box.** Okapia turns a Raspberry Pi into a
classic Macintosh: power on, hear the chime, land in the Finder. No Linux underneath, no emulator window on someone
else's desktop. [Basilisk II](https://github.com/kanjitalk755/macemu) and SheepShaver run bare metal on the hardware
through [Circle](https://github.com/rsta2/circle), both in one kernel image. On a Raspberry Pi 4, System 7 runs on a
68040 about five times as fast as a Quadra 650, and Mac OS 9 on a PowerPC close to a Power Macintosh 6100/60.

Hold **Option** at power-on and a boot menu opens: pick System 7 or Mac OS 9 with the mouse, and the right processor
comes with it.

Bring your own ROMs and Systems; your disk images live on the card.

## Status

Working on a Raspberry Pi 4 and under QEMU:

- **68k**: System 7.0.1 to Mac OS 8.1 on a 68040 with an exact 68881 (IEEE binary128); about 5× a Quadra 650 on
  Speedometer's CPU test.
- **PowerPC**: Mac OS 7.5.2 to 9.0.4 from an Old World or New World ROM, close to a Power Macintosh 6100/60.
- **The Okapia boot menu**: pick the startup volume, the engine, memory, screen refresh, sound output, mouse
  resolution, shared folder; make a new volume; mount images as disks or CD-ROMs.
- Display at the monitor's native mode with the same resolutions on both engines, six colour depths, a compositor that
  redraws only what changed.
- Sound on the jack, HDMI or USB with working volume, and **the startup chime read from each Macintosh's own ROM**.
- USB keyboard and mouse as ADB, with the Mac's own acceleration; a shared folder on the card; the clock and PRAM kept
  across power cuts; volumes repaired after an interrupted session; clean restart and shut down.

Not yet: networking, Happy Mac and Sad Mac screens, provisioning. See the [roadmap](docs/project/roadmap.md).

No Apple ROM, System or disk image ships with this project: you supply them.

## Quick start

On a Pi 4: format a card FAT32 with 32 KB clusters, copy the Raspberry Pi firmware, `config.txt` and `kernel8.img`
(staged by `scripts/make-pi-sd.sh`), your ROM and a disk image holding a System, and power on. Hold **Option** for the
boot menu. Leave Mac OS through **Special → Shut Down**. Details: [Getting started](docs/getting-started.md).

Build on macOS:

```bash
./scripts/install-tools.sh
```

```bash
./scripts/bootstrap.sh
```

```bash
./scripts/build-libs.sh qemu
```

```bash
. scripts/env.sh && $MAKE -C src/kernel
```

Then `./scripts/make-sd-image.sh` and `./scripts/run-live.sh` under QEMU, or `build-libs.sh pi4` for the board — see
[Build](docs/contributing/build.md) and [Testing and debugging](docs/contributing/testing-and-debugging.md).

## Documentation

The full documentation is in [`docs/`](docs/README.md), in English, one page per subject, each covering how to use a
feature, how it works, its status, its pitfalls and its history.

| | |
|---|---|
| **Use it** | [Getting started](docs/getting-started.md) · [Preferences](docs/preferences.md) |
| **Understand it** | [Vision](docs/project/vision.md) · [Architecture](docs/project/architecture.md) · [Decisions](docs/project/decisions.md) · [Roadmap](docs/project/roadmap.md) · [Glossary](docs/project/glossary.md) |
| **Topics** | [Startup and shutdown](docs/topics/startup-and-shutdown.md) · [Boot menu](docs/topics/boot-menu.md) · [68k engine](docs/topics/engine-68k.md) · [PowerPC engine](docs/topics/engine-powerpc.md) · [Display](docs/topics/display.md) · [Sound](docs/topics/sound.md) · [Input](docs/topics/input.md) · [Storage](docs/topics/storage.md) · [Shared folder](docs/topics/shared-folder.md) · [Clock and PRAM](docs/topics/clock-and-pram.md) · [Network](docs/topics/network.md) |
| **Contribute** | [Contributor guide](docs/contributing/guide.md) · [Build](docs/contributing/build.md) · [Testing and debugging](docs/contributing/testing-and-debugging.md) · [Benchmarks](docs/contributing/benchmarks.md) |
| **Notes** | [Development notes and research studies](docs/README.md#notes) |

AI agents: read [`AGENTS.md`](AGENTS.md) first.

## Licence

Okapia is written by Jonathan Pepin and released under GPLv3 or later. Circle is GPLv3 (Rene Stange); Basilisk II and
SheepShaver are GPLv2-or-later (Christian Bauer, Marc Hellwig and contributors, carried on by kanjitalk755); libhfs is
GPLv2-or-later (Robert Leslie). No Apple ROM or system software is included.
