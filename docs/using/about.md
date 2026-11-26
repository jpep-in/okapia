<!-- Copyright (C) 2026 Jonathan Pepin -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

## A Macintosh I would actually switch on

I love starting up an old Macintosh. I have a few in my collection, mostly compact all-in-one models and the
low, flat LC “pizza boxes”. The chime, the Finder, a favourite game: that feeling never really goes away.

Getting there can take rather more effort. There is a heavy machine to bring out of the cupboard, a display to
connect, an old VGA adapter and its limited resolutions, and a keyboard and mouse from another era. Wi-Fi was
never part of the deal. A BlueSCSI makes storage and file transfers much more practical, but there is still a
little installation to assemble before I can play.

Sometimes I find myself leaving it all in the cupboard. Okapia began with a simple wish: **make that Macintosh
something I would switch on for a quick game, rather than an afternoon of setting things up.**

## A Mac, straight from power-on

For this project, I wanted the experience to begin with the Macintosh. Starting a host operating system, then an
emulator, put another layer between me and that feeling.

I first looked at microcontrollers. Projects such as [M5Tab-Macintosh](https://github.com/amcchord/M5Tab-Macintosh)
on the M5Stack Tab5 show how much can be done on a small device. But the Pico and ESP32-class boards I considered
left too little performance headroom for the Mac I had in mind.

Then I discovered [Circle](https://github.com/rsta2/circle): a way to build software directly for Raspberry Pi
hardware, without Linux or another host OS. That was the turning point. I could approach the Pi like a
microcontroller, with the power and connections of a small computer. My goal became a 68K Macintosh faster than
anything I could have owned at the time.

## Why these two emulators?

I admire [Mini vMac](https://www.gryphel.com/c/minivmac/) and [Snow](https://github.com/twvd/snow). Recreating the
behaviour of particular historical machines is valuable work. My own interest was in giving their software more
room to run.

Basilisk II and SheepShaver were the right foundations for that idea. They use compatible Macintosh ROMs with
patches and provide an environment for classic Mac OS without reproducing every piece of the original machine
cycle by cycle. That leaves scope to spend modern hardware’s performance on the Macintosh’s software.

The boot menu followed naturally. I liked being able to choose a system on later Power Macs. Here I could take
that idea further: put the disks, the two Macintosh engines and the settings together, and make changing System
feel like part of the machine.

## The first boot was only the beginning

Seeing the Finder appear on a Raspberry Pi for the first time felt like a small miracle. Applications were
already wonderfully usable. Yet I was convinced the processor could do more.

That led to the 68K-to-AArch64 translator and work on the floating-point unit. There were failed approaches,
long investigations and attempts I put aside before returning to them months later. Execution telemetry and a
home-built benchmark tool eventually made the difficult failures visible. Claude helped me work through a
breakthrough in that process.

The gains deserve a proper explanation, with the machines, tests and limits recorded alongside them. The
[benchmarks page](benchmarks.md) now records the available CPU and FPU measurements with their configurations and limits.

The technical challenge is enjoyable in itself, but I also like what it might enable: writing new applications
for old systems, and perhaps giving them a few small everyday jobs again.
[Retro68](https://github.com/autc04/Retro68) already provides tools to build classic 68K and PowerPC Macintosh
applications on a modern computer. This history can still be something we make things with.

## Why bring it back to a modern Mac?

Once the Pi port existed, a desktop version felt like a natural next step. The shared boot menu could replace a
separate configuration utility, and the work on the AArch64 CPU path and portable FPU could serve Apple silicon
too. I also wanted the crisp, whole-number scaling on Retina displays that I had struggled to get in my own
emulator setups.

On a modern Mac, the experience is easier to reach, including for someone who never used these systems when
they were new. That matters to me: preserving this part of computing history also means letting people discover
and use it.

[Infinite Mac](https://infinitemac.org/) is a wonderful example of that spirit: a classic Mac is only a browser
away. It even supports custom configurations. For my own longer-lived setups, I like keeping the ROMs, disks and
files in an ordinary folder or on an SD card, choosing my own system language, and making that Macintosh my own.
That is the experience I want Okapia to offer.

## Still plenty to explore

System 6 is one of the challenges I would like to return to. Its 24-bit memory world does not fit Basilisk II’s
current assumptions. My attempts have not succeeded. For now.

The display path also has room to improve. Moving more display work onto a separate CPU core is a direction I
want to explore, to improve smoothness and leave more time for emulation. These are goals, rather than features
I can promise today.

If you try Okapia, I would love to hear what you enjoy, what gets in the way, and what you would like it to do.
Please share ideas and reproducible bugs through [GitHub Issues](https://github.com/jpep-in/okapia/issues).

— **Jonathan Pepin**

Read [Making Of](making.md) for the development process, AI assistance and the people and projects behind it.
