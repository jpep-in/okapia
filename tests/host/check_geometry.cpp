/*
 * check_geometry.cpp — measure the chrome instead of looking at it.
 *
 * Every complaint about an interface being "not quite right" is a number that
 * was never checked: a ring three pixels clear on one side and two on another,
 * a label a pixel high because it was centred on a cell that carries a descent
 * it never uses. Those are invisible to a screenshot at 1:1 and obvious at 3,
 * which is the worst way to find them.
 *
 * So each part is drawn alone on a white field and measured. The measurements
 * are printed and the asymmetries are failures, at every scale the theme
 * supports — a rule that holds at 1:1 and breaks at 2.25 is not a rule.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "okapia_gfx.h"
#include "okapia_layout.h"
#include "okapia_theme.h"
#include "okapia_screen.h"
#include "okapia_strings.h"
#include "okapia_specimen.h"
#include "okapia_widgets.h"

static const unsigned W = 400;
static const unsigned H = 160;
static unsigned s_Pixels[W * H];
static unsigned s_nFailures;

static TSurface Blank (void)
{
    const unsigned nWhite = GfxPaletteEntry (ColorWhite);
    for (unsigned i = 0; i < W * H; i++)
    {
        s_Pixels[i] = nWhite;
    }
    TSurface s = { (unsigned char *) s_Pixels, W, H, W * (unsigned) sizeof (unsigned) };
    return s;
}

struct TBox
{
    int nLeft, nTop, nRight, nBottom;   // inclusive; nRight < nLeft when empty
};

// Reading a surface goes through here rather than through the static buffer the
// small checks happen to use. The earlier version indexed that buffer with its
// own dimensions, so it silently answered about the wrong picture if ever handed
// another one — a trap that had not sprung yet, which is the only good moment to
// remove one.
static unsigned PixelAt (const TSurface *pSurface, int nX, int nY)
{
    if (nX < 0 || nY < 0 || nX >= (int) pSurface->nWidth || nY >= (int) pSurface->nHeight)
    {
        return GfxPaletteEntry (ColorWhite);
    }
    const unsigned *p = (const unsigned *) (pSurface->pPixels
                                            + (size_t) nY * pSurface->nPitch);
    return p[nX];
}

// The bounding box of everything that is not the background, inside a window.
static TBox InkBox (const TSurface *pSurface, const TRect &rWindow)
{
    const unsigned nWhite = GfxPaletteEntry (ColorWhite);
    TBox b = { (int) pSurface->nWidth, (int) pSurface->nHeight, -1, -1 };

    for (int y = rWindow.nY; y < rWindow.nY + (int) rWindow.nHeight; y++)
    {
        for (int x = rWindow.nX; x < rWindow.nX + (int) rWindow.nWidth; x++)
        {
            if (PixelAt (pSurface, x, y) == nWhite)
            {
                continue;
            }
            if (x < b.nLeft)   b.nLeft = x;
            if (x > b.nRight)  b.nRight = x;
            if (y < b.nTop)    b.nTop = y;
            if (y > b.nBottom) b.nBottom = y;
        }
    }
    return b;
}

static void Expect (const char *pWhat, int nGot, int nWant)
{
    const bool bOK = nGot == nWant;
    if (!bOK)
    {
        s_nFailures++;
    }
    printf ("    %-34s %3d  (expected %3d) %s\n", pWhat, nGot, nWant, bOK ? "" : "  <-- OFF");
}

// A ring drawn around a control must clear it by the same amount on all four
// sides. Measuring all four is the point: three of them were right.
static void CheckRing (const TSurface *pSurface, const char *pWhat, const TRect &rControl,
                       unsigned nExpectedGap)
{
    const TBox b = InkBox (pSurface, Rect (rControl.nX - 40, rControl.nY - 40,
                                 rControl.nWidth + 80, rControl.nHeight + 80));
    printf ("  %s\n", pWhat);
    Expect ("clearance left", rControl.nX - b.nLeft, (int) nExpectedGap);
    Expect ("clearance right",
            b.nRight - (rControl.nX + (int) rControl.nWidth - 1), (int) nExpectedGap);
    Expect ("clearance top", rControl.nY - b.nTop, (int) nExpectedGap);
    Expect ("clearance bottom",
            b.nBottom - (rControl.nY + (int) rControl.nHeight - 1), (int) nExpectedGap);
}

static void CheckScale (unsigned nScale16)
{
    TTheme T;
    ThemeMake (nScale16, &T);
    printf ("\n=== scale %u/16 — body %u px, stroke %u, ring %u/%u, focus %u ===\n",
            nScale16, T.pBodyFont->nHeight, T.M.nStroke, T.M.nRingWidth, T.M.nRingGap,
            T.M.nFocusGap);

    const TRect Button = Rect (120, 60, 140, T.M.nButtonHeight);

    // The default button's ring: outside the button by the gap plus its own width.
    TSurface s = Blank ();
    T.DrawButton (&s, Button, "Démarrer", StateDefault, &T);
    CheckRing (&s, "default button — ring", Button, T.M.nRingGap + T.M.nRingWidth);

    // Focus rings, on every control that can take one. The clearance must not
    // depend on which control it is.
    const unsigned nFocus = T.M.nFocusGap + T.M.nStroke;

    s = Blank ();
    T.DrawButton (&s, Button, "Focus", StateFocused, &T);
    CheckRing (&s, "button — focus", Button, nFocus);

    // Sized from its own label. A control narrower than its text overflows —
    // nothing clips a string to its control yet — and the ring would then be
    // measured against the overflow rather than against the control.
    const TRect Popup = Rect (120, 60,
                              GfxTextWidth (T.pBodyFont, "Dynamique")
                              + 4 * T.M.nGap, T.M.nButtonHeight);
    s = Blank ();
    T.DrawPopup (&s, Popup, "Dynamique", StateFocused, &T);
    CheckRing (&s, "popup menu — focus", Popup, nFocus);

    const TRect Field = Rect (120, 60, 140, T.M.nFieldHeight);
    s = Blank ();
    const TFieldMark Mark = { 6, 6 };
    T.DrawField (&s, Field, "Okapia", StateFocused | StateCaret, Mark, &T);
    CheckRing (&s, "text field — focus", Field, nFocus);

    // The tick box and the radio sit inside a taller row, so the ring is
    // measured against the box the theme actually draws, not against the row.
    const unsigned nBox = T.M.nCheckSize;
    const TRect Row = Rect (120, 60, 140, nBox);
    const TRect Box = Rect (Row.nX, Row.nY, nBox, nBox);

    s = Blank ();
    T.DrawCheckbox (&s, Row, "", StateFocused, &T);
    CheckRing (&s, "tick box — focus", Box, nFocus);

    s = Blank ();
    T.DrawRadio (&s, Row, "", StateFocused, &T);
    CheckRing (&s, "radio button — focus", Box, nFocus);

    // The contents of a dialogue must clear its frame by the margin — the
    // margin, not what is left of it once the frame has taken its share. The
    // frame is walked pixel by pixel rather than assumed: rule, gap, rule.
    {
        const TRect Dialog = Rect (20, 20, 360, 120);
        s = Blank ();
        T.DrawDialog (&s, Dialog, &T);

        const unsigned nWhite = GfxPaletteEntry (ColorWhite);
        const int y = Dialog.nY + (int) Dialog.nHeight / 2;
        int x = Dialog.nX;
        for (int nRun = 0; nRun < 2; nRun++)
        {
            while (x < (int) s.nWidth && PixelAt (&s, x, y) != nWhite) x++;   // a rule
            if (nRun == 0)
            {
                while (x < (int) s.nWidth && PixelAt (&s, x, y) == nWhite) x++;   // the gap
            }
        }

        const TRect C = ThemeContent (Dialog, &T);
        printf ("  dialogue — content clear of the inner rule\n");
        Expect ("clearance left", C.nX - x, (int) T.M.nMargin);
    }

    // A label centred in a box must have as much air above its capitals as
    // below their baseline.
    s = Blank ();
    T.DrawButton (&s, Button, "Hom", StateNormal, &T);
    const TRect Inside = RectInset (Button, (int) T.M.nStroke + 2, (int) T.M.nStroke + 2);
    const TBox b = InkBox (&s, Inside);
    printf ("  button — text\n");
    Expect ("air above minus below",
            (b.nTop - Button.nY) - (Button.nY + (int) Button.nHeight - 1 - b.nBottom), 0);
    Expect ("air left minus right",
            (b.nLeft - Button.nX) - (Button.nX + (int) Button.nWidth - 1 - b.nRight), 0);
}

// Nothing a screen draws may stray into the dialogue's frame or its margin.
// This is the check that would have caught the lower half of the specimen
// spilling into the border the moment the margin moved: the top and left were
// right, and nobody looked at the other two.
static void CheckSpecimen (unsigned nScale, unsigned nPage)
{
    const unsigned nW = 640 * nScale;
    const unsigned nH = 480 * nScale;
    unsigned *p = (unsigned *) calloc ((size_t) nW * nH, sizeof (unsigned));
    if (p == 0)
    {
        return;
    }
    TSurface s = { (unsigned char *) p, nW, nH, nW * (unsigned) sizeof (unsigned) };
    SpecimenDraw (&s, nPage);

    TTheme T0;
    ThemeMake (ThemeScaleFor (nW, nH), &T0);
    // The focus as the loop will place it, and not as the layout left it: a
    // ring is drawn outside the control it surrounds, so a page that acquires
    // one on a control placed without room for it would put it in the margin —
    // and measuring the layout alone would never see that.
    {
        TWidget *pW = 0;
        const unsigned nN = SpecimenWidgets (&pW);
        TScreen Screen;
        ScreenInit (&Screen, &T0, pW, nN);
        SpecimenRepaint (&s);
    }

    TTheme T;
    ThemeMake (ThemeScaleFor (nW, nH), &T);
    const TRect Dialog = SpecimenDialog ();
    const TRect C = ThemeContent (Dialog, &T);

    // Walk inwards from the dialogue's edge over the frame, then look for ink
    // in the band between the frame and the content rectangle.
    const unsigned nWhite = GfxPaletteEntry (ColorWhite);
    int nWorstLeft = 1000, nWorstRight = 1000, nWorstTop = 1000, nWorstBottom = 1000;
    int nBotX = -1, nBotY = -1;

    for (int y = C.nY; y < C.nY + (int) C.nHeight; y++)
    {
        for (int x = Dialog.nX; x < C.nX; x++)
        {
            if (PixelAt (&s, x, y) != nWhite && x > Dialog.nX + 8 * (int) nScale)
            {
                const int d = C.nX - x;
                if (d < nWorstLeft) nWorstLeft = d;
            }
        }
        for (int x = C.nX + (int) C.nWidth; x < Dialog.nX + (int) Dialog.nWidth; x++)
        {
            if (PixelAt (&s, x, y) != nWhite
                && x < Dialog.nX + (int) Dialog.nWidth - 8 * (int) nScale)
            {
                const int d = x - (C.nX + (int) C.nWidth) + 1;
                if (d < nWorstRight) nWorstRight = d;
            }
        }
    }
    for (int x = C.nX; x < C.nX + (int) C.nWidth; x++)
    {
        for (int y = Dialog.nY; y < C.nY; y++)
        {
            if (PixelAt (&s, x, y) != nWhite && y > Dialog.nY + 8 * (int) nScale)
            {
                const int d = C.nY - y;
                if (d < nWorstTop) nWorstTop = d;
            }
        }
        for (int y = C.nY + (int) C.nHeight; y < Dialog.nY + (int) Dialog.nHeight; y++)
        {
            if (PixelAt (&s, x, y) != nWhite
                && y < Dialog.nY + (int) Dialog.nHeight - 8 * (int) nScale)
            {
                const int d = y - (C.nY + (int) C.nHeight) + 1;
                if (d < nWorstBottom) { nWorstBottom = d; nBotX = x; nBotY = y; }
            }
        }
    }

    // No two components may touch, once each is given the room its state draws
    // in. The list frame legitimately contains its rows and its scroller, and a
    // separator is a rule laid in the space between things, so those are exempt.
    TWidget *pList = 0;
    const unsigned nCount = SpecimenWidgets (&pList);
    unsigned nOverlaps = 0;
    for (unsigned i = 0; i < nCount; i++)
    {
        for (unsigned j = i + 1; j < nCount; j++)
        {
            const TWidget &A = pList[i];
            const TWidget &B = pList[j];
            // Labels are exempt for the same reason WidgetHit ignores them:
            // their rectangle is a text box, not an extent — as tall as the
            // line and as wide as the column, while the ink inside is neither.
            // Controls are what must not touch.
            if (   A.Type == WidgetLabel     || B.Type == WidgetLabel
                || A.Type == WidgetTitle     || B.Type == WidgetTitle
                || A.Type == WidgetSeparator || B.Type == WidgetSeparator
                || A.Type == WidgetParagraph || B.Type == WidgetParagraph
                || A.Type == WidgetAlert     || B.Type == WidgetAlert
                || A.Type == WidgetIcon      || B.Type == WidgetIcon
                || A.Type == WidgetScrollbar || B.Type == WidgetScrollbar)
            {
                continue;
            }
            // A tick box and a radio wear their ring around the small box inside
            // their row, not around the row: the row already holds it, and
            // expanding again counts the same pixels twice.
            const bool bInA = A.Type == WidgetCheckbox || A.Type == WidgetRadio;
            const bool bInB = B.Type == WidgetCheckbox || B.Type == WidgetRadio;
            const int a = bInA ? 0 : (int) ThemeReach (&T, A.nState);
            const int b = bInB ? 0 : (int) ThemeReach (&T, B.nState);
            const TRect ra = RectInset (A.Rect, -a, -a);
            const TRect rb = RectInset (B.Rect, -b, -b);
            if (   ra.nX < rb.nX + (int) rb.nWidth  && rb.nX < ra.nX + (int) ra.nWidth
                && ra.nY < rb.nY + (int) rb.nHeight && rb.nY < ra.nY + (int) ra.nHeight)
            {
                if (nOverlaps == 0)
                {
                    printf ("  overlap: %s / %s at (%d,%d)\n",
                            A.pText ? A.pText : "(no text)",
                            B.pText ? B.pText : "(no text)", rb.nX, rb.nY);
                }
                nOverlaps++;
            }
        }
    }
    if (nOverlaps != 0)
    {
        printf ("  %u pair(s) of components overlap\n", nOverlaps);
        s_nFailures++;
    }

    printf ("\n=== specimen %ux%u page %u [%s] — nothing may enter the margin ===\n",
            nW, nH, nPage, StringsCode (StringsLanguage ()));
    printf ("  margin %u px; nearest intrusion: l %d  r %d  t %d  b %d\n",
            T.M.nMargin,
            nWorstLeft == 1000 ? -1 : nWorstLeft, nWorstRight == 1000 ? -1 : nWorstRight,
            nWorstTop == 1000 ? -1 : nWorstTop, nWorstBottom == 1000 ? -1 : nWorstBottom);
    if (nWorstLeft != 1000 || nWorstRight != 1000 || nWorstTop != 1000 || nWorstBottom != 1000)
    {
        printf ("    <-- overflows (bottom: x=%d y=%d; content bottom=%d)\n",
                nBotX, nBotY, C.nY + (int) C.nHeight);
        s_nFailures++;
    }
    free (p);
}

/*
 *  No text outside its control
 *
 *  Every label goes through GfxTextBox, which cuts what will not fit — so this
 *  is a check that the seam is really the only way in. Each part is drawn with
 *  a label far longer than its control and measured: any ink beyond the control
 *  plus what its state legitimately draws outside it is a part that found its
 *  own way to the surface.
 *
 *  It is worth its own pass because the failure is invisible on a screenshot
 *  until two controls stand side by side, and then it looks like a spacing
 *  mistake rather than a label running out of its box.
 */
