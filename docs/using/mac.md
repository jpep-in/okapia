<!-- Copyright (C) 2026 Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

## Before you begin

You’ll need Okapia for macOS, a compatible **Macintosh ROM**, and a **disk image with Mac OS already installed**. Okapia supplies the application; you supply the Apple software.

A ROM is a copy of the startup software from a Macintosh. A disk image is a file that acts as that Macintosh’s hard disk. A System installer alone is not the same as a ready-to-start disk.

The first desktop build targets Macs with Apple silicon. Check the download’s system requirements when a public release is available. Intel Mac, Linux and Windows builds are future possibilities.

## Open Okapia

Download the macOS application from the official release and move **Okapia.app** to your Applications folder. Open it to reach the welcome page.

The current development application is signed ad hoc. Public signing and notarisation are still in preparation; development builds may require macOS’s **Open Anyway** approval in Privacy & Security. Follow the instructions accompanying the download.

## Create your workspace

Leave **For** set to **This computer**. Choose **Create…** to make an Okapia Workspace, or **Open…** if you already have one.

A workspace is an ordinary folder that holds the Macintosh’s ROMs, disks and settings. You can keep more than one workspace, for example one for each collection of systems or applications.

## Add your ROM and disk

Under **Macintosh ROM**, choose your ROM file. Okapia identifies it and explains which systems it can start. For a 68K Macintosh, the recommended ROM is the 1 MB Quadra/Centris 610, 650 or 800 ROM. PowerPC systems use an appropriate Old World ROM or New World **Mac OS ROM** file.

Under **The Mac’s disk**, choose **Add a disk image…** and select your bootable image. Files chosen this way are copied into the workspace. You can also copy the files into that folder yourself; the welcome page refreshes as the folder changes.

Use an uncompressed, raw hard disk or CD image. Compressed archives and compressed disk images need converting first. See [disk image help](help.md#which-disk-images).

## Start your Macintosh

Choose **Continue** to open Okapia’s boot menu. Select a startup disk and start the Macintosh. The system on that disk determines whether Okapia uses the 68K or PowerPC engine.

Use the **Display** page to choose the window size or full screen. Other settings are available in the boot menu, so you can start with the defaults.

<!-- system-extensions -->

## Return to the welcome page

Hold **Shift** when opening Okapia, or choose **File → Change Okapia Workspace…**. Open the folder holding your ROMs
and disks, then check the ROM and disk verdicts before continuing.

## Retina screens, kept sharp

Okapia is designed for Retina displays: the classic Macintosh’s pixels stay crisp at whole-number display scales,
and Okapia’s own menus and welcome page are drawn at the screen’s full pixel density. The result is sharp classic
graphics and clear, high-resolution interface text.

Choose a window size or full screen on the **Display** page. The offered scales help you find a comfortable fit;
shrinking a picture to fit a smaller window is distinct from pixel-perfect enlargement. See
[Desktop](../topics/desktop.md#using-it) for the scaling details. Choose the classic Macintosh’s own resolution
separately in its **Monitors** control panel.

## Find your files again

The workspace keeps your disk images and settings together. **File → Show Okapia Workspace in Finder** opens it in the modern Mac’s Finder. **File → Change Okapia Workspace…** opens another workspace.

The workspace’s **shared** folder can appear inside the classic Macintosh as a shared volume. Some early Systems need an additional extension; see [shared folder help](help.md#where-is-the-shared-folder).

## End a session

Inside the classic Macintosh, choose **Special → Shut Down** before closing Okapia. Let Mac OS finish writing its disks.

Want to take the same kind of setup to a dedicated machine? Follow [the Raspberry Pi guide](pi.md).

For early Systems, see [the extension requirements](extensions.md) for sound and the shared folder.
