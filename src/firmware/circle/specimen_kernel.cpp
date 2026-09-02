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
#include "okapia_input.h"
#include "okapia_screen.h"
#include "okapia_specimen.h"
#include "okapia_strings.h"
#include "okapia_theme.h"
#include "okapia_widgets.h"

#define FROM "specimen"

// The loop's own beat, and the caret's. Ten milliseconds is short enough that a
// keystroke is never left waiting to be noticed; half a second is what a
// Macintosh shipped with and what its Control Panel let you change.
static const unsigned SLICE_MS = 10;
static const unsigned CARET_MS = 500;

CSpecimenKernel::CSpecimenKernel (void)
:   m_Timer (&m_Interrupt),
    m_Logger (m_Options.GetLogLevel (), &m_Timer),
    m_pUSBHCI (0)
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
    // A specimen with no input is still worth looking at, so this warns rather
    // than failing: the pages can be read without a keyboard, only not walked.
    m_pUSBHCI = new CUSBHCIDevice (&m_Interrupt, &m_Timer, TRUE);
    if (m_pUSBHCI == 0 || !m_pUSBHCI->Initialize ())
    {
        m_Logger.Write (FROM, LogWarning, "No USB; the specimen will not answer");
    }
    return true;
}

// Says what was operated, so that a run under an emulator can be judged from the
// serial log rather than from a screenshot of a ring one has to squint at.
static const char *WidgetName (const TWidget *pWidget)
{
    switch (pWidget->Type)
    {
    case WidgetButton:      return "bouton";
    case WidgetIconButton:  return "bouton icône";
    case WidgetCheckbox:    return "case";
    case WidgetRadio:       return "radio";
    case WidgetList:        return "liste";
    case WidgetPopup:       return "déroulante";
    case WidgetField:       return "champ";
    default:                return "composant";
    }
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
    TSurface Screen0;
    Screen0.pPixels = (unsigned char *) (uintptr) pOutput->GetBuffer ();
    Screen0.nWidth  = nWidth;
    Screen0.nHeight = nHeight;
    Screen0.nPitch  = nPitch;

    // Everything is drawn here and only the part that changed is copied
    // forward. Painting into the visible buffer means the ground goes down
    // before the control on top of it, and at sixty refreshes a second that is
    // seen — the screen blinks on every click, which is exactly what it did.
    // One allocation, at the start, and none afterwards.
    TSurface Surface;
    Surface.nWidth  = nWidth;
    Surface.nHeight = nHeight;
    Surface.nPitch  = nWidth * (unsigned) sizeof (unsigned);
    Surface.pPixels = new unsigned char[(size_t) Surface.nPitch * nHeight];
    if (Surface.pPixels == 0)
    {
        m_Logger.Write (FROM, LogError, "No room for a shadow surface");
        return ShutdownHalt;
    }

    const unsigned nScale16 = ThemeScaleFor (nWidth, nHeight);
    m_Logger.Write (FROM, LogNotice, "Theme scale %u/16 for a %ux%u output",
                    nScale16, nWidth, nHeight);

    // The specimen answers now, so nothing here chooses on a clock. Tab walks
    // the focus, Space and Return operate, the arrows move the list, and the
    // pointer is the firmware's own — the Mac's QuickDraw has not started and
    // there is nothing else on this screen to draw one.
    TTheme Theme;
    ThemeMake (nScale16, &Theme);

    FwInputWatch ();
    FwInputBounds (nWidth, nHeight);

    unsigned nPage = 0;
    const unsigned nPages = SpecimenPageCount (&Surface);
    TWidget *pWidgets = 0;
    TScreen  Screen;

    SpecimenDraw (&Surface, nPage);
    // Two statements and not one: the order in which C++ evaluates arguments is
    // its own business, so pWidgets has to be filled before it is passed.
    unsigned nCount = SpecimenWidgets (&pWidgets);
    ScreenInit (&Screen, &Theme, pWidgets, nCount);
    Screen.Background = ColorWhite;             // the dialogue's ground
    Screen.Bounds     = Rect (0, 0, nWidth, nHeight);
    SpecimenRepaint (&Surface);
    GfxBlit (&Screen0, &Surface, Screen.Bounds);

    // The pointer appears when the mouse first moves, and not before. A menu
    // nobody has touched has nothing to point with, and it keeps this screen
    // identical to the one tests/host renders — which is the whole worth of
    // that comparison.
    int  nX = 0, nY = 0;
    bool bPointer = false;

    m_Logger.Write (FROM, LogNotice,
                    "Page %u sur %u [%s] ; Tab, Espace, Retour, flèches, "
                    "Gauche/Droite pour la page, L pour la langue",
                    nPage, nPages, StringsCode (StringsLanguage ()));

    for (unsigned nTick = 0; ; nTick++)
    {
        TEvent Event;
        bool bRepaint = false;
        bool bMoved   = false;

        while (FwInputNext (&Event))
        {
            const TScreenReply Reply = ScreenEvent (&Screen, &Event);

            if (Event.Type == EventMouseMove)
            {
                bMoved = bPointer = true;
            }
            switch (Reply.Result)
            {
            case ScreenActivated:
                {
                    // A pop-up's label is whichever item it holds, so the log
                    // says what was chosen rather than an empty pair of quotes.
                    const TWidget *pW = &pWidgets[Reply.nIndex];
                    const char *pWhat = pW->pItems != 0 && pW->nChoice >= 0
                                            ? pW->pItems[pW->nChoice].pText
                                            : (pW->pText != 0 ? pW->pText : "");
                    m_Logger.Write (FROM, LogNotice, "Actionné : %s \"%s\"",
                                    WidgetName (pW), pWhat);
                }
                bRepaint = true;
                break;

            case ScreenChanged:
                bRepaint = true;
                break;

            case ScreenCancelled:
                m_Logger.Write (FROM, LogNotice, "Échap");
                break;

            case ScreenIdle:
                // What the screen did not want falls through to here. The page
                // is the specimen's own affair and not a component's, so it is
                // handled where it belongs rather than inside the loop.
                // L walks the languages. Not a product control — the real one
                // is a pop-up in the settings — but the only way to see, by
                // hand, what the measurements already check: that a page holds
                // in the longer language as well as the one it was drawn in.
                if (Event.Type == EventKeyDown
                    && (Event.nChar == 'l' || Event.nChar == 'L'))
                {
                    StringsSetLanguage ((TLanguage) ((StringsLanguage () + 1)
                                                     % LanguageCount));
                    SpecimenDraw (&Surface, nPage);
                    nCount = SpecimenWidgets (&pWidgets);
                    ScreenInit (&Screen, &Theme, pWidgets, nCount);
                    Screen.Background = ColorWhite;
                    Screen.Bounds     = Rect (0, 0, nWidth, nHeight);
                    m_Logger.Write (FROM, LogNotice, "Langue %s",
                                    StringsCode (StringsLanguage ()));
                    bRepaint = true;
                    break;
                }
                if (Event.Type == EventKeyDown
                    && (Event.nKey == OkKeyLeft || Event.nKey == OkKeyRight))
                {
                    nPage = (nPage + (Event.nKey == OkKeyRight ? 1 : nPages - 1)) % nPages;
                    SpecimenDraw (&Surface, nPage);
                    nCount = SpecimenWidgets (&pWidgets);
                    ScreenInit (&Screen, &Theme, pWidgets, nCount);
                    Screen.Background = ColorWhite;
                    Screen.Bounds     = Rect (0, 0, nWidth, nHeight);
                    m_Logger.Write (FROM, LogNotice, "Page %u", nPage);
                    bRepaint = true;
                }
                break;
            }
        }

        // Hidden before anything under it is touched, shown after: the pointer
        // keeps what it covered, and painting over it would leave those pixels
        // in the save-under to be stamped back at the next move.
        //
        // And only what changed is drawn again. Repainting the screen whole on
        // every click made it blink — the ground going back down before the
        // controls — and cost so much at 1280x960 under a window that reports
        // piled up behind it and the pointer stopped on its way. The screen
        // says what to redraw; it falls back to everything on the first paint
        // and on a change of page, where everything is what changed.
        TRect Damage = Rect (0, 0, 0, 0);

        if (bRepaint)
        {
            Damage = GfxCursorHide (&Surface);
            TRect Painted;
            if (!ScreenPaintDirty (&Surface, &Screen, &Painted))
            {
                SpecimenRepaint (&Surface);
                Painted = Screen.Bounds;
            }
            Damage = RectUnion (Damage, Painted);
            bMoved = bPointer;
        }
        if (bMoved)
        {
            FwInputPointer (&nX, &nY);
            // The shape follows what is under it, which only the screen knows.
            const TGfxCursor Shape =
                ScreenCursorAt (&Screen, nX, nY) == CursorBeam ? GfxCursorBeam
                                                               : GfxCursorArrow;
            Damage = RectUnion (Damage,
                                GfxCursorShow (&Surface, nX, nY, Theme.nIconScale, Shape));
        }
        // The caret, on the half-second a Macintosh shipped with. It is the one
        // thing on this screen that happens without anybody doing anything.
        if (nTick % (CARET_MS / SLICE_MS) == 0 && ScreenBlinkCaret (&Screen))
        {
            TRect Blinked;
            Damage = RectUnion (Damage, GfxCursorHide (&Surface));
            if (ScreenPaintDirty (&Surface, &Screen, &Blinked))
            {
                Damage = RectUnion (Damage, Blinked);
            }
            if (bPointer)
            {
                const TGfxCursor Shape =
                    ScreenCursorAt (&Screen, nX, nY) == CursorBeam ? GfxCursorBeam
                                                                   : GfxCursorArrow;
                Damage = RectUnion (Damage, GfxCursorShow (&Surface, nX, nY,
                                                           Theme.nIconScale, Shape));
            }
        }

        if (Damage.nWidth != 0)
        {
            GfxBlit (&Screen0, &Surface, Damage);
        }

        // No CActLED::Blink here. It is not a hint to the LED but a pair of
        // blocking delays — two hundred milliseconds on, five hundred off — so
        // once a second the loop simply stopped, and with it the pointer and
        // every keystroke. It read exactly like an emulator running out of
        // time, and it was one line of decoration.
        CTimer::Get ()->MsDelay (SLICE_MS);
    }

    return ShutdownHalt;
}
