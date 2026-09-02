//
// okapia_firmware.cpp — the boot firmware, as the kernel sees it.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_firmware.h"

#include <circle/bcmframebuffer.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/types.h>

#include <wrap_fatfs.h>

#include "okapia_gfx.h"
#include "okapia_input.h"
#include "okapia_theme.h"

#define FROM "firmware"

// About two seconds of a still grey field, which is what a Macintosh showed
// before its Happy Mac. Long enough to hold Option, short enough that nobody
// waiting to boot resents it.
static const unsigned WINDOW_MS = 2000;

// The window is spent in slices rather than one sleep, so that whatever
// delivers a USB report — an interrupt, or the cooperative scheduler getting a
// turn — has one. Waiting two seconds in a single call would work only if the
// first were true, and that is exactly the kind of thing not worth assuming.
static const unsigned SLICE_MS = 20;

// USB usage identifiers, which stop at okapia_input.cpp for everything except
// this: the zap is asked for by two keys that mean nothing to a menu, so there
// is no logical key to give them and inventing one would be worse.
static const unsigned char KEY_P = 0x13;
static const unsigned char KEY_R = 0x15;

TFirmwareResult FirmwareRun (void)
{
    // 0, 0 asks the firmware for the display's own size; what it grants is
    // reported rather than assumed, because on a Pi 5 the request is ignored
    // outright and under QEMU the answer is a device property.
    CBcmFrameBuffer *pOutput = new CBcmFrameBuffer (0, 0, 32);
    if (pOutput == 0 || !pOutput->Initialize ())
    {
        CLogger::Get ()->Write (FROM, LogWarning, "No frame buffer; going straight to the Mac");
        delete pOutput;
        return FirmwareBoot;
    }

    const unsigned nWidth  = pOutput->GetWidth ();
    const unsigned nHeight = pOutput->GetHeight ();
    const unsigned nDepth  = pOutput->GetDepth ();

    if (nDepth != 32)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Expected 32 bpp, got %u; skipping", nDepth);
        delete pOutput;
        return FirmwareBoot;
    }

    TTheme Theme;
    const unsigned nScale16 = ThemeScaleFor (nWidth, nHeight);
    ThemeMake (nScale16, &Theme);

    TSurface Surface;
    Surface.pPixels = (unsigned char *) (uintptr) pOutput->GetBuffer ();
    Surface.nWidth  = nWidth;
    Surface.nHeight = nHeight;
    Surface.nPitch  = pOutput->GetPitch ();

    Theme.DrawDesktop (&Surface, &Theme);

    // The keyboard and the mouse, taken for the length of the window and *not*
    // given back. Circle keeps one handler per device and InputInit() puts the
    // Macintosh's in their place when StartMacintosh() runs, which is the whole
    // handover.
    //
    // Registering 0 to detach looks tidier and wedges the boot: it does not
    // detach anything, it puts the device back into cooked mode
    // (usbkeyboard.cpp:200), and the reports then arriving go to
    // CKeyboardBehaviour instead. Measured — with that call the kernel stopped
    // dead after this function returned, with no further log at all, and
    // removing it was enough. Leaving ours in place until InputInit() replaces
    // them costs nothing: they only ever touch their own statics.
    const bool bKeyboard = FwInputBegin (nWidth, nHeight);

    CLogger::Get ()->Write (FROM, LogNotice, "Output %ux%u, theme scale %u/16, %s, holding %u ms",
                            nWidth, nHeight, nScale16,
                            bKeyboard ? "keyboard attached" : "no keyboard", WINDOW_MS);

    for (unsigned nWaited = 0; nWaited < WINDOW_MS; nWaited += SLICE_MS)
    {
        // Drained rather than read, so that a window nobody touches cannot end
        // with a full queue: this one only wants the latch, but the queue is
        // the same one the screens will read from.
        TEvent Event;
        while (FwInputNext (&Event))
        {
        }
        CTimer::Get ()->MsDelay (SLICE_MS);
    }

    const unsigned nSeen = FwInputSeenModifiers ();

    // Command-Option-P-R, the combination a Macintosh answered by forgetting its
    // parameter RAM. It is watched here rather than passed on, because a real
    // Macintosh does this in its ROM before the emulated ADB is alive at all:
    // letting it through would reach nobody.
    const bool bForgetPram = (nSeen & ModOption) && (nSeen & ModCommand)
                          && FwInputSeenKey (KEY_P) && FwInputSeenKey (KEY_R);

    // Option on its own opens the chooser. Every other combination including it
    // belongs to the Macintosh, so anything else held alongside disqualifies it.
    const bool bOption = !bForgetPram
                      && (nSeen & ModOption)
                      && !(nSeen & (ModCommand | ModControl | ModShift));

    if (bForgetPram)
    {
        // main.cpp:106 rebuilds the parameter RAM from scratch as soon as the
        // "NuMc" signature is missing, so removing the file *is* the zap — and
        // it touches nothing of the user's data.
        //
        // f_unlink and not remove(): the newlib glue's remove() deletes the file
        // and then leaves something behind that wedges the next fopen — the ROM
        // never opened and the kernel stopped dead after this point, with no log
        // at all. Going straight to the file system does the same job and the
        // boot carries on.
        const FRESULT nResult = f_unlink ("SD:/BasiliskII_XPRAM");
        CLogger::Get ()->Write (FROM, LogNotice, "Command-Option-P-R: parameter RAM %s (%d)",
                                nResult == FR_OK ? "forgotten"
                                                 : (nResult == FR_NO_FILE ? "was already absent"
                                                                          : "could not be removed"),
                                (int) nResult);
    }
    else if (bOption)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "Option held: the chooser would open here");
    }
    else if (FwInputSeenAnything ())
    {
        CLogger::Get ()->Write (FROM, LogNotice,
                                "Input during the window, none of ours: modifiers %02X",
                                nSeen);
    }

    // Handed back before the emulator claims its own. Measured under QEMU: a
    // frame buffer can be claimed, released and claimed again, same address and
    // same size, and the second one is live. Worth re-checking on hardware —
    // the mailbox is the firmware's, not ours.
    delete pOutput;

    CLogger::Get ()->Write (FROM, LogNotice, "Display handed back");
    return FirmwareBoot;
}
