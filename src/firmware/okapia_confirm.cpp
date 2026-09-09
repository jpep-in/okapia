/*
 * okapia_confirm.cpp — see okapia_confirm.h.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_confirm.h"
#include "okapia_icons.h"
#include "okapia_page.h"
#include "okapia_strings.h"

static const unsigned MAX_WIDGETS = 8;
static TWidget s_Widgets[MAX_WIDGETS];
static TPage   s_Page;
static int     s_nYes = -1;
static int     s_nNo  = -1;
static unsigned s_nHeight;

unsigned ConfirmWidgets (TWidget **ppList)
{
    *ppList = s_Widgets;
    return s_Page.nCount;
}

TRect ConfirmDialog (void)
{
    return s_Page.Dialog;
}

TConfirmAnswer ConfirmOperate (int nIndex)
{
    if (nIndex == s_nYes) return ConfirmYes;
    if (nIndex == s_nNo)  return ConfirmNo;
    return ConfirmWaiting;
}

// The alert's own width, in the design units the theme was drawn against. Wide
// enough for a sentence to break two or three times and no wider: a line of
// text one has to sweep the head across is a line nobody finishes.
static const unsigned ALERT_WIDTH = 520;

// Taller than any sentence will need, for the trial pass: the measurement is
// how much of it went unused.
static const unsigned ALERT_TRIAL = 400;

void ConfirmDraw (TSurface *pSurface, const TConfirm *pConfirm)
{
    // Laid out twice: once to find out how tall the sentence turns out to be,
    // and once for real. An alert is the one screen that may size itself — it
    // interrupts, so it is allowed to be its own shape — and a fixed height
    // would leave a band of white under a short sentence and cut a long one.
    for (int nPass = 0; nPass < 2; nPass++)
    {
        PageBeginAlert (&s_Page, pSurface, s_Widgets, MAX_WIDGETS, ALERT_WIDTH,
                        nPass == 0 ? ALERT_TRIAL : s_nHeight, pConfirm->pTitle);
        const TTheme *pTheme = &s_Page.Theme;
        TLayout &Layout = s_Page.Layout;

        PageFooter (&s_Page);
        // Neither answer takes the focus: an alert with two of them has no
        // navigation to offer. Return is yes, Escape is no, and both work
        // wherever the hand happens to be — which is what a Macintosh alert
        // always promised. The default one used to wear two rings at once, the
        // one that means "Return does this" and the one that means "the
        // keyboard is here", and they said the same thing twice.
        s_nYes = PageLast (&s_Page, pConfirm->pYes, StateDefault | StateNoFocus);
        s_nNo  = pConfirm->pNo == 0 ? -1
                                    : PageLast (&s_Page, pConfirm->pNo, StateNoFocus);

        // The mark is two lines tall, which is what keeps it under the size at
        // which the typeface stops growing — and it is the typeface that draws
        // the marks' own letters.
        const unsigned nIcon = 2 * pTheme->M.nLineHeight;

        // The sentence starts at the mark's own top and runs beside it. It used
        // to be centred in whatever height was left, which put it below the
        // mark and made the two read as two separate things rather than as one
        // remark with a mark against it.
        const unsigned nBand = LayoutRoom (&Layout);
        TRow Body;
        RowBegin (&Body, pTheme, LayoutRow (&Layout, nBand, StateNormal));
        const TRect IconBox = Rect (Body.Free.nX, Body.Free.nY, nIcon, nIcon);
        PageAdd (&s_Page, WidgetIcon, IconBox, 0, StateNormal)
            ->Paint = pConfirm->Level == ConfirmNote ? OkapiaPaintNote
                    : pConfirm->Level == ConfirmStop ? OkapiaPaintStop
                                                     : OkapiaPaintCaution;
        RowNext (&Body, nIcon, StateNormal);
        RowSkip (&Body, pTheme->M.nGap);

        TRect Text = RowRest (&Body, StateNormal);
        const unsigned nWanted = GfxTextWrapHeight (pTheme->pBodyFont, Text.nWidth,
                                                    pConfirm->pBody,
                                                    pTheme->M.nLineHeight);
        const unsigned nHigh = nWanted > nIcon ? nWanted : nIcon;
        Text = Rect (Text.nX, Text.nY, Text.nWidth, nHigh);
        PageAdd (&s_Page, WidgetParagraph, Text, pConfirm->pBody, StateNormal);

        if (nPass == 0)
        {
            // What the first pass has learnt, back in design units: the trial
            // frame less the room the body turned out not to need.
            const unsigned nSpare = nBand > nHigh ? nBand - nHigh : 0;
            s_nHeight = ALERT_TRIAL - nSpare * 16 / s_Page.nScale16;
        }
    }
    ConfirmRepaint (pSurface);
}

void ConfirmRepaint (TSurface *pSurface)
{
    PagePaint (pSurface, &s_Page);
}
