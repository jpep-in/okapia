<!-- Copyright (C) 2026 Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

## What does Okapia include?

The Macintosh emulator, the boot menu and, in the desktop application, the tools to prepare a Raspberry Pi card. It does not include Apple ROMs, Mac OS, applications or bootable disk images. You supply those files.

## Which systems?

Okapia combines a **68K Macintosh** for System 7 and Mac OS 8, and a **PowerPC Macintosh** for later classic Mac OS systems, through Mac OS 9. The exact system must suit the ROM you supply; not every ROM starts every System.

The welcome page identifies your ROM and explains what it can start. It also checks the disks you add. For the detailed version limits and tested ROMs, see the [68K documentation](../topics/engine-68k.md#using-it) and [PowerPC documentation](../topics/engine-powerpc.md#using-it).

System 6, Mac OS X and modern macOS are outside these two engines’ supported systems.

## Where do I get a ROM?

A ROM is the Macintosh’s startup firmware. You need a suitable ROM file from your own Macintosh setup. Okapia does not distribute or link to ROM downloads.

For 68K, the recommended ROM is the 1 MB Quadra/Centris 610, 650 or 800 ROM. For PowerPC, use a compatible Old World ROM or a New World Mac OS ROM file. The welcome page tells you if the file is unsuitable.

## Which disk images?

Use an **uncompressed, raw image** of a hard disk or a CD. An image may contain a single volume or a whole disk. HFS is supported, with HFS+ for systems that understand it.

Common names include `.img` and `.image` for hard disks, and `.iso`, `.toast` or `.cdr` for CDs. The filename alone does not prove the file’s format. Convert compressed `.dmg`, `.sit` and compressed Disk Copy images before adding them.

To start immediately, use a disk with Mac OS already installed. A data disk will mount, but it cannot start the Macintosh by itself.

## Why is there a question-mark floppy?

The Macintosh has not found a bootable system. Return to the boot menu and check that the chosen disk has Mac OS installed and is compatible with the selected ROM. The welcome page’s verdict helps identify a mismatch.

## Where is the shared folder?

The **shared** folder in your workspace is the way to exchange files with the classic Macintosh. Under some early System 7 versions, the File System Manager extension is needed before that volume appears. See [the shared folder guide](../topics/shared-folder.md#using-it).

## How do I open the boot menu?

Hold **Option** at startup, or enable **Show this menu at startup** in Okapia. With no bootable disk, the menu opens automatically.

The same boot menu lets you choose a startup disk, adjust settings and display options, and read the logs on Mac and Raspberry Pi.

<!-- boot-menu-preview -->

## Where are the logs?

Open the boot menu’s **Logs** page to read the current log. Select **Previous session** to see the preceding run.
These controls are shared by Mac and Raspberry Pi.

The log files are also kept in your workspace: **okapia-desktop.log** and **okapia-desktop-previous.log** for a
desktop session; **okapia.log** and **okapia-previous.log** for a Pi session. Shut down the classic Macintosh before
moving its workspace or removing its card to read these files on another computer.

## Can I use Linux or Windows?

The desktop application currently targets macOS first. Linux and Windows are potential future desktop platforms. The downloads page only lists platforms that have published release files.

## What should I include in a bug report?

Include your Okapia build, host or Pi model, ROM identity, Mac OS version, what you did and what happened. Screenshots help when the problem is visible.

Include the relevant session’s log from the **Logs** page or your workspace.

Read the log before sharing it: it may contain workspace paths or filenames. Keep Apple ROMs and disk images out of bug reports. Use [GitHub Issues](https://github.com/jpep-in/okapia/issues) to report a reproducible problem.
