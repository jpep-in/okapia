/*
 * okapia_specimen.cpp — every component, in every state, on one screen.
 *
 * There is not a coordinate in this file. Each section is handed a rectangle
 * with room left in it and spends what it needs; the sections are then flowed
 * in order and a page ends where the room does. The count is therefore asked
 * for and not declared — and it does not change with the display, since the
 * whole design scales with it: what a bigger screen buys is a bigger interface,
 * not more of it. A split decided by hand goes stale the first time a metric
 * changes; this one cannot.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_specimen.h"
#include "okapia_layout.h"
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
static void AddButton (TRow *pRow, const char *pText, unsigned nState)
{
    const unsigned nWidth = WidgetButtonWidth (pRow->pTheme, pText, nState);
    Add (WidgetButton, RowNext (pRow, nWidth, nState), pText, nState);
}

static void AddIconButton (TRow *pRow, TIconPainter Paint, unsigned nState)
{
    const unsigned nSize = pRow->pTheme->M.nButtonHeight;
    Add (WidgetIconButton, RowNext (pRow, nSize, nState), 0, nState)->Paint = Paint;
}

// A section's own heading, and the rule above it. Every section wears the same
// one, so the rhythm of the page is decided in a single place.
static void Heading (TLayout *pLayout, const char *pText)
{
    const TTheme *pTheme = pLayout->pTheme;
    Add (WidgetLabel, LayoutTop (pLayout, pTheme->M.nLineHeight), pText, StateStrong);
    LayoutRowGap (pLayout);
}

/*
 *  The sections
 *
 *  Each spends the room it is given and nothing more. None of them knows what
 *  page it is on, what precedes it or what follows: that is the flow's affair,
 *  and it is what lets a page break move without a line of this changing.
 */

static void SectionButtons (TLayout *pLayout)
{
    const TTheme *pTheme = pLayout->pTheme;
    Heading (pLayout, "Boutons");

    TRow Row;
    RowBegin (&Row, pTheme, LayoutRow (pLayout, pTheme->M.nButtonHeight, StateDefault));
    AddButton (&Row, "Normal",  StateNormal);
    AddButton (&Row, "Défaut",  StateDefault);
    AddButton (&Row, "Focus",   StateFocused);
    AddButton (&Row, "Enfoncé", StatePressed);
    AddButton (&Row, "Inactif", StateDisabled);
    AddIconButton (&Row, OkapiaPaintSettings, StateNormal);
    AddIconButton (&Row, OkapiaPaintPram,     StatePressed);
    AddIconButton (&Row, OkapiaPaintPower,    StateDisabled);
}

static void SectionChoices (TLayout *pLayout)
{
    const TTheme *pTheme = pLayout->pTheme;
    Heading (pLayout, "Cases et boutons radio");

    static const char *const Checks[] = { "Décochée", "Cochée", "Focus", "Inactive" };
    static const unsigned CheckStates[] =
    {
        StateNormal, StateChecked, StateChecked | StateFocused, StateDisabled | StateChecked
    };
    static const char *const Radios[] = { "Par défaut", "Autre", "Focus", "Inactif" };
    static const unsigned RadioStates[] =
    {
        StateChecked, StateNormal, StateFocused, StateDisabled
    };

    const unsigned nGutter = pTheme->M.nGap + 2 * ThemeReach (pTheme, StateFocused);

    TRect Band = LayoutRow (pLayout, pTheme->M.nCheckSize, StateFocused);
    for (unsigned i = 0; i < 4; i++)
    {
        Add (WidgetCheckbox, RectColumn (Band, i, 4, nGutter), Checks[i], CheckStates[i]);
    }
    LayoutRowGap (pLayout);

    Band = LayoutRow (pLayout, pTheme->M.nCheckSize, StateFocused);
    for (unsigned i = 0; i < 4; i++)
    {
        Add (WidgetRadio, RectColumn (Band, i, 4, nGutter), Radios[i], RadioStates[i]);
    }
}

