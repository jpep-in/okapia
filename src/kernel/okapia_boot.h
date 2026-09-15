//
// okapia_boot.h — which Macintosh runs, and what happens when it stops.
//
// One image carries both emulators (docs/project/architecture.md), so the choice is
// no longer made by which kernel the Pi's firmware loaded: it is made here, at
// run time, and changing it costs a function call rather than a reboot.
//
// The two engines are entered through the two functions below and nothing else.
// That is the whole of the seam: the build renames every other symbol of one
// engine so the two cores can share an image, and these two names are the
// deliberate exception (scripts/merge-engines.sh).
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_boot_h
#define _okapia_boot_h

enum TOkapiaExit
{
    OkapiaHalt,             // stop the board
    OkapiaReboot,           // reset it
    OkapiaSwitchTo68k,      // the startup volume asks for the other Macintosh
    OkapiaSwitchToPowerPC
};

// Neither returns until its Macintosh has stopped. Both assume the board is
// theirs to bring up — COkapiaBoard is shared and idempotent, so the second one
// to run finds serial, the card and USB already there and re-registers nothing.
//
// C linkage on purpose. These two names are the only ones the merge rule must
// leave out of the rename list, and a plain name is one a build script can be
// asked to protect without knowing how this compiler mangles.
// bSwitched says this engine was entered because the *other* one read the
// card's answer and handed over. It then skips its own firmware window: the
// firmware is shared and has just run, and asking again looked exactly like a
// reboot back to the menu — pick PowerPC, watch the menu come up a second time,
// pick again. Every later time round the window opens as usual, because that is
// the only way back to the menu after a restart from Mac OS.
extern "C" TOkapiaExit OkapiaRun68k (int bSwitched);
extern "C" TOkapiaExit OkapiaRunPowerPC (int bSwitched);

#endif
