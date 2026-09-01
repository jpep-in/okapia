//
// specimen_kernel.cpp — a kernel that does nothing but show the interface.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "specimen_kernel.h"

#include <circle/bcmframebuffer.h>
#include <circle/util.h>

#include "okapia_gfx.h"
#include "okapia_specimen.h"
#include "okapia_theme.h"

#define FROM "specimen"

CSpecimenKernel::CSpecimenKernel (void)
:   m_Timer (&m_Interrupt),
    m_Logger (m_Options.GetLogLevel (), &m_Timer)
{
    m_ActLED.Blink (2);
}

bool CSpecimenKernel::Initialize (void)
{
    // Serial first, always: a failure before the log exists cannot be told from
    // a hang (AGENTS.md).
    if (!m_Serial.Initialize (115200))
    {
        return false;
    }
    if (!m_Logger.Initialize (&m_Serial))
    {
        return false;
    }
    if (!m_Interrupt.Initialize () || !m_Timer.Initialize ())
    {
        m_Logger.Write (FROM, LogError, "Interrupts or timer would not start");
        return false;
    }
    return true;
}

TShutdownMode CSpecimenKernel::Run (void)
{
    m_Logger.Write (FROM, LogNotice, "Okapia theme specimen");

    // 0, 0 asks the firmware for the display's own size. What it grants is
    // reported rather than assumed: on a Pi 5 the request is ignored outright,
    // and under QEMU the answer is a device property, not a negotiated mode.
    CBcmFrameBuffer *pOutput = new CBcmFrameBuffer (0, 0, 32);
    if (pOutput == 0 || !pOutput->Initialize ())
    {
        m_Logger.Write (FROM, LogError, "No frame buffer");
        return ShutdownHalt;
    }

    const unsigned nWidth  = pOutput->GetWidth ();
    const unsigned nHeight = pOutput->GetHeight ();
    const unsigned nDepth  = pOutput->GetDepth ();
    const unsigned nPitch  = pOutput->GetPitch ();

    m_Logger.Write (FROM, LogNotice, "Output: %ux%u, %u bpp, pitch %u",
                    nWidth, nHeight, nDepth, nPitch);

    if (nDepth != 32)
    {
        m_Logger.Write (FROM, LogError, "Expected a 32 bpp output, got %u", nDepth);
        return ShutdownHalt;
    }

    // Straight into the frame buffer: there is no canvas to magnify. The theme
    // is built for this size, so the faces and the curves are drawn at the
    // display's own resolution rather than blown up from 640x480.
    TSurface Surface;
    Surface.pPixels = (unsigned char *) (uintptr) pOutput->GetBuffer ();
    Surface.nWidth  = nWidth;
    Surface.nHeight = nHeight;
    Surface.nPitch  = nPitch;

    const unsigned nScale16 = ThemeScaleFor (nWidth, nHeight);
    m_Logger.Write (FROM, LogNotice, "Theme scale %u/16 for a %ux%u output",
                    nScale16, nWidth, nHeight);

    // The pages alternate on a slow, fixed cadence so that a capture can aim at
    // either one by the second it takes the shot. Nothing here reads input, so
    // the clock is the only thing that can choose.
    static const unsigned PAGE_SECONDS = 5;
    for (unsigned nTick = 0; ; nTick++)
    {
        const unsigned nPage = (nTick / PAGE_SECONDS) % SPECIMEN_PAGES;
        if (nTick % PAGE_SECONDS == 0)
        {
            SpecimenDraw (&Surface, nPage);
            m_Logger.Write (FROM, LogNotice, "Page %u drawn", nPage);
        }
        m_ActLED.Blink (1);
        CTimer::Get ()->MsDelay (1000);
    }

    return ShutdownHalt;
}