static void SectionList (TLayout *pLayout)
{
    const TTheme *pTheme = pLayout->pTheme;
    Heading (pLayout, "Liste, ascenseur, saisie");

    const unsigned nRow   = pTheme->M.nRowHeight;
    const unsigned nReach = ThemeReach (pTheme, StateFocused);

    // The band is as tall as the taller of the two things standing in it. The
    // column's height is asked of the theme rather than measured off a
    // screenshot: four controls, each reserving what a ring draws above and
    // below it, and three gaps between them.
    const unsigned nList   = 3 * nRow + 2;
    const unsigned nColumn = 2 * pTheme->M.nButtonHeight + 2 * pTheme->M.nFieldHeight
                           + 8 * nReach + 3 * pTheme->M.nRowGap;

    TRow Row;
    RowBegin (&Row, pTheme,
              LayoutRow (pLayout, nList > nColumn ? nList : nColumn, StateFocused));

    // Fractions of what is there, never design constants: the content narrowed
    // when the margin was fixed, and a constant 366 would have pushed the
    // right-hand column straight into the frame.
    const TRect List = Rect (Row.Free.nX, Row.Free.nY,
                             Row.Free.nWidth * 56 / 100, nList);
    RowNext (&Row, List.nWidth, StateNormal);
    Add (WidgetListFrame, List, 0, StateNormal);

    static const char *const Rows[] =
    {
        "Système 7.1.2 — Macintosh IIci",
        // Longer than the row, on purpose: a volume is named by whoever
        // formatted it, and the cut has to be on the page and not only in a
        // test. Every screen of this firmware will meet one of these.
        "Mac OS 8.1 français — Quadra 900 avec disque de démarrage",
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
                         Rect (Row.Free.nX, List.nY, pTheme->M.nScrollbarWidth, nList),
                         0, StateNormal);
    RowAbut (&Row, pTheme->M.nScrollbarWidth);
    pBar->nValue = 1;
    pBar->nSpan  = 3;

    // The right-hand column is a layout of its own inside what the row has
    // left. Nesting is the whole reason the model is a rectangle being spent
    // rather than a running ordinate.
    TLayout Column;
    LayoutBegin (&Column, pTheme, RowRest (&Row, StateFocused));

    Add (WidgetPopup, LayoutRow (&Column, pTheme->M.nButtonHeight, StateFocused),
         "Dynamique", StateNormal);
    LayoutRowGap (&Column);
    Add (WidgetPopup, LayoutRow (&Column, pTheme->M.nButtonHeight, StateFocused),
         "HDMI", StateFocused);
    LayoutRowGap (&Column);
    Add (WidgetField, LayoutRow (&Column, pTheme->M.nFieldHeight, StateFocused),
         "Okapia", StateNormal);
    LayoutRowGap (&Column);
    Add (WidgetField, LayoutRow (&Column, pTheme->M.nFieldHeight, StateFocused),
         "Macintosh HD — disque de démarrage", StateFocused);
}

static void SectionProgress (TLayout *pLayout)
{
    const TTheme *pTheme = pLayout->pTheme;
    Heading (pLayout, "Progression");

    static const unsigned Steps[] = { 0, 250, 620, 1000 };
    for (unsigned i = 0; i < 4; i++)
    {
        Add (WidgetProgress, LayoutTop (pLayout, pTheme->M.nProgressHeight), 0,
             StateNormal)->nValue = Steps[i];
        if (i < 3)
        {
            LayoutRowGap (pLayout);
        }
    }
}

static void SectionTypography (TLayout *pLayout)
{
    const TTheme *pTheme = pLayout->pTheme;
    Heading (pLayout, "Typographie");

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
        Add (WidgetLabel, LayoutTop (pLayout, pTheme->M.nLineHeight), Lines[i],
             i == 1 ? StateStrong : StateNormal);
    }
}

static void SectionStates (TLayout *pLayout)
{
    const TTheme *pTheme = pLayout->pTheme;
    Heading (pLayout, "Un même libellé, tous ses états");

    TRow Row;
    RowBegin (&Row, pTheme, LayoutRow (pLayout, pTheme->M.nButtonHeight, StateDefault));
    AddButton (&Row, "Réglages", StateNormal);
    AddButton (&Row, "Réglages", StateDefault);
    AddButton (&Row, "Réglages", StateFocused);
    AddButton (&Row, "Réglages", StatePressed);
    AddButton (&Row, "Réglages", StateDisabled);
}

typedef void TSection (TLayout *pLayout);

static TSection *const s_Sections[] =
{
    SectionButtons, SectionChoices, SectionList,
    SectionProgress, SectionTypography, SectionStates
};
static const unsigned SECTIONS = sizeof s_Sections / sizeof s_Sections[0];

// Places sections from nFrom until the room runs out, and answers the first one
// that did not fit. A section that overflows is rolled back whole rather than
// shaved: shaving is what four rounds of crowding were, and the page was the
// thing at fault, not the contents.
static unsigned Flow (TLayout *pLayout, unsigned nFrom)
{
    unsigned i = nFrom;
    for (; i < SECTIONS; i++)
    {
        const TLayout  Saved = *pLayout;
        const unsigned nMark = s_nWidgets;

        if (i > nFrom)
        {
            LayoutSectionGap (pLayout);
            Add (WidgetSeparator, LayoutTop (pLayout, 1), 0, StateNormal);
            LayoutRowGap (pLayout);
        }
        s_Sections[i] (pLayout);

        if (pLayout->bOverflow)
        {
            *pLayout   = Saved;
            s_nWidgets = nMark;
            break;
        }
    }
    return i;
}

