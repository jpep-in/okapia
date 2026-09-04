/*
 * okapia_output.cpp — see okapia_output.h.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_output.h"

#include <circle/logger.h>

#define FROM "okapia-output"

static CBcmFrameBuffer *s_pOutput;
static bool             s_bTried;

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

    CLogger::Get ()->Write (FROM, LogNotice, "Display %ux%u, %u bpp, pitch %u",
                            s_pOutput->GetWidth (), s_pOutput->GetHeight (),
                            s_pOutput->GetDepth (), s_pOutput->GetPitch ());
    return s_pOutput;
}
