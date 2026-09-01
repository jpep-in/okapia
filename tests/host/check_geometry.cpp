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

#include "okapia_gfx.h"
#include "okapia_theme.h"
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
    printf ("    %-34s %3d  (attendu %3d) %s\n", pWhat, nGot, nWant, bOK ? "" : "  <-- ECART");
}

// A ring drawn around a control must clear it by the same amount on all four
// sides. Measuring all four is the point: three of them were right.
static void CheckRing (const TSurface *pSurface, const char *pWhat, const TRect &rControl,
                       unsigned nExpectedGap)
{
    const TBox b = InkBox (pSurface, Rect (rControl.nX - 40, rControl.nY - 40,
                                 rControl.nWidth + 80, rControl.nHeight + 80));
    printf ("  %s\n", pWhat);
    Expect ("dégagement à gauche", rControl.nX - b.nLeft, (int) nExpectedGap);
    Expect ("dégagement à droite",
            b.nRight - (rControl.nX + (int) rControl.nWidth - 1), (int) nExpectedGap);
    Expect ("dégagement en haut", rControl.nY - b.nTop, (int) nExpectedGap);
    Expect ("dégagement en bas",
            b.nBottom - (rControl.nY + (int) rControl.nHeight - 1), (int) nExpectedGap);
}

static void CheckScale (unsigned nScale16)
{
    TTheme T;
    ThemeMake (nScale16, &T);
    printf ("\n=== échelle %u/16 — corps %u px, trait %u, anneau %u/%u, focus %u ===\n",
            nScale16, T.pBodyFont->nHeight, T.M.nStroke, T.M.nRingWidth, T.M.nRingGap,
            T.M.nFocusGap);

    const TRect Button = Rect (120, 60, 140, T.M.nButtonHeight);

    // The default button's ring: outside the button by the gap plus its own width.
    TSurface s = Blank ();
    T.DrawButton (&s, Button, "Démarrer", StateDefault, &T);
    CheckRing (&s, "bouton par défaut — anneau", Button, T.M.nRingGap + T.M.nRingWidth);

    // Focus rings, on every control that can take one. The clearance must not
    // depend on which control it is.
    const unsigned nFocus = T.M.nFocusGap + T.M.nStroke;

    s = Blank ();
    T.DrawButton (&s, Button, "Focus", StateFocused, &T);
    CheckRing (&s, "bouton — focus", Button, nFocus);

    // Sized from its own label. A control narrower than its text overflows —
    // nothing clips a string to its control yet — and the ring would then be
    // measured against the overflow rather than against the control.
    const TRect Popup = Rect (120, 60,
                              GfxTextWidth (T.pBodyFont, "Dynamique")
                              + 4 * T.M.nGap, T.M.nButtonHeight);
    s = Blank ();
    T.DrawPopup (&s, Popup, "Dynamique", StateFocused, &T);
    CheckRing (&s, "menu local — focus", Popup, nFocus);

    const TRect Field = Rect (120, 60, 140, T.M.nFieldHeight);
    s = Blank ();
    T.DrawField (&s, Field, "Okapia", StateFocused, &T);
    CheckRing (&s, "champ de saisie — focus", Field, nFocus);

    // The tick box and the radio sit inside a taller row, so the ring is
    // measured against the box the theme actually draws, not against the row.
    const unsigned nBox = T.M.nCheckSize;
    const TRect Row = Rect (120, 60, 140, nBox);
    const TRect Box = Rect (Row.nX, Row.nY, nBox, nBox);

    s = Blank ();
    T.DrawCheckbox (&s, Row, "", StateFocused, &T);
    CheckRing (&s, "case à cocher — focus", Box, nFocus);

    s = Blank ();
    T.DrawRadio (&s, Row, "", StateFocused, &T);
    CheckRing (&s, "bouton radio — focus", Box, nFocus);

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
        printf ("  dialogue — contenu dégagé du filet intérieur\n");
        Expect ("dégagement à gauche", C.nX - x, (int) T.M.nMargin);
    }

    // A label centred in a box must have as much air above its capitals as
    // below their baseline.
    s = Blank ();
    T.DrawButton (&s, Button, "Hom", StateNormal, &T);
    const TRect Inside = RectInset (Button, (int) T.M.nStroke + 2, (int) T.M.nStroke + 2);
    const TBox b = InkBox (&s, Inside);
    printf ("  bouton — texte\n");
    Expect ("air au-dessus moins en dessous",
            (b.nTop - Button.nY) - (Button.nY + (int) Button.nHeight - 1 - b.nBottom), 0);
    Expect ("air à gauche moins à droite",
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
    const TWidget *pList = 0;
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
                || A.Type == WidgetListFrame || B.Type == WidgetListFrame
                || A.Type == WidgetListRow   || B.Type == WidgetListRow
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
                    printf ("  chevauchement : %s / %s en (%d,%d)\n",
                            A.pText ? A.pText : "(sans texte)",
                            B.pText ? B.pText : "(sans texte)", rb.nX, rb.nY);
                }
                nOverlaps++;
            }
        }
    }
    if (nOverlaps != 0)
    {
        printf ("  %u paire(s) de composants se chevauchent\n", nOverlaps);
        s_nFailures++;
    }

    printf ("\n=== spécimen %ux%u page %u — rien ne doit entrer dans la marge ===\n",
            nW, nH, nPage);
    printf ("  marge %u px ; intrusion la plus proche : g %d  d %d  h %d  b %d\n",
            T.M.nMargin,
            nWorstLeft == 1000 ? -1 : nWorstLeft, nWorstRight == 1000 ? -1 : nWorstRight,
            nWorstTop == 1000 ? -1 : nWorstTop, nWorstBottom == 1000 ? -1 : nWorstBottom);
    if (nWorstLeft != 1000 || nWorstRight != 1000 || nWorstTop != 1000 || nWorstBottom != 1000)
    {
        printf ("    <-- déborde (bas : x=%d y=%d ; bas du contenu=%d)\n",
                nBotX, nBotY, C.nY + (int) C.nHeight);
        s_nFailures++;
    }
    free (p);
}

int main (void)
{
    static const unsigned Scales[] = { 16, 24, 32, 36, 48 };
    for (unsigned i = 0; i < sizeof Scales / sizeof Scales[0]; i++)
    {
        CheckScale (Scales[i]);
    }

    for (unsigned nPage = 0; nPage < SPECIMEN_PAGES; nPage++)
    {
        CheckSpecimen (1, nPage);
        CheckSpecimen (2, nPage);
    }

    printf ("\n%u écart(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