// The page's own frame: title at the top, footer at the bottom, and the middle
// handed to the flow. The footer is claimed *before* the middle is filled,
// which is the whole reason the lower half stopped ending up in the border.
static void PageFrame (TLayout *pLayout, const TTheme *pTheme, const TRect &rContent,
                       unsigned nPage, unsigned nPages)
{
    LayoutBegin (pLayout, pTheme, rContent);

    Add (WidgetTitle, LayoutTop (pLayout, pTheme->pTitleFont->nHeight),
         "Échantillon — le système de composants", StateNormal);
    LayoutRowGap (pLayout);
    Add (WidgetSeparator, LayoutTop (pLayout, 1), 0, StateNormal);
    LayoutSectionGap (pLayout);

    TRow Foot;
    RowBegin (&Foot, pTheme,
              LayoutBottom (pLayout, pTheme->M.nButtonHeight
                                     + 2 * ThemeReach (pTheme, StateDefault)));
    Foot.Free = Rect (Foot.Free.nX,
                      Foot.Free.nY + (int) ThemeReach (pTheme, StateDefault),
                      Foot.Free.nWidth, pTheme->M.nButtonHeight);

    AddIconButton (&Foot, OkapiaPaintSettings, StateNormal);
    AddIconButton (&Foot, OkapiaPaintPram,     StateNormal);
    AddIconButton (&Foot, OkapiaPaintPower,    StateNormal);

    const unsigned nStart = WidgetButtonWidth (pTheme, "Démarrer", StateDefault);
    Add (WidgetButton, RowLast (&Foot, nStart, StateDefault), "Démarrer", StateDefault);

    // Which page of how many, in the space the footer has left over. It is only
    // ever seen when the components need more than one, which is the honest
    // place to say so.
    if (nPages > 1)
    {
        static char s_Page[] = "0 / 0";
        s_Page[0] = (char) ('0' + (nPage + 1) % 10);
        s_Page[4] = (char) ('0' + nPages % 10);
        RowSkip (&Foot, pTheme->M.nGap * 2);
        Add (WidgetLabel, RowRest (&Foot, StateNormal), s_Page, StateNormal);
    }

    LayoutSectionGap (pLayout);
    Add (WidgetSeparator, LayoutBottom (pLayout, 1), 0, StateNormal);
    LayoutSectionGap (pLayout);
}

static TRect ContentFor (TSurface *pSurface, const TTheme *pTheme)
{
    const unsigned nDW = (unsigned) S (608);
    const unsigned nDH = (unsigned) S (458);
    s_Dialog = Rect ((int) (pSurface->nWidth  - nDW) / 2,
                     (int) (pSurface->nHeight - nDH) / 2, nDW, nDH);
    return ThemeContent (s_Dialog, pTheme);
}

unsigned SpecimenPageCount (TSurface *pSurface)
{
    TTheme Theme;
    s_nScale16 = ThemeScaleFor (pSurface->nWidth, pSurface->nHeight);
    ThemeMake (s_nScale16, &Theme);
    const TRect Content = ContentFor (pSurface, &Theme);

    const unsigned nSaved = s_nWidgets;
    unsigned nPages = 0;
    for (unsigned nFrom = 0; nFrom < SECTIONS; nPages++)
    {
        TLayout Layout;
        s_nWidgets = 0;
        PageFrame (&Layout, &Theme, Content, nPages, 1);
        const unsigned nNext = Flow (&Layout, nFrom);
        if (nNext == nFrom)
        {
            nPages++;                   // a section too tall for any page: say so
            break;
        }
        nFrom = nNext;
    }
    s_nWidgets = nSaved;
    return nPages == 0 ? 1 : nPages;
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
    const TRect Content = ContentFor (pSurface, &Theme);
    const unsigned nPages = SpecimenPageCount (pSurface);
    if (nPage >= nPages)
    {
        nPage = 0;
    }

    // The pages before this one are flowed and thrown away. It costs nothing —
    // laying out places no pixels — and it is what keeps a page from having to
    // know where the one before it stopped.
    unsigned nFrom = 0;
    for (unsigned i = 0; i < nPage; i++)
    {
        TLayout Skip;
        s_nWidgets = 0;
        PageFrame (&Skip, &Theme, Content, i, nPages);
        nFrom = Flow (&Skip, nFrom);
    }

    TLayout Layout;
    s_nWidgets = 0;
    PageFrame (&Layout, &Theme, Content, nPage, nPages);
    Flow (&Layout, nFrom);

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