// Ink beyond a control, on either side, once what its state legitimately draws
// outside itself is allowed for.
static void Outside (const TSurface *pSurface, const TRect &rControl, unsigned nReach)
{
    const TBox b = InkBox (pSurface, Rect (0, 0, W, H));
    const int nRight = b.nRight - (rControl.nX + (int) rControl.nWidth - 1) - (int) nReach;
    const int nLeft  = rControl.nX - b.nLeft - (int) nReach;
    Expect ("overflow right", nRight > 0 ? nRight : 0, 0);
    Expect ("overflow left", nLeft  > 0 ? nLeft  : 0, 0);
}

static void CheckTruncation (unsigned nScale16)
{
    static const char *const Long =
        "Macintosh HD — Système 7.1.2 français, disque de démarrage, très long";

    TTheme T;
    ThemeMake (nScale16, &T);

    struct TCase
    {
        const char *pWhat;
        void      (*Draw) (TSurface *, const TRect &, const char *, unsigned,
                           const TTheme *);
        unsigned    nState;
    };
    const TCase Cases[] =
    {
        { "label",      T.DrawLabel,    StateNormal  },
        { "button",     T.DrawButton,   StateNormal  },
        { "default button", T.DrawButton, StateDefault },
        // Both at once: the default button keeps its heavy black ring and also
        // gets the thin grey focus line, laid *outside* it. Placed like an
        // ordinary focus it fell 3 to 4 pixels out, inside the black band, and
        // the two overlapped. What this case checks is that the reach follows
        // the drawing: otherwise the grey line lands in the margin, which
        // nothing repaints.
        { "default button with focus", T.DrawButton, StateDefault | StateFocused },
        { "button with focus", T.DrawButton, StateFocused },
        { "tick box",   T.DrawCheckbox, StateNormal  },
        { "radio",      T.DrawRadio,    StateNormal  },
        { "popup",      T.DrawPopup,    StateNormal  }
    };

    printf ("\n=== truncation at scale %u/16 — no text outside its control ===\n",
            nScale16);

    // Narrow on purpose: wide enough to be a control, far too narrow for the
    // label it is given.
    const TRect Control = Rect (60, 60, 140 * nScale16 / 16, T.M.nButtonHeight);

    for (unsigned i = 0; i < sizeof Cases / sizeof Cases[0]; i++)
    {
        TSurface s = Blank ();
        Cases[i].Draw (&s, Control, Long, Cases[i].nState, &T);
        printf ("  %s\n", Cases[i].pWhat);
        Outside (&s, Control, ThemeReach (&T, Cases[i].nState));
    }

    // The field carries its caret's offset, so it does not share the others'
    // signature and gets its own turn. The caret is put at the very end, which
    // is the position that used to place it beyond a cut label.
    {
        TSurface s = Blank ();
        const TFieldMark Mark = { (unsigned) strlen (Long), (unsigned) strlen (Long) };
        T.DrawField (&s, Control, Long, StateFocused | StateCaret, Mark, &T);
        printf ("  field, caret at the end\n");
        Outside (&s, Control, ThemeReach (&T, StateFocused));
    }

    // A paragraph does not truncate — it wraps — so what it owes is the other
    // half of the same promise: every line inside the box that was reserved for
    // it. Measuring and drawing walk one loop for exactly this reason.
    {
        TSurface s = Blank ();
        const unsigned nWidth = 220 * nScale16 / 16;
        const unsigned nHigh  = GfxTextWrapHeight (T.pBodyFont, nWidth, Long,
                                                   T.M.nLineHeight);
        const TRect Box = Rect (60, 40, nWidth, nHigh);
        T.DrawParagraph (&s, Box, Long, StateNormal, &T);
        printf ("  paragraph (%u lines)\n", nHigh / T.M.nLineHeight);
        Outside (&s, Box, 0);
        const TBox b = InkBox (&s, Rect (0, 0, W, H));
        Expect ("overflow bottom",
                b.nBottom - (Box.nY + (int) Box.nHeight - 1) <= 0
                    ? 0 : b.nBottom - (Box.nY + (int) Box.nHeight - 1), 0);
    }
}

