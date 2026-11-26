<!-- Copyright (C) 2026 Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

## Gather a few things

**A 68K Macintosh, reborn on Raspberry Pi.** Switch on the power, hear the chime and start straight into classic
Mac OS. Okapia runs directly on the Pi’s hardware, without Linux or any other host operating system. The desktop
application prepares the card; it is not an operating system running on the Pi.

The Pi 4 interpreter baseline measured over **five times a Quadra 650’s CPU performance** in Speedometer. This is
the 68K CPU test, not a claim that every application or disk operation runs five times faster. See the
[benchmark and method](benchmarks.md). Experimental translator results are documented separately there;
translation is not the default Pi configuration.

- A **Raspberry Pi 4**, a suitable power supply, an HDMI screen, a USB keyboard and a USB mouse.
- A microSD card and a reader connected to a computer.
- A compatible Macintosh ROM and a disk image with Mac OS installed.

The card holds Okapia itself and your Macintosh files. Once prepared, the Pi is a standalone Macintosh setup.

## Prepare your card

Choose guided preparation with the Mac application, or install the Raspberry Pi ZIP yourself.

<div class="setup-methods">

<details class="setup-method" name="pi-setup-method" open>
<summary>Open Okapia on your Mac <span>Guided card preparation</span></summary>

Get the **Okapia macOS application** from the [official releases](https://github.com/jpep-in/okapia/releases).
It helps you check the card, add your Macintosh files and install Okapia.

### Choose Raspberry Pi 4

Open Okapia on your Mac. On the welcome page, set **For** to **Raspberry Pi 4**. Choose **SD Card…** and select the card in your reader.

The application shows the card’s available space and its suitability for the Pi. If you already have a workspace open, use **File → Change Okapia Workspace…** to return to this page.

### Check the card

Read the card’s verdict before continuing. Okapia identifies its format, cluster size and any older Okapia build.
The Pi needs **FAT32 with 32 KB allocation units (clusters)**. Selecting a card in Okapia does not format it.

Formatting erases a card, so save anything you need before formatting it. The [manual setup documentation](../getting-started.md#the-card) explains the required layout.

### Bring your ROM and disks

Choose your Macintosh ROM and add a bootable Mac OS disk image. Okapia copies the files onto the card and checks which disks can start with the ROMs you supplied.

A 68K ROM and a suitable System are enough to begin. Add a compatible PowerPC ROM and disk when you want to use the PowerPC Macintosh too.

### Prepare the card

With a public release available, choose **Prepare the Card**. Okapia downloads the Pi’s files from the official release, checks them and installs them on the card. The progress appears in the application.

If the card already has an older Okapia, the button becomes **Update**. Your ROMs, disk images and preferences are separate from the release’s files.

If no packaged public release is available yet, card preparation needs a local release supplied by a developer, or the [manual build and setup](advanced.md).

</details>

<details class="setup-method" name="pi-setup-method">
<summary>Manual setup <span>Download the ZIP and copy the files</span></summary>

Use any computer with a card reader.

### Download the Raspberry Pi 4 ZIP

Open the [official releases](https://github.com/jpep-in/okapia/releases) and choose the **Raspberry Pi 4** archive:
`okapia-<release>-pi4.zip`. If no Pi 4 ZIP is listed yet, follow the [build instructions](advanced.md#build-for-raspberry-pi)
to produce the card files locally.

### Check the card’s format

Use **FAT32 with 32 KB allocation units (clusters)**. Save anything you need before formatting: formatting erases
the card. See the [card layout reference](../getting-started.md#the-card) for formatting details.

### Extract and copy

Unzip the archive and copy **its contents to the root of the card**. Keep its folders, including `firmware/` and
licences. `config.txt` and `kernel8-rpi4.img` should be directly on the card, rather than inside an extra ZIP folder.
The archive contains Okapia and the Pi boot files; Apple ROMs and Mac OS are supplied separately.

### Add your Macintosh files

Copy your compatible ROM and bootable Mac OS disk image to the card’s root. For a first 68K setup, name the copies
`okapia.rom` and `macintosh.img`, then create a plain-text file named **Okapia_Prefs** at the card’s root:

```text
rom /okapia.rom
disk /macintosh.img
bootmenu true
```

You can keep other filenames by changing these paths. For PowerPC ROMs and additional disks, see the
[preferences reference](../preferences.md). Keep each individual file below FAT32’s 4 GB limit.

Eject the card safely when the copies have finished. Continue with **Connect and start** below; both preparation
methods lead to the same standalone Macintosh.

</details>

</div>

## Connect and start

Eject the card from your Mac, put it into the Pi, connect the screen, keyboard and mouse, and switch on the power.

Hold **Option** during startup to open Okapia’s boot menu. Select your startup disk and start. With no bootable disk, the menu opens automatically.

<!-- system-extensions -->

## Connect over Wi-Fi or Ethernet

Your classic Macintosh can join your local network through the Pi 4’s **built-in Wi-Fi** or its **Ethernet port**.
Wi-Fi has been tested with **WPA2 on both 2.4 and 5 GHz**.

In Okapia’s boot menu, open **Network**, choose **Wi-Fi**, select your network, enter its password and set your
country. Choose **Join** and wait for the connected message before starting the Macintosh. For a wired connection,
choose **Ethernet** instead. If a connection is already running, changing its type asks you to **Save and restart**.

Inside classic Mac OS, choose **Ethernet** in the **TCP/IP** control panel and use **DHCP** to obtain an address
from your router. Early Systems may need MacTCP and suitable networking software. If the Macintosh started before
Wi-Fi connected and has no address, restart it once the connection is ready. See the [network guide](../topics/network.md#using-it)
for tested setups and troubleshooting.

## Shut down before switching off

Inside the Macintosh, choose **Special → Shut Down**. When shutdown has completed and the screen has gone dark, switch off the Pi’s power.

Keep the card in the Pi while it is running. To inspect or update it on your Mac, shut the Macintosh down first.

If startup stops, [the help page](help.md) explains the common causes and how to find the log.
If the screen stays blank, check the Pi 4 boot files, power and HDMI setup in the [manual setup documentation](../getting-started.md).

For early Systems, see [the extension requirements](extensions.md) for sound and the shared folder.
