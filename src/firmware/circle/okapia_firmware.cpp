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

#include "okapia_gfx.h"
#include "okapia_theme.h"

#define FROM "firmware"

// About two seconds of a still grey field, which is what a Macintosh showed
// before its Happy Mac. Long enough to hold Option, short enough that nobody
// waiting to boot resents it.
static const unsigned WINDOW_MS = 2000;

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

    CLogger::Get ()->Write (FROM, LogNotice, "Output %ux%u, theme scale %u/16, holding %u ms",
                            nWidth, nHeight, nScale16, WINDOW_MS);

    CTimer::Get ()->MsDelay (WINDOW_MS);

    // Handed back before the emulator claims its own. Measured under QEMU: a
    // frame buffer can be claimed, released and claimed again, same address and
    // same size, and the second one is live. Worth re-checking on hardware —
    // the mailbox is the firmware's, not ours.
    delete pOutput;

    CLogger::Get ()->Write (FROM, LogNotice, "Display handed back");
    return FirmwareBoot;
}
