# Getting started

> From an empty SD card to the Finder, on a Raspberry Pi 4 or under QEMU.

Okapia ships no ROM, no System and no disk image: you supply them. Building the kernel itself is described
in [Building](contributing/build.md).

## What you need

| | |
|---|---|
| Board | a Raspberry Pi 4 (the reference). A Pi 3 works through the same code; a Pi 5 is a compatibility target |
| Power | the official 5.1 V / 3 A supply. A small yellow lightning bolt on screen is the Pi firmware reporting under-voltage |
| Display | HDMI. The firmware reads the monitor's EDID; see [Display](topics/display.md) for monitors it drives at the wrong mode |
| Input | a USB keyboard and a USB mouse |
| Card | a microSD card, FAT32, **32 KB clusters** (see below) |
| 68k ROM | a 1 MB 32-bit-clean ROM — the 1993 Quadra/Centris 610/650/800 ROM, checksum `F1ACAD13` |
| PowerPC ROM | optional: a 4 MB Old World ROM (such as a Power Macintosh 9600's) or a New World `Mac OS ROM` file |
| A System | a disk image holding System 7.x to Mac OS 9.0.4 |

`scripts/check-rom.py <file>` checks a ROM before you boot it: size, checksum, the 32-bit-clean word for a
68k ROM, the nanokernel type and the Mac OS versions it allows for a PowerPC ROM.

## The card

Format it FAT32 with **32 KB clusters**. FatFs keeps no map of a file's clusters, so every seek inside a disk
image walks the FAT one cluster at a time: 4 KB clusters make a 500 MB image a chain of 128,000 links, 32 KB
make it 16,000. The kernel logs the size it found (`Card: FAT32, 32768-byte clusters`).

Files at the root:

| File | What it is |
|---|---|
| `start4.elf`, `fixup4.dat`, `bcm2711-rpi-4-b.dtb`, `armstub8-rpi4.bin` | the Raspberry Pi 4 firmware and ARM stub |
| `config.txt` | the Pi firmware's configuration (below) |
| `kernel8-rpi4.img` | Okapia, built for the Pi 4 |
| `okapia.rom` | the 68k ROM (any name, set by `rom`) |
| `powermac9600v1.rom` | a PowerPC ROM, if you want the PowerPC Macintosh (set by `romppc`) |
| `*.image`, `*.img` | disk images; any name works |
| `BasiliskII_Prefs` | [preferences](preferences.md), written with commented examples on the first boot if absent |
| `BasiliskII_XPRAM`, `SheepShaver_XPRAM` | each Macintosh's parameter RAM, written by Okapia |
| `okapia.log`, `okapia-previous.log` | the log of this session and the previous one, written by Okapia |
| `shared/` | the [shared folder](topics/shared-folder.md) |
| `BasiliskII.keycodes` | optional key-mapping override (see `keycodefile`) |

### Staging it

With a Pi 4 kernel built (`scripts/build-libs.sh pi4`, then `make -C src/kernel`):

```bash
./scripts/make-pi-sd.sh
```

It refuses a kernel built for QEMU (which carries no real SD driver), fetches nothing, and stages
`rpi4-sd-contents/`: firmware, ARM stub, `config.txt`, the kernel renamed `kernel8-rpi4.img`, and everything
in `qemu/sd-contents/` that is not a disk image. Disk images are left out on purpose — copy the one you
want by hand. Then:

```bash
cp -R rpi4-sd-contents/ /Volumes/OKAPIA/
```

### `config.txt`

The staged file is Circle's `config64.txt` plus an `[all]` section:

| Line | Why |
|---|---|
| `hdmi_force_hotplug=1` | brings HDMI up even when nothing was plugged in at power-on |
| `disable_overscan=1` | no TV safety border: a 1920×1080 output otherwise came back as 1824×984 in a black frame |
| `disable_splash=1`, `boot_delay=0` | no rainbow square, no pause before the kernel. The Pi firmware offers no custom splash image — only its own or none |
| `hdmi_group=2`, `hdmi_mode=87`, `hdmi_cvt=…`, `max_framebuffer_*` | commented out: a custom mode for a monitor the firmware drives at the wrong resolution (see [Display](topics/display.md#using-it)) |

Forcing a CEA mode (`hdmi_group=1`, `hdmi_mode=16`) used to pin every monitor to 1080p; Okapia asks the
firmware for its preferred mode instead.

## First boot

1. Power on. The chime plays from the ROM of the Macintosh that is about to start
   ([Sound](topics/sound.md#startup-chime)).
2. Two seconds of plain grey. **Hold Option** to open the boot menu, or set `bootmenu true` to always open it.
   With nothing bootable on the card, the menu opens by itself.
3. In the menu, pick the startup volume, the emulator for a universal System, read-only or mounted per
   volume, and the settings (memory, screen refresh, sound, mouse, language) — see
   [Boot menu](topics/boot-menu.md).
4. The Macintosh starts. **Always leave through Special → Shut Down** — see
   [Startup and shutdown](topics/startup-and-shutdown.md). The Pi cannot cut its own power: the screen goes
   dark and you switch it off.

If something goes wrong, mount the card on another computer and read `okapia.log` (or
`okapia-previous.log` after a restart): the kernel logs what the firmware granted, what it chose and why.
Set `perfreport true` for counters every five seconds.

## Under QEMU, without a board

Stage ROMs and disk images in `qemu/sd-contents/`, then:

```bash
./scripts/build-libs.sh qemu
. scripts/env.sh && $MAKE -C src/kernel
./scripts/make-sd-image.sh
./scripts/run-live.sh
```

`run-live.sh` opens a window and a monitor socket; end the session with Finder → Shut Down, never by closing
QEMU — see [Testing and debugging](contributing/testing-and-debugging.md). QEMU emulates no sound output and
no RTC, and accepts video modes a real Pi refuses: validate on hardware.

## Common first-boot problems

| Symptom | Cause |
|---|---|
| Question-mark floppy | no bootable volume, or a volume left in use with `hfsrepair false` — see [Storage](topics/storage.md) |
| A blank "Welcome to Macintosh" box on System 7.1 | the wrong `modelid` for that System; leave `modelidauto true` — see [68k engine](topics/engine-68k.md#model-id) |
| "System 6.0.8 does not work with 32-bit addressing" after forgetting the PRAM | the startup disk fell back to the first `disk` line; pick the volume again in the boot menu |
| No shared volume on the desktop under System 7.0 or 7.1 | install the File System Manager 1.2 extension — see [Shared folder](topics/shared-folder.md) |
| No sound under System 7.1 | install the Sound Manager 3.x extension — see [Sound](topics/sound.md) |
| The picture uses only part of the screen | the Mac's resolution is smaller than the output; pick another in Monitors or set `screen` |
| Wrong fonts in menus and icon labels | a damaged System — restage the volume before suspecting video |
