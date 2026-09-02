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
#include "okapia_strings.h"
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
    WidgetClear (p);
    p->Type   = Type;
    p->Rect   = rRect;
    p->pText  = pText;
    p->nState = nState;
    return p;
}

// Buttons are laid out from their own labels, never from fixed rectangles.
// "Enregistrer et redémarrer" and "Save and restart" are not the same width,
// and a table of hard-coded rectangles is precisely how translated Macintosh
// dialogues used to come apart.
// Placed with the room a focus ring needs, whatever state they are drawn in:
// the layout runs before the loop and cannot know which control will be
// focused, and one that can be will be. See okapia_layout.h.
static void AddButton (TRow *pRow, const char *pText, unsigned nState)
{
    const unsigned nWidth = WidgetButtonWidth (pRow->pTheme, pText, nState);
    Add (WidgetButton, RowNext (pRow, nWidth, nState | StateFocused), pText, nState);
}

static void AddIconButton (TRow *pRow, TIconPainter Paint, unsigned nState)
{
    const unsigned nSize = pRow->pTheme->M.nButtonHeight;
    Add (WidgetIconButton, RowNext (pRow, nSize, nState | StateFocused), 0, nState)
        ->Paint = Paint;
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
    Heading (pLayout, Str (StrSpecButtons));

    TRow Row;
    RowBegin (&Row, pTheme, LayoutRow (pLayout, pTheme->M.nButtonHeight, StateDefault));
    AddButton (&Row, Str (StrSpecNormal),   StateNormal);
    AddButton (&Row, Str (StrSpecDefault),  StateDefault);
    AddButton (&Row, Str (StrSpecFocus),    StateFocused);
    AddButton (&Row, Str (StrSpecPressed),  StatePressed);
    AddButton (&Row, Str (StrSpecDisabled), StateDisabled);
    AddIconButton (&Row, OkapiaPaintSettings, StateNormal);
    AddIconButton (&Row, OkapiaPaintPram,     StatePressed);
    AddIconButton (&Row, OkapiaPaintPower,    StateDisabled);
}

