/*
 * okapia_output.cpp — see okapia_output.h.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_output.h"

#include <circle/bcmpropertytags.h>
#include <circle/logger.h>
#include <stdio.h>

#define FROM "okapia-output"

// Not in Circle's list of tags: 0 is BGR, 1 is RGB.
#define PROPTAG_GET_PIXEL_ORDER 0x00040006

static CBcmFrameBuffer *s_pOutput;
static bool             s_bTried;
static bool             s_bRedLow;
static TFwMonitor       s_Monitor;

/*
 *  What the monitor says about itself
 *
 *  The EDID describes the monitor's preferred timing. Circle reports the
 *  dimensions of the frame buffer granted by the Pi firmware. The monitor's
 *  input information can confirm the HDMI signal timing when they differ.
 */
static void LogMonitor (void)
{
    CBcmPropertyTags Tags;
    TPropertyTagEDIDBlock EDID;
    EDID.nBlockNumber = EDID_FIRST_BLOCK;
    if (   !Tags.GetTag (PROPTAG_GET_EDID_BLOCK, &EDID, sizeof EDID, 4)
        || EDID.nStatus != EDID_STATUS_SUCCESS)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "No EDID from the firmware");
        return;
    }
    const u8 *b = EDID.Block;

    // The manufacturer: three letters, five bits each, big-endian in bytes 8-9.
    const unsigned nMaker = (unsigned) b[8] << 8 | b[9];
    char Maker[4] = {
        (char) ('@' + ((nMaker >> 10) & 0x1F)),
        (char) ('@' + ((nMaker >> 5) & 0x1F)),
        (char) ('@' + (nMaker & 0x1F)),
        '\0'
    };

    // Four 18-byte descriptors from byte 54. The first is the preferred timing
    // whenever it is a timing at all; a name is the one tagged 0xFC.
    unsigned nWidth = 0, nHeight = 0;
    char Name[14] = "";
    for (unsigned d = 54; d + 18 <= 126; d += 18)
    {
        if (b[d] != 0 || b[d + 1] != 0)
        {
            if (nWidth == 0)
            {
                nWidth  = b[d + 2] | (unsigned) (b[d + 4] >> 4) << 8;
                nHeight = b[d + 5] | (unsigned) (b[d + 7] >> 4) << 8;
            }
        }
        else if (b[d + 3] == 0xFC)
        {
            unsigned n = 0;
            while (n < 13 && b[d + 5 + n] != 0x0A && b[d + 5 + n] >= ' ')
            {
                Name[n] = (char) b[d + 5 + n];
                n++;
            }
            Name[n] = '\0';
        }
    }

    s_Monitor.nPreferredWidth = nWidth;
    s_Monitor.nPreferredHeight = nHeight;
    snprintf (s_Monitor.Name, sizeof s_Monitor.Name, "%s%s%s", Maker, *Name ? " " : "", Name);
    CLogger::Get ()->Write (FROM, LogNotice, "Monitor %s %s, preferred mode %ux%u",
                            Maker, Name, nWidth, nHeight);

    if (   nWidth != 0 && s_pOutput != 0
        && (nWidth != s_pOutput->GetWidth () || nHeight != s_pOutput->GetHeight ()))
    {
        CLogger::Get ()->Write (FROM, LogWarning,
                                "Output frame buffer %ux%u differs from EDID preferred %ux%u. "
                                "Check the monitor's input mode and config.txt HDMI or framebuffer settings.",
                                s_pOutput->GetWidth (), s_pOutput->GetHeight (),
                                nWidth, nHeight);
    }
}

CBcmFrameBuffer *FwOutputClaim (void)
{
    if (s_bTried)
    {
        return s_pOutput;
    }
    s_bTried = true;

    // 0, 0 asks for the display's own size.
    s_pOutput = new CBcmFrameBuffer (0, 0, 32);
    if (s_pOutput == 0 || !s_pOutput->Initialize ())
    {
        delete s_pOutput;
        s_pOutput = 0;
        CLogger::Get ()->Write (FROM, LogWarning, "No frame buffer");
        return 0;
    }

    // Asked, never assumed. A firmware that does not answer is taken to be a
    // Raspberry Pi's, since that is where the answer is BGR.
    CBcmPropertyTags Tags;
    TPropertyTagSimple Order;
    s_bRedLow = Tags.GetTag (PROPTAG_GET_PIXEL_ORDER, &Order, sizeof Order)
             && Order.nValue == 1;

    CLogger::Get ()->Write (FROM, LogNotice, "Output frame buffer %ux%u, %u bpp, pitch %u, %s",
                            s_pOutput->GetWidth (), s_pOutput->GetHeight (),
                            s_pOutput->GetDepth (), s_pOutput->GetPitch (),
                            s_bRedLow ? "red in the low byte" : "blue in the low byte");
    LogMonitor ();
    return s_pOutput;
}

bool FwOutputRedLow (void)
{
    return s_bRedLow;
}

const TFwMonitor &FwOutputMonitor (void)
{
    return s_Monitor;
}
