//
// okapia_boot.cpp — see okapia_boot.h.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "okapia_circle.h"

#include <circle/startup.h>

#include "okapia_boot.h"
#include "hal_circle.h"

#define FROM "okapia-boot"

int main (void)
{
    // The 68k Macintosh is the front door, and not by preference: it is the one
    // whose symbols keep their own names in the merged image, so it is the one
    // this file can call without knowing anything about the build. It brings
    // the board up, reads the preferences and runs the firmware's window; if
    // the startup volume asks for the other engine, it says so and returns
    // here rather than starting anything.
    TOkapiaExit Exit = OkapiaRun68k (0);

    for (;;)
    {
        switch (Exit)
        {
        case OkapiaSwitchToPowerPC:
            CLogger::Get ()->Write (FROM, LogNotice, "Switching to the PowerPC Macintosh");
            Exit = OkapiaRunPowerPC (1);
            continue;

        case OkapiaSwitchTo68k:
            CLogger::Get ()->Write (FROM, LogNotice, "Switching to the 68k Macintosh");
            Exit = OkapiaRun68k (1);
            continue;

        case OkapiaReboot:
            BoardLogFlush ();
            return EXIT_REBOOT;

        case OkapiaHalt:
        default:
            BoardPowerOff ();
            halt ();
            return EXIT_HALT;
        }
    }
}