static void SectionChoices (TLayout *pLayout)
{
    const TTheme *pTheme = pLayout->pTheme;
    Heading (pLayout, Str (StrSpecChoices));

    const char *const Checks[] =
    {
        Str (StrSpecUnchecked), Str (StrSpecChecked), Str (StrSpecFocus), Str (StrSpecInactive)
    };
    static const unsigned CheckStates[] =
    {
        StateNormal, StateChecked, StateChecked | StateFocused, StateDisabled | StateChecked
    };
    const char *const Radios[] =
    {
        Str (StrSpecDefault), Str (StrSpecOther), Str (StrSpecFocus), Str (StrSpecDisabled)
    };
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
    Heading (pLayout, Str (StrSpecList));

    const unsigned nRow   = pTheme->M.nRowHeight;
    const unsigned nReach = ThemeReach (pTheme, StateFocused);

    // More systems than the list can show, so that the scroller is doing
    // something rather than standing there for the look of it. Two of them are
    // out of reach, which a chooser will have as soon as a volume has no
    // System on it.
    // Static, and filled here rather than initialised: the list keeps a pointer
    // to these for as long as it is on the screen, so a local array would be
    // gone by the time anything drew it. The text pointers themselves are safe
    // — Str() answers into a table that is compiled in and outlives everything.
    static TListItem Items[7];
    static const TGlyphImage *const Art[7] =
    {
        &OkapiaIconSystem7, &OkapiaIconMacOS8, &OkapiaIconSystem6,
        &OkapiaIconMacOS9,  &OkapiaIconSystem7, 0, 0
    };
    static const char *const Names[7] =
    {
        "Système 7.1.2 — Macintosh IIci",
        // Longer than the row, on purpose: a volume is named by whoever
        // formatted it, and the cut has to be on the page and not only in a
        // test. Every screen of this firmware will meet one of these.
        "Mac OS 8.1 français — Quadra 900 avec disque de démarrage",
        "Système 6.0.8 — Macintosh IIci",
        "Mac OS 9.1 — Power Macintosh",
        0, 0, 0
    };
    for (unsigned i = 0; i < 7; i++)
    {
        Items[i].pText  = Names[i] != 0 ? Names[i]
                                        : Str (i == 4 ? StrRescueDisk : StrNoSystem);
        Items[i].pIcon  = Art[i];
        Items[i].nState = i >= 5 ? StateDisabled : StateNormal;
    }

    // The band is as tall as the taller of the two things standing in it. The
    // column's height is asked of the theme rather than measured off a
    // screenshot: four controls, each reserving what a ring draws above and
    // below it, and three gaps between them.
    const unsigned nList   = 4 * nRow + 2;
    const unsigned nColumn = 2 * pTheme->M.nButtonHeight + 2 * pTheme->M.nFieldHeight
                           + 8 * nReach + 3 * pTheme->M.nRowGap;

    TRow Row;
    RowBegin (&Row, pTheme,
              LayoutRow (pLayout, nList > nColumn ? nList : nColumn, StateFocused));

    // Fractions of what is there, never design constants: the content narrowed
    // when the margin was fixed, and a constant 366 would have pushed the
    // right-hand column straight into the frame.
    const TRect Frame = Rect (Row.Free.nX, Row.Free.nY,
                              Row.Free.nWidth * 56 / 100, nList);
    RowNext (&Row, Frame.nWidth, StateFocused);

    TWidget *pList = Add (WidgetList, Frame, 0, StateNormal);
    pList->pItems  = Items;
    pList->nItems  = sizeof Items / sizeof Items[0];
    pList->nChoice = 1;

    // The right-hand column is a layout of its own inside what the row has
    // left. Nesting is the whole reason the model is a rectangle being spent
    // rather than a running ordinate.
    TLayout Column;
    LayoutBegin (&Column, pTheme, RowRest (&Row, StateFocused));

    // Real choices, so the menu has something to show. A pop-up carrying a
    // fixed string is a button wearing a triangle.
    static TListItem Rates[5];
    static const char *const RateNames[5] =
    {
        0, "60 Hz", "30 Hz", "15 Hz", "10 Hz"
    };
    for (unsigned i = 0; i < 5; i++)
    {
        Rates[i].pText  = RateNames[i] != 0 ? RateNames[i] : Str (StrDynamic);
        Rates[i].pIcon  = 0;
        Rates[i].nState = StateNormal;
    }

    static TListItem Outputs[4];
    static const char *const OutputNames[4] = { 0, "HDMI", "Jack", "USB" };
    for (unsigned i = 0; i < 4; i++)
    {
        Outputs[i].pText  = OutputNames[i] != 0 ? OutputNames[i] : Str (StrSoundOff);
        Outputs[i].pIcon  = 0;
        Outputs[i].nState = i == 3 ? StateDisabled : StateNormal;
    }

    TWidget *pRate = Add (WidgetPopup,
                          LayoutRow (&Column, pTheme->M.nButtonHeight, StateFocused),
                          0, StateNormal);
    pRate->pItems  = Rates;
    pRate->nItems  = sizeof Rates / sizeof Rates[0];
    pRate->nChoice = 0;
    LayoutRowGap (&Column);

    TWidget *pOut = Add (WidgetPopup,
                         LayoutRow (&Column, pTheme->M.nButtonHeight, StateFocused),
                         0, StateNormal);
    pOut->pItems  = Outputs;
    pOut->nItems  = sizeof Outputs / sizeof Outputs[0];
    pOut->nChoice = 1;
    LayoutRowGap (&Column);

    // A field with storage of its own is an editable one; a field without is
    // read-only, which is what every other control here is. Twenty-seven bytes
    // because a volume's name ends up in a Pascal string in the volume record.
    static char s_Shared[28] = "Okapia";
    TWidget *pEdit = Add (WidgetField,
                          LayoutRow (&Column, pTheme->M.nFieldHeight, StateFocused),
                          s_Shared, StateFocused);
    pEdit->pEdit     = s_Shared;
    pEdit->nEditSize = sizeof s_Shared;
    pEdit->nCaret    = 6;

    LayoutRowGap (&Column);
    Add (WidgetField, LayoutRow (&Column, pTheme->M.nFieldHeight, StateFocused),
         "Macintosh HD — disque de démarrage", StateNormal);
}