int main (void)
{
    static const unsigned Scales[] = { 16, 24, 32, 36, 48 };
    for (unsigned i = 0; i < sizeof Scales / sizeof Scales[0]; i++)
    {
        CheckScale (Scales[i]);
        CheckTruncation (Scales[i]);
    }

    // The gap between two neighbours is measured between what is *drawn*, not
    // between rectangles: what a control wears adds on each side, and the gap
    // constant is air on top of it. Two holes made the rule lie — a row filled
    // from the right put no gutter between its buttons, and a row taken from
    // the bottom did not reserve what its controls wear.
    {
        printf ("\n=== the gap between neighbours allows for what they wear ===\n");
        TTheme T;
        ThemeMake (16, &T);

        TRow R;
        RowBegin (&R, &T, Rect (0, 0, 600, T.M.nButtonHeight));
        const TRect A = RowLast (&R, 80, StateDefault | StateFocused);
        const TRect B = RowLast (&R, 80, StateFocused);
        const int nAir = A.nX - (B.nX + (int) B.nWidth);
        const unsigned nOwed = T.M.nGap + ThemeReach (&T, StateDefault | StateFocused)
                             + ThemeReach (&T, StateFocused);
        Expect ("two footer buttons keep their gutter on top of their rings",
                nAir >= (int) nOwed, 1);

        TLayout L;
        LayoutBegin (&L, &T, Rect (0, 0, 600, 400));
        const TRect Low  = LayoutRowBottom (&L, T.M.nButtonHeight, StateFocused);
        LayoutRowGapBottom (&L);
        const TRect High = LayoutRowBottom (&L, T.M.nButtonHeight, StateFocused);
        const int nGapV = Low.nY - (High.nY + (int) High.nHeight);
        Expect ("and two rows taken from the bottom keep theirs",
                nGapV >= (int) (T.M.nRowGap + 2 * ThemeReach (&T, StateFocused)), 1);
    }

    // Every page, at both sizes, **in every language**. That last one is the
    // whole reason the translations came before the chooser: a layout laid out
    // against one language and translated afterwards is a layout that comes
    // apart, and French runs longer than English almost everywhere. The page
    // count is asked for rather than assumed — it is what the flow decided, so
    // a section that grows moves a page and this follows it unedited.
    for (unsigned nLang = 0; nLang < LanguageCount; nLang++)
    {
    StringsSetLanguage ((TLanguage) nLang);
    for (unsigned nScale = 1; nScale <= 2; nScale++)
    {
        const unsigned nW = 640 * nScale;
        const unsigned nH = 480 * nScale;
        unsigned *p = (unsigned *) calloc ((size_t) nW * nH, sizeof (unsigned));
        if (p == 0)
        {
            continue;
        }
        TSurface s = { (unsigned char *) p, nW, nH, nW * (unsigned) sizeof (unsigned) };
        const unsigned nPages = SpecimenPageCount (&s);
        free (p);

        for (unsigned nPage = 0; nPage < nPages; nPage++)
        {
            CheckSpecimen (nScale, nPage);
        }
    }
    }
    StringsSetLanguage ((TLanguage) 0);

    printf ("\n%u failure(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
