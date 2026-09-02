/*
 * okapia_specimen.cpp — every component, in every state, on one screen.
 *
 * The layout is written in the design's own units and multiplied by the theme's
 * scale, so the screen is composed for the display it lands on rather than
 * blown up from a small one. The rectangles are still placed by hand here; a
 * row-and-column box model is what will replace that, and it is the same work
 * that will let a translation change a button's width without moving anything
 * else.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_specimen.h"
#include "okapia_widgets.h"

// Enough for this screen twice over, and it costs nothing: the firmware never
// allocates, here or anywhere else.
static const unsigned MAX_WIDGETS = 96;
static TWidget  s_Widgets[MAX_WIDGETS];
static unsigned s_nWidgets;
static unsigned s_nScale16 = 16;
static TRect    s_Dialog;

// Design units to surface pixels.
static inline int S (int nValue)
{
    return nValue * (int) s_nScale16 / 16;
}

static TWidget *Add (TWidgetType Type, const TRect &rRect, const char *pText, unsigned nState)
{
    if (s_nWidgets >= MAX_WIDGETS)
    {
        return &s_Widgets[MAX_WIDGETS - 1];         // never silently past the end
    }
    TWidget *p = &s_Widgets[s_nWidgets++];
    p->Type   = Type;
    p->Rect   = rRect;
    p->pText  = pText;
    p->nState = nState;
    p->nValue = 0;
    p->nSpan  = 0;
    p->pIcon  = 0;
    p->Paint  = 0;
    p->nGroup = 0;
    return p;
}

// Buttons are laid out from their own labels, never from fixed rectangles.
// "Enregistrer et redémarrer" and "Save and restart" are not the same width,
// and a table of hard-coded rectangles is precisely how translated Macintosh
// dialogues used to come apart.
static int AddButton (const TTheme *pTheme, int nX, int nY, const char *pText, unsigned nState)
{
    const unsigned nWidth = WidgetButtonWidth (pTheme, pText, nState);
    Add (WidgetButton, Rect (nX, nY, nWidth, pTheme->M.nButtonHeight), pText, nState);
    return nX + (int) nWidth;
}

TRect SpecimenDialog (void)
{
    return s_Dialog;
}

unsigned SpecimenWidgets (TWidget **ppList)
{
    *ppList = s_Widgets;
    return s_nWidgets;
}

void SpecimenDraw (TSurface *pSurface, unsigned nPage)
{
    TTheme Theme;
    s_nScale16 = ThemeScaleFor (pSurface->nWidth, pSurface->nHeight);
    ThemeMake (s_nScale16, &Theme);
    const TTheme *pTheme = &Theme;

    s_nWidgets = 0;

    // The dialogue keeps the design's proportions and is centred on whatever
    // the surface turns out to be.
    const unsigned nDW = (unsigned) S (608);
    const unsigned nDH = (unsigned) S (458);
    const TRect Dialog = Rect ((int) (pSurface->nWidth  - nDW) / 2,
                               (int) (pSurface->nHeight - nDH) / 2, nDW, nDH);
    s_Dialog = Dialog;

    // Everything flows from the content rectangle: down from its top, and the
    // footer anchored to its bottom. Absolute ordinates measured from the
    // dialogue's own edge is what let the lower half spill into the frame the
    // moment the margin changed — a layout that only holds for one set of
    // metrics is not a layout.
    const TRect Content = ThemeContent (Dialog, pTheme);
    const int nLeft  = Content.nX;
    const int nRight = Content.nX + (int) Content.nWidth;
    const unsigned nInner = Content.nWidth;
    const unsigned nGap = pTheme->M.nGap;
    const unsigned nBH  = pTheme->M.nButtonHeight;
    const unsigned nLH  = pTheme->M.nLineHeight;
    const unsigned nRowGap  = pTheme->M.nRowGap;
    const unsigned nSection = pTheme->M.nSectionGap;
    const int      nRing    = (int) ThemeReach (pTheme, StateFocused);
    const unsigned nIcon = nBH;
    const unsigned nRow  = pTheme->M.nRowHeight;

    // The footer first, so the rest knows where it must stop — and inset by
    // what the default button's ring will draw outside itself, or the ring
    // lands in the margin the content rectangle was supposed to keep clear.
    const int nReach = (int) ThemeReach (pTheme, StateDefault);
    const int yFoot = Content.nY + (int) Content.nHeight - (int) nIcon - nReach;
    const int ySep2 = yFoot - (int) pTheme->M.nSectionGap - 1;

    int y = Content.nY;

    Add (WidgetTitle, Rect (nLeft, y, nInner, pTheme->pTitleFont->nHeight),
         nPage == 0 ? "Échantillon — contrôles" : "Échantillon — états et typographie",
         StateNormal);
    y += (int) pTheme->pTitleFont->nHeight + (int) nRowGap;
    Add (WidgetSeparator, Rect (nLeft, y, nInner, 1), 0, StateNormal);
    y += (int) nSection;

    if (nPage != 0)
    {
        /* Progression */
        Add (WidgetLabel, Rect (nLeft, y, nInner, nLH), "Progression", StateStrong);
        y += (int) nLH + (int) nRowGap;
        static const unsigned Steps[] = { 0, 250, 620, 1000 };
        for (unsigned i = 0; i < 4; i++)
        {
            Add (WidgetProgress, Rect (nLeft, y, nInner, pTheme->M.nProgressHeight), 0,
                 StateNormal)->nValue = Steps[i];
            y += (int) pTheme->M.nProgressHeight + (int) nRowGap;
        }
        y += (int) nSection - (int) nRowGap;

        /* Typographie */
        Add (WidgetSeparator, Rect (nLeft, y, nInner, 1), 0, StateNormal);
        y += (int) nRowGap;
        Add (WidgetLabel, Rect (nLeft, y, nInner, nLH), "Typographie", StateStrong);
        y += (int) nLH + (int) nRowGap;

        static const char *const Lines[] =
        {
            "Choisissez un système à démarrer.",
            "Le volume n’a pas été démonté proprement. Réparer ?",
            "ÀÂÇÈÉÊËÎÏÔÙÛ  àâçèéêëîïôùû  ŒŒ œuvre",
            "« Guillemets », tirets — et points de suspension…",
            "0123456789  ()[]{}  ,.;:!?  /\\  +-*=  %&@#"
        };
        for (unsigned i = 0; i < 5; i++)
        {
            Add (WidgetLabel, Rect (nLeft, y, nInner, nLH), Lines[i],
                 i == 1 ? StateStrong : StateNormal);
            y += (int) nLH;
        }
        y += (int) nSection;

        /* États d'un même bouton, côte à côte */
        Add (WidgetSeparator, Rect (nLeft, y, nInner, 1), 0, StateNormal);
        y += (int) nRowGap;
        Add (WidgetLabel, Rect (nLeft, y, nInner, nLH), "Un même libellé, tous ses états",
             StateStrong);
        y += (int) nLH + (int) nRowGap + nRing;

        int xs = nLeft;
        xs = AddButton (pTheme, xs, y, "Réglages", StateNormal)   + (int) (nGap * 2);
        xs = AddButton (pTheme, xs, y, "Réglages", StateDefault)  + (int) (nGap * 2);
        xs = AddButton (pTheme, xs, y, "Réglages", StateFocused)  + (int) (nGap * 2);
        xs = AddButton (pTheme, xs, y, "Réglages", StatePressed)  + (int) (nGap * 2);
        AddButton (pTheme, xs, y, "Réglages", StateDisabled);
    }
    else
    {
        /* Boutons */
        Add (WidgetLabel, Rect (nLeft, y, nInner, nLH), "Boutons", StateStrong);
        y += (int) nLH + (int) nRowGap;

        // Rows reserve what a ring will draw outside its control, above and below.
        // Two rows an ordinary gap apart still collide when both wear one.
        int x = nLeft;
        y += nRing;
        x = AddButton (pTheme, x, y, "Normal", StateNormal)    + (int) (nGap * 2);
        x = AddButton (pTheme, x, y, "Défaut", StateDefault)   + (int) (nGap * 2);
        x = AddButton (pTheme, x, y, "Focus", StateFocused)    + (int) (nGap * 2);
        x = AddButton (pTheme, x, y, "Enfoncé", StatePressed)  + (int) (nGap * 2);
        x = AddButton (pTheme, x, y, "Inactif", StateDisabled) + (int) (nGap * 3);

        Add (WidgetIconButton, Rect (x, y, nIcon, nIcon), 0, StateNormal)->Paint
            = OkapiaPaintSettings;
        Add (WidgetIconButton, Rect (x + (int) (nIcon + nGap), y, nIcon, nIcon), 0,
             StatePressed)->Paint = OkapiaPaintPram;
        Add (WidgetIconButton, Rect (x + (int) (2 * (nIcon + nGap)), y, nIcon, nIcon), 0,
             StateDisabled)->Paint = OkapiaPaintPower;
        y += (int) nBH + nRing + (int) nSection;

        /* Cases et boutons radio */
        Add (WidgetLabel, Rect (nLeft, y, nInner, nLH), "Cases et boutons radio", StateStrong);
        y += (int) nLH + (int) nRowGap;

        // A real gutter between the columns, not a shared edge: four rectangles that
        // exactly tile the width touch, and each one's focus ring then reaches into
        // its neighbour.
        const unsigned nCol4 = nInner / 4;
        const unsigned nCellW = nCol4 - nGap;
        const unsigned nCH = pTheme->M.nCheckSize + 2 * (unsigned) nRing;
        Add (WidgetCheckbox, Rect (nLeft,                   y, nCellW, nCH), "Décochée", StateNormal);
        Add (WidgetCheckbox, Rect (nLeft + (int) nCol4,     y, nCellW, nCH), "Cochée",   StateChecked);
        Add (WidgetCheckbox, Rect (nLeft + (int) nCol4 * 2, y, nCellW, nCH), "Focus",
             StateChecked | StateFocused);
        Add (WidgetCheckbox, Rect (nLeft + (int) nCol4 * 3, y, nCellW, nCH), "Inactive",
             StateDisabled | StateChecked);
        y += (int) nCH + (int) nRowGap;

        Add (WidgetRadio, Rect (nLeft,                   y, nCellW, nCH), "Par défaut", StateChecked);
        Add (WidgetRadio, Rect (nLeft + (int) nCol4,     y, nCellW, nCH), "Autre",      StateNormal);
        Add (WidgetRadio, Rect (nLeft + (int) nCol4 * 2, y, nCellW, nCH), "Focus",      StateFocused);
        Add (WidgetRadio, Rect (nLeft + (int) nCol4 * 3, y, nCellW, nCH), "Inactif",    StateDisabled);
        y += (int) nCH + (int) nSection;

        /* Liste, ascenseur, colonne de saisie */
        Add (WidgetLabel, Rect (nLeft, y, nInner, nLH), "Liste, ascenseur, saisie", StateStrong);
        y += (int) nLH + (int) nRowGap;

        // Widths as fractions of the content, never as design constants: the
        // content narrowed when the margin was fixed, and a fixed 366 would have
        // pushed the right-hand column straight into the frame.
        const unsigned nBar = pTheme->M.nScrollbarWidth;
        const unsigned nListW = nInner * 56 / 100;
        const TRect List = Rect (nLeft, y, nListW, 3 * nRow + 2);
        Add (WidgetListFrame, List, 0, StateNormal);
        static const char *const Rows[] =
        {
            "Système 7.1.2 — Macintosh IIci",
            "Mac OS 8.1 — Quadra 900",
            "Système 6.0.8 — Macintosh IIci"
        };
        static const TGlyphImage *const Icons[] =
        {
            &OkapiaIconSystem7, &OkapiaIconMacOS8, &OkapiaIconSystem6
        };
        for (unsigned i = 0; i < 3; i++)
        {
            Add (WidgetListRow,
                 Rect (List.nX + 1, List.nY + 1 + (int) (i * nRow), List.nWidth - 2, nRow),
                 Rows[i], i == 1 ? StateSelected : StateNormal)->pIcon = Icons[i];
        }

        TWidget *pBar = Add (WidgetScrollbar,
                             Rect (List.nX + (int) nListW + (int) nGap / 2, List.nY,
                                   nBar, List.nHeight), 0, StateNormal);
        pBar->nValue = 1;
        pBar->nSpan  = 3;

        const int nCol = List.nX + (int) nListW + (int) nGap / 2 + (int) nBar + (int) nGap * 2;
        const unsigned nColW = (unsigned) (nRight - nCol
                                           - (int) ThemeReach (pTheme, StateFocused));
        int yc = y;
        Add (WidgetPopup, Rect (nCol, yc, nColW, nBH), "Dynamique", StateNormal);
        yc += (int) nBH + (int) nRowGap + nRing;
        Add (WidgetPopup, Rect (nCol, yc, nColW, nBH), "HDMI", StateFocused);
        yc += (int) nBH + (int) nRowGap + nRing;
        Add (WidgetField, Rect (nCol, yc, nColW, pTheme->M.nFieldHeight), "Okapia", StateNormal);
        yc += (int) pTheme->M.nFieldHeight + (int) nRowGap + nRing;
        Add (WidgetField, Rect (nCol, yc, nColW, pTheme->M.nFieldHeight),
             "Macintosh HD", StateFocused);

        // The meter and the typography live on the second page. Crowding them in
        // here is what four rounds of shaving were trying to avoid, and the
        // shaving was the wrong move: the page was.
        y += (int) List.nHeight;
    }

    /* Pied de dialogue, ancré en bas du contenu */
    Add (WidgetSeparator, Rect (nLeft, ySep2, nInner, 1), 0, StateNormal);
    Add (WidgetIconButton, Rect (nLeft, yFoot, nIcon, nIcon), 0, StateNormal)->Paint
        = OkapiaPaintSettings;
    Add (WidgetIconButton, Rect (nLeft + (int) (nIcon + nGap), yFoot, nIcon, nIcon), 0,
         StateNormal)->Paint = OkapiaPaintPram;
    Add (WidgetIconButton, Rect (nLeft + (int) (2 * (nIcon + nGap)), yFoot, nIcon, nIcon), 0,
         StateNormal)->Paint = OkapiaPaintPower;

    const unsigned nStart = WidgetButtonWidth (pTheme, "Démarrer", StateDefault);
    AddButton (pTheme, nRight - nReach - (int) nStart, yFoot, "Démarrer", StateDefault);

    SpecimenRepaint (pSurface);
}

void SpecimenRepaint (TSurface *pSurface)
{
    TTheme Theme;
    ThemeMake (ThemeScaleFor (pSurface->nWidth, pSurface->nHeight), &Theme);
    Theme.DrawDesktop (pSurface, &Theme);
    Theme.DrawDialog (pSurface, s_Dialog, &Theme);
    WidgetDrawAll (pSurface, &Theme, s_Widgets, s_nWidgets);
}