// What an alert looks like, drawn where it will be read: inside the page rather
// than as a page of its own, because the question is whether it reads as more
// urgent than what surrounds it, and that cannot be judged alone.
static void SectionAlert (TLayout *pLayout)
{
    const TTheme *pTheme = pLayout->pTheme;
    Heading (pLayout, Str (StrSpecAlert));

    const char *const Message = Str (StrRepairBody);

    // Two lines tall, which is about what the panel this follows used and, not
    // by accident, about as large as the mark inside it can be set: the ladder
    // of faces tops out at a capital of twenty-five pixels, so a triangle much
    // bigger than this one gets an exclamation lost inside it.
    const unsigned nIcon = 2 * pTheme->M.nLineHeight;
    const unsigned nText = GfxTextWrapHeight (pTheme->pBodyFont,
                                              pLayout->Free.nWidth - 2 * pTheme->M.nMargin
                                                  - nIcon - pTheme->M.nGap * 2,
                                              Message, pTheme->M.nLineHeight);
    const unsigned nBody = nText > nIcon ? nText : nIcon;
    const unsigned nFrame = nBody + pTheme->M.nSectionGap + pTheme->M.nButtonHeight
                          + 2 * pTheme->M.nMargin + 2 * ThemeReach (pTheme, StateDefault);

    const TRect Frame = LayoutTop (pLayout, nFrame);
    Add (WidgetAlert, Frame, 0, StateNormal);

    TLayout Inside;
    LayoutBegin (&Inside, pTheme, RectInset (Frame, (int) pTheme->M.nMargin,
                                             (int) pTheme->M.nMargin));

    TRow Body;
    RowBegin (&Body, pTheme, LayoutTop (&Inside, nBody));
    Add (WidgetIcon, Rect (Body.Free.nX, Body.Free.nY, nIcon, nIcon), 0,
         StateNormal)->Paint = OkapiaPaintCaution;
    RowNext (&Body, nIcon, StateNormal);
    RowSkip (&Body, pTheme->M.nGap);
    Add (WidgetParagraph, RowRest (&Body, StateNormal), Message, StateNormal);

    LayoutSectionGap (&Inside);

    TRow Buttons;
    RowBegin (&Buttons, pTheme, LayoutRow (&Inside, pTheme->M.nButtonHeight, StateDefault));
    Add (WidgetButton,
         RowLast (&Buttons, WidgetButtonWidth (pTheme, Str (StrRepairNow), StateDefault),
                  StateDefault | StateFocused),
         Str (StrRepairNow), StateDefault);
    Add (WidgetButton,
         RowLast (&Buttons, WidgetButtonWidth (pTheme, Str (StrRepairSkip), StateNormal),
                  StateFocused),
         Str (StrRepairSkip), StateNormal);
}

static void SectionProgress (TLayout *pLayout)
{
    const TTheme *pTheme = pLayout->pTheme;
    Heading (pLayout, Str (StrSpecProgress));

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
    Heading (pLayout, Str (StrSpecTypography));

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
    Heading (pLayout, Str (StrSpecStates));

    TRow Row;
    RowBegin (&Row, pTheme, LayoutRow (pLayout, pTheme->M.nButtonHeight, StateDefault));
    AddButton (&Row, Str (StrSettings), StateNormal);
    AddButton (&Row, Str (StrSettings), StateDefault);
    AddButton (&Row, Str (StrSettings), StateFocused);
    AddButton (&Row, Str (StrSettings), StatePressed);
    AddButton (&Row, Str (StrSettings), StateDisabled);
}

typedef void TSection (TLayout *pLayout);

static TSection *const s_Sections[] =
{
    SectionButtons, SectionChoices, SectionList, SectionAlert,
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
         Str (StrSpecTitle), StateNormal);
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

    const unsigned nStart = WidgetButtonWidth (pTheme, Str (StrStart), StateDefault);
    Add (WidgetButton, RowLast (&Foot, nStart, StateDefault | StateFocused),
         Str (StrStart), StateDefault);

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

    // Upwards from the footer, since that is the direction it was built in.
    LayoutSectionGapBottom (pLayout);
    Add (WidgetSeparator, LayoutBottom (pLayout, 1), 0, StateNormal);
    LayoutSectionGapBottom (pLayout);
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
