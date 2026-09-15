/*
 * check_screen.cpp — drive the interface with no screen and no keyboard.
 *
 * The event loop is the first part of the firmware whose whole behaviour is
 * decidable: given this array of components and this sequence of events, the
 * focus is here and that control is checked. So it is checked here, where a
 * regression takes a second to find, rather than by tabbing around under an
 * emulator and trusting one's memory of where the ring was.
 *
 * The components are made up on purpose. Measuring the loop against the
 * specimen's layout would make every change of layout a change of test, and
 * would say nothing more.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>

#include <string.h>

#include "okapia_screen.h"

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s %s\n", bOK ? "ok  " : "FAIL ", pWhat);
    if (!bOK)
    {
        s_nFailures++;
    }
}

// The screen every check below runs on: something of each kind, a list of three
// rows, two radio groups, and one control nobody can reach.
//
//   0 label      (never focusable)
//   1 button "Start", the default one
//   2 button "Settings"
//   3 button "Off", disabled
//   4 checkbox
//   5 radio, group 0
//   6 radio, group 0
//   7 radio, group 1
//   8 a list of seven items, three of them visible
//   9 an editable field
//  10 a pop-up with four choices
enum { WLabel, WStart, WSettings, WOff, WCheck, WRadioA, WRadioB, WOther, WList,
       WField, WPopup, WCount };

static TTheme s_Theme;

// Seven, so that three visible rows leave four out of sight: a list that fits
// entirely proves nothing about the one the chooser will show.
static const TListItem s_Items[] =
{
    { "System 7.1.2",    0, StateNormal, {0} },
    { "Mac OS 8.1",      0, StateNormal, {0} },
    { "System 6.0.8",    0, StateNormal, {0} },
    { "Mac OS 9.1",      0, StateNormal, {0} },
    { "Backup",          0, StateNormal, {0} },
    { "Data",            0, StateDisabled, {0} },
    { "Work",            0, StateNormal, {0} }
};
static const unsigned ITEMS = sizeof s_Items / sizeof s_Items[0];
static const unsigned VISIBLE = 3;

static char s_Edit[16];

// A pop-up's choices. Four, one of them out of reach, so that both the walk and
// the refusal are exercised.
static const TListItem s_Rates[] =
{
    { "Dynamic",     0, StateNormal, {0} },
    { "60 frames/s", 0, StateNormal, {0} },
    { "30 frames/s", 0, StateDisabled, {0} },
    { "15 frames/s", 0, StateNormal, {0} }
};

static void Build (TWidget *pW)
{
    ThemeMake (16, &s_Theme);
    for (unsigned i = 0; i < WCount; i++)
    {
        WidgetClear (&pW[i]);
    }
    pW[WLabel]   .Rect = Rect (10,  10, 200, 16);

    pW[WStart]   .Type = WidgetButton;
    pW[WStart]   .Rect = Rect (10,  40, 100, 20);
    pW[WStart]   .nState = StateDefault;

    pW[WSettings].Type = WidgetButton;
    pW[WSettings].Rect = Rect (120, 40, 100, 20);

    pW[WOff]     .Type = WidgetButton;
    pW[WOff]     .Rect = Rect (230, 40, 100, 20);
    pW[WOff]     .nState = StateDisabled;

    pW[WCheck]   .Type = WidgetCheckbox;
    pW[WCheck]   .Rect = Rect (10,  70, 100, 16);

    pW[WRadioA]  .Type = WidgetRadio;
    pW[WRadioA]  .Rect = Rect (10,  90, 100, 16);
    pW[WRadioA]  .nState = StateChecked;
    pW[WRadioB]  .Type = WidgetRadio;
    pW[WRadioB]  .Rect = Rect (120, 90, 100, 16);

    pW[WOther]   .Type = WidgetRadio;
    pW[WOther]   .Rect = Rect (230, 90, 100, 16);
    pW[WOther]   .nState = StateChecked;
    pW[WOther]   .nGroup = 1;

    pW[WList]    .Type = WidgetList;
    pW[WList]    .Rect = Rect (10, 120, 320,
                               VISIBLE * s_Theme.M.nRowHeight + 2);
    pW[WList]    .pItems  = s_Items;
    pW[WList]    .nItems  = ITEMS;
    pW[WList]    .nChoice = -1;

    strcpy (s_Edit, "Okapia");
    pW[WField]   .Type      = WidgetField;
    pW[WField]   .Rect      = Rect (10, 260, 200, s_Theme.M.nFieldHeight);
    pW[WField]   .pText     = s_Edit;
    pW[WField]   .pEdit     = s_Edit;
    pW[WField]   .nEditSize = sizeof s_Edit;
    pW[WField]   .nCaret    = 6;
    pW[WField]   .nAnchor   = 6;

    pW[WPopup]   .Type    = WidgetPopup;
    pW[WPopup]   .Rect    = Rect (10, 300, 200, s_Theme.M.nButtonHeight);
    pW[WPopup]   .pItems  = s_Rates;
    pW[WPopup]   .nItems  = sizeof s_Rates / sizeof s_Rates[0];
    pW[WPopup]   .nChoice = 0;
}

static TEvent Key (unsigned nKey, unsigned nModifiers)
{
    TEvent e = { EventKeyDown, nKey, 0, nModifiers, 0, 0 };
    return e;
}

static TEvent Char (unsigned nCode)
{
    TEvent e = { EventKeyDown, OkKeyNone, nCode, 0, 0, 0 };
    return e;
}

static TEvent Mouse (TEventType Type, int nX, int nY)
{
    TEvent e = { Type, OkKeyNone, 0, 0, nX, nY };
    return e;
}

// Events are passed by reference, because taking the address of a temporary is
// not something C++ allows and writing every one of them into a named variable
// would bury the sequences these checks are made of.
static TScreenReply Send (TScreen *pScreen, const TEvent &rEvent)
{
    return ScreenEvent (pScreen, &rEvent);
}

static bool Checked (const TScreen *p, unsigned i)
{
    return (p->pWidgets[i].nState & StateChecked) != 0;
}

static bool Pressed (const TScreen *p, unsigned i)
{
    return (p->pWidgets[i].nState & StatePressed) != 0;
}

// The focus is a bit on the component as well as an index, and a check that
// only looked at the index would miss the two parting company — which is
// exactly what draws a ring around a control that no longer has it.
static bool FocusIs (const TScreen *p, int nIndex)
{
    if (p->nFocus != nIndex)
    {
        return false;
    }
    for (unsigned i = 0; i < p->nCount; i++)
    {
        const bool bBit = (p->pWidgets[i].nState & StateFocused) != 0;
        if (bBit != ((int) i == nIndex))
        {
            return false;
        }
    }
    return true;
}

static void CheckTraversal (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);

    printf ("Focus traversal\n");
    Expect (FocusIs (&S, WStart), "the focus starts on the first control, not on the label");

    Send (&S, Key (OkKeyTab, 0));
    Expect (FocusIs (&S, WSettings), "Tab moves on");

    Send (&S, Key (OkKeyTab, 0));
    Expect (FocusIs (&S, WCheck), "Tab skips the disabled button");

    Send (&S, Key (OkKeyTab, ModShift));
    Expect (FocusIs (&S, WSettings), "Shift-Tab goes back");

    // A screen that says where its focus starts is obeyed: the alert of phase
    // 16i opens on its field, not on whichever control it placed first.
    {
        TWidget V[WCount];
        TScreen T;
        Build (V);
        V[WCheck].nState |= StateFocused;
        ScreenInit (&T, &s_Theme, V, WCount);
        Expect (T.nFocus == WCheck, "a screen that declares its focus is obeyed");
        Send (&T, Key (OkKeyTab, 0));
        Expect (FocusIs (&T, WRadioA), "and Tab moves on from there");
    }

    // All the way round: the wrap is where an off-by-one hides. Counted from
    // the array rather than written down, so that adding a control to the
    // screen above does not quietly turn this into a test of something else.
    unsigned nFocusable = 0;
    for (unsigned i = 0; i < WCount; i++)
    {
        if (WidgetFocusable (&W[i]))
        {
            nFocusable++;
        }
    }
    const int nWas = S.nFocus;
    for (unsigned i = 0; i < nFocusable; i++)
    {
        Send (&S, Key (OkKeyTab, 0));
    }
    Expect (FocusIs (&S, nWas), "Tab goes all the way round and back");
}

static void CheckOperating (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);

    printf ("Operating with the keyboard\n");

    // Return operates what the keyboard holds. On a page one walks through,
    // the eye is on the control just reached: jumping to the default button at
    // the other end of the page would be a trap.
    const int nFirst = S.nFocus;
    TScreenReply r = Send (&S, Key (OkKeyReturn, 0));
    Expect (r.Result == ScreenActivated && r.nIndex == nFirst,
            "Return operates the control that has the focus");

    Send (&S, Key (OkKeyTab, 0));                 // Settings
    r = Send (&S, Key (OkKeyReturn, 0));
    Expect (r.Result == ScreenActivated && r.nIndex == WSettings,
            "and it follows the focus, it does not jump to the default button");

    r = Send (&S, Key (OkKeySpace, 0));
    Expect (r.Result == ScreenActivated && r.nIndex == WSettings,
            "Space does the same");

    // Falling back on the default button only exists where there is no focus at
    // all: an alert, the information pane. Space falls back on nothing — it is
    // the key of the box under the cursor, and a page with no focus has no box.
    {
        TWidget W2[WCount];
        TScreen S2;
        Build (W2);
        for (unsigned i = 0; i < WCount; i++)
        {
            W2[i].nState |= StateNoFocus;
        }
        W2[WStart].nState |= StateDefault;
        ScreenInit (&S2, &s_Theme, W2, WCount);
        Expect (S2.nFocus < 0, "a page where nothing takes the focus has no focus");
        const TScreenReply d = Send (&S2, Key (OkKeyReturn, 0));
        Expect (d.Result == ScreenActivated && d.nIndex == WStart,
                "and there, Return is the default button");
        Expect (Send (&S2, Key (OkKeySpace, 0)).Result == ScreenIdle,
                "while Space does nothing");
    }

    Send (&S, Key (OkKeyTab, 0));                 // the tick box
    Expect (!Checked (&S, WCheck), "the box starts unticked");
    Send (&S, Key (OkKeySpace, 0));
    Expect (Checked (&S, WCheck), "Space ticks");
    Send (&S, Key (OkKeySpace, 0));
    Expect (!Checked (&S, WCheck), "Space unticks");

    Send (&S, Key (OkKeyTab, 0));                 // radio A, already chosen
    Send (&S, Key (OkKeyTab, 0));                 // radio B
    Send (&S, Key (OkKeySpace, 0));
    Expect (Checked (&S, WRadioB) && !Checked (&S, WRadioA),
            "one radio displaces another");
    Expect (Checked (&S, WOther), "and leaves the other group alone");

    r = Send (&S, Key (OkKeyEscape, 0));
    Expect (r.Result == ScreenCancelled, "Escape cancels");
}

static void CheckList (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);

    printf ("The list, and its scrolling\n");

    Send (&S, Key (OkKeyDown, 0));
    Expect (W[WList].nChoice < 0, "an arrow outside a list does nothing");

    // Bounded, so that a loop that can no longer reach the list fails here
    // instead of hanging the build.
    for (unsigned i = 0; i < WCount && S.nFocus != WList; i++)
    {
        Send (&S, Key (OkKeyTab, 0));
    }
    Expect (FocusIs (&S, WList), "the focus lands on the list");
    Expect (WidgetListVisible (&W[WList], &s_Theme) == VISIBLE,
            "three rows visible out of seven");

    Send (&S, Key (OkKeyDown, 0));
    Expect (W[WList].nChoice == 0, "Down chooses the first row");
    Send (&S, Key (OkKeyDown, 0));
    Send (&S, Key (OkKeyDown, 0));
    Expect (W[WList].nChoice == 2 && W[WList].nTop == 0,
            "moving down within what is visible does not scroll");

    Send (&S, Key (OkKeyDown, 0));
    Expect (W[WList].nChoice == 3 && W[WList].nTop == 1,
            "passing the last visible row scrolls by one row");

    Send (&S, Key (OkKeyDown, 0));
    Send (&S, Key (OkKeyDown, 0));
    Expect (W[WList].nChoice == 6, "a disabled row is stepped over");

    TScreenReply r = Send (&S, Key (OkKeyDown, 0));
    Expect (W[WList].nChoice == 6 && r.Result == ScreenIdle,
            "the list stops at the bottom instead of wrapping");
    Expect (W[WList].nTop == ITEMS - VISIBLE, "and the scrolling stops with it");

    Send (&S, Key (OkKeyHome, 0));
    Expect (W[WList].nChoice == 0 && W[WList].nTop == 0, "Home goes back to the top");

    Send (&S, Key (OkKeyPageDown, 0));
    Expect (W[WList].nChoice == 2, "Page Down moves one page less a row");

    Send (&S, Key (OkKeyEnd, 0));
    Expect (W[WList].nChoice == 6 && W[WList].nTop == ITEMS - VISIBLE,
            "End goes to the last and brings it into view");
}

static void CheckScroller (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);

    printf ("The list's scroller\n");

    const TRect Bar = WidgetListScroller (&W[WList], &s_Theme);
    Expect (Bar.nWidth != 0, "seven items for three rows: there is a scroller");

    const TRect Thumb = ThemeScrollThumb (&s_Theme, Bar, W[WList].nTop, VISIBLE, ITEMS);
    Expect (Thumb.nHeight * ITEMS >= (Bar.nHeight - 2) * VISIBLE - ITEMS
            && Thumb.nHeight * ITEMS <= (Bar.nHeight - 2) * VISIBLE + ITEMS,
            "the thumb takes of the track what the view takes of the whole");

    const int nMid = Bar.nX + (int) Bar.nWidth / 2;

    // Below the thumb: a page down, not a jump to where the click landed.
    Send (&S, Mouse (EventMouseDown, nMid, Bar.nY + (int) Bar.nHeight - 2));
    Send (&S, Mouse (EventMouseUp,   nMid, Bar.nY + (int) Bar.nHeight - 2));
    Expect (W[WList].nTop == VISIBLE - 1, "clicking below the thumb moves a page");
    Expect (W[WList].nChoice < 0, "and chooses nothing: the view moved, not the selection");

    Send (&S, Mouse (EventMouseDown, nMid, Bar.nY + 1));
    Send (&S, Mouse (EventMouseUp,   nMid, Bar.nY + 1));
    Expect (W[WList].nTop == 0, "clicking above moves back as much");

    // Taking hold of the thumb and dragging it to the bottom.
    const TRect T0 = ThemeScrollThumb (&s_Theme, Bar, 0, VISIBLE, ITEMS);
    Send (&S, Mouse (EventMouseDown, nMid, T0.nY + (int) T0.nHeight / 2));
    Send (&S, Mouse (EventMouseMove, nMid, Bar.nY + (int) Bar.nHeight));
    Expect (W[WList].nTop == ITEMS - VISIBLE, "dragging the thumb down scrolls to the end");
    Send (&S, Mouse (EventMouseMove, nMid, Bar.nY - 20));
    Expect (W[WList].nTop == 0, "and dragging it up goes back to the top");
    Send (&S, Mouse (EventMouseUp, nMid, Bar.nY - 20));
    Expect (S.nDragList < 0, "releasing lets go of the thumb");
}

static void CheckField (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);

    printf ("The editable field\n");

    for (unsigned i = 0; i < WCount && S.nFocus != WField; i++)
    {
        Send (&S, Key (OkKeyTab, 0));
    }
    Expect (FocusIs (&S, WField), "the focus reaches the field");

    Send (&S, Char ('!'));
    Expect (strcmp (s_Edit, "Okapia!") == 0 && W[WField].nCaret == 7,
            "a keystroke is inserted at the caret");

    Send (&S, Key (OkKeyBackspace, 0));
    Expect (strcmp (s_Edit, "Okapia") == 0, "backspace erases");

    Send (&S, Key (OkKeyLeft, 0));
    Send (&S, Key (OkKeyLeft, 0));
    Send (&S, Char ('X'));
    Expect (strcmp (s_Edit, "OkapXia") == 0, "typing goes in the middle, not at the end");

    Send (&S, Key (OkKeyDelete, 0));
    Expect (strcmp (s_Edit, "OkapXa") == 0, "Delete erases ahead");

    Send (&S, Key (OkKeyHome, 0));
    Send (&S, Char (0xE9));                             // é
    Expect (strcmp (s_Edit, "\xC3\xA9OkapXa") == 0,
            "an accented character is written as two bytes");
    Send (&S, Key (OkKeyRight, 0));
    Expect (W[WField].nCaret == 3, "and the arrow steps over it in one go");
    Send (&S, Key (OkKeyLeft, 0));
    Send (&S, Key (OkKeyBackspace, 0));
    Expect (strcmp (s_Edit, "OkapXa") == 0, "and backspace erases it whole");

    // Fills the buffer and then some: refusing is right, half a character is not.
    Send (&S, Key (OkKeyEnd, 0));
    for (unsigned i = 0; i < 40; i++)
    {
        Send (&S, Char ('a'));
    }
    Expect (strlen (s_Edit) == sizeof s_Edit - 1, "a full field refuses instead of overflowing");

    // Tab still means Tab: a field takes the keys that are its own and no others.
    Send (&S, Key (OkKeyTab, 0));
    Expect (S.nFocus != WField, "Tab leaves the field instead of being typed into it");
}

// Command and a letter, and the mark they work on.
static TEvent Command (unsigned nChar)
{
    TEvent e = { EventKeyDown, OkKeyNone, nChar, ModCommand, 0, 0 };
    return e;
}

static void CheckSelection (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);

    printf ("Selection and clipboard\n");

    for (unsigned i = 0; i < WCount && S.nFocus != WField; i++)
    {
        Send (&S, Key (OkKeyTab, 0));
    }
    Expect (!WidgetFieldHasSelection (&W[WField]), "nothing is selected at first");

    Send (&S, Key (OkKeyLeft, ModShift));
    Send (&S, Key (OkKeyLeft, ModShift));
    Expect (WidgetFieldHasSelection (&W[WField]) && W[WField].nCaret == 4
            && W[WField].nAnchor == 6, "Shift-arrow extends the selection");

    Send (&S, Key (OkKeyLeft, 0));
    Expect (!WidgetFieldHasSelection (&W[WField]) && W[WField].nCaret == 4,
            "an arrow alone collapses it to the edge it points at");

    Send (&S, Command ('a'));
    Expect (W[WField].nAnchor == 0 && W[WField].nCaret == 6, "Command-A takes everything");

    Send (&S, Command ('c'));
    Send (&S, Key (OkKeyEnd, 0));
    Send (&S, Command ('v'));
    Expect (strcmp (s_Edit, "OkapiaOkapia") == 0, "Command-C then Command-V pastes");

    Send (&S, Command ('a'));
    Send (&S, Command ('x'));
    Expect (strcmp (s_Edit, "") == 0 && !WidgetFieldHasSelection (&W[WField]),
            "Command-X cuts everything");
    Send (&S, Command ('v'));
    Expect (strcmp (s_Edit, "OkapiaOkapia") == 0, "and what was cut pastes back");

    // Typing over a selection replaces it.
    Send (&S, Key (OkKeyHome, 0));
    Send (&S, Key (OkKeyRight, ModShift));
    Send (&S, Key (OkKeyRight, ModShift));
    Send (&S, Char ('X'));
    Expect (strcmp (s_Edit, "XapiaOkapia") == 0, "typing replaces what is selected");

    Send (&S, Command ('a'));
    Send (&S, Key (OkKeyBackspace, 0));
    Expect (strcmp (s_Edit, "") == 0, "and backspace erases the whole selection");

    // Dragging with the mouse selects.
    strcpy (s_Edit, "Okapia");
    W[WField].nCaret = W[WField].nAnchor = 0;
    const int nY2 = W[WField].Rect.nY + 2;
    Send (&S, Mouse (EventMouseDown, W[WField].Rect.nX + 2, nY2));
    Send (&S, Mouse (EventMouseMove, W[WField].Rect.nX + 200, nY2));
    Expect (WidgetFieldHasSelection (&W[WField]) && W[WField].nAnchor == 0
            && W[WField].nCaret == 6, "dragging in the field selects");
}

static void CheckMenu (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);
    S.Bounds = Rect (0, 0, 640, 480);

    printf ("A popup's menu\n");

    for (unsigned i = 0; i < WCount && S.nFocus != WPopup; i++)
    {
        Send (&S, Key (OkKeyTab, 0));
    }
    Expect (FocusIs (&S, WPopup), "the focus reaches the popup");
    Expect (!ScreenMenuOpen (&S), "and its menu is closed");

    // Return opens it too, which is how one enters a control just reached: the
    // menu then takes Return to choose and Escape to give up, so the key means
    // the same on the way in and on the way out. Return is the default button
    // only where the keyboard holds nothing that opens.
    Send (&S, Key (OkKeyReturn, 0));
    Expect (ScreenMenuOpen (&S), "Return opens the popup under the keyboard");
    Send (&S, Key (OkKeyEscape, 0));
    Expect (!ScreenMenuOpen (&S), "and Escape closes it");

    Send (&S, Key (OkKeySpace, 0));
    Expect (ScreenMenuOpen (&S), "Space opens it");
    Expect (S.Menu.nChoice == 0, "on the current choice");

    Send (&S, Key (OkKeyDown, 0));
    Expect (S.Menu.nChoice == 1, "Down moves down inside it");
    Send (&S, Key (OkKeyDown, 0));
    Expect (S.Menu.nChoice == 3, "and steps over a choice out of reach");
    Expect (W[WPopup].nChoice == 0, "changing nothing until confirmed");

    TScreenReply r = Send (&S, Key (OkKeyReturn, 0));
    Expect (r.Result == ScreenActivated && r.nIndex == WPopup, "Return confirms");
    Expect (W[WPopup].nChoice == 3, "and the choice is taken");
    Expect (!ScreenMenuOpen (&S), "the menu is closed");
    Expect (S.bDirtyAll, "and the whole screen is to be redrawn, since it covered some");

    // Escape puts it back as it was, which is the whole point of a menu one can
    // open to look at.
    Send (&S, Key (OkKeySpace, 0));
    Send (&S, Key (OkKeyUp, 0));
    Send (&S, Key (OkKeyEscape, 0));
    Expect (!ScreenMenuOpen (&S) && W[WPopup].nChoice == 3, "Escape closes without changing anything");

    // While it is open nothing underneath answers: that is what modal means.
    Send (&S, Key (OkKeySpace, 0));
    Send (&S, Key (OkKeyTab, 0));
    Expect (ScreenMenuOpen (&S) && S.nFocus == WPopup,
            "Tab does not go through an open menu");
    Send (&S, Mouse (EventMouseDown, 400, 60));
    Expect (!ScreenMenuOpen (&S), "a click beside it closes it");

    /*
     *  Opened by a click, it stays open
     *
     *  Otherwise the release that ends the opening click at once chooses what
     *  lies under the pointer — the menu appears and disappears, and the only
     *  way to use it is to keep the button held down.
     */
    const int nX = W[WPopup].Rect.nX + 10;
    const int nY = W[WPopup].Rect.nY + 2;
    Send (&S, Mouse (EventMouseDown, nX, nY));
    Expect (ScreenMenuOpen (&S), "clicking the popup opens the menu");
    TScreenReply r2 = Send (&S, Mouse (EventMouseUp, nX, nY));
    Expect (ScreenMenuOpen (&S) && r2.Result != ScreenActivated,
            "releasing without moving leaves it open");

    // And it closes at the next click, on an item.
    const int nItemY = S.Menu.Rect.nY + (int) s_Theme.M.nMenuRow * 3 / 2;
    Send (&S, Mouse (EventMouseMove, nX, nItemY));
    Send (&S, Mouse (EventMouseDown, nX, nItemY));
    r2 = Send (&S, Mouse (EventMouseUp, nX, nItemY));
    Expect (r2.Result == ScreenActivated && !ScreenMenuOpen (&S),
            "the next click chooses and closes");
    Expect (W[WPopup].nChoice == 1, "and it is the item aimed at");

    // Dragging through from the opening still chooses, as it always did.
    Send (&S, Mouse (EventMouseDown, nX, nY));
    const int nFirstY = S.Menu.Rect.nY + (int) s_Theme.M.nMenuRow / 2;
    Send (&S, Mouse (EventMouseMove, nX, nFirstY));
    r2 = Send (&S, Mouse (EventMouseUp, nX, nFirstY));
    Expect (r2.Result == ScreenActivated && W[WPopup].nChoice == 0,
            "dragging through and releasing still chooses");
}

static void CheckMouse (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);

    printf ("The mouse\n");

    Send (&S, Mouse (EventMouseDown, 160, 50));         // Settings
    Expect (Pressed (&S, WSettings), "pressed during the click");
    Expect (FocusIs (&S, WSettings), "the click takes the focus");

    TScreenReply r = Send (&S, Mouse (EventMouseUp, 160, 50));
    Expect (r.Result == ScreenActivated && r.nIndex == WSettings, "released inside: operated");
    Expect (!Pressed (&S, WSettings), "and no longer pressed");

    Send (&S, Mouse (EventMouseDown, 160, 50));
    Send (&S, Mouse (EventMouseMove, 160, 200));
    Expect (!Pressed (&S, WSettings), "leaving the control releases it visually");
    Send (&S, Mouse (EventMouseMove, 160, 50));
    Expect (Pressed (&S, WSettings), "coming back presses it again");
    Send (&S, Mouse (EventMouseMove, 160, 200));
    r = Send (&S, Mouse (EventMouseUp, 160, 200));
    Expect (r.Result != ScreenActivated, "released outside: nothing done");

    r = Send (&S, Mouse (EventMouseDown, 300, 50));     // the disabled button
    Expect (!Pressed (&S, WOff), "a disabled control takes nothing");
    Expect (r.Result == ScreenChanged && S.nFocus < 0,
            "and the click counts as a click on nothing: the focus leaves");

    const int nRow = (int) s_Theme.M.nRowHeight;
    Send (&S, Mouse (EventMouseDown, 100, 121 + nRow + nRow / 2));      // the third
    Send (&S, Mouse (EventMouseUp,   100, 121 + nRow + nRow / 2));
    Expect (W[WList].nChoice == 1, "clicking a row selects it");
    Expect (FocusIs (&S, WList), "and gives the focus to the list");

    // Clicking on nothing leaves the current control — otherwise a text field
    // cannot be left with the mouse, and that is the first place the hand goes.
    Send (&S, Mouse (EventMouseDown, 100, 145));
    Send (&S, Mouse (EventMouseUp,   100, 145));
    r = Send (&S, Mouse (EventMouseDown, 5, 5));        // the background
    Expect (r.Result == ScreenChanged && S.nFocus < 0, "clicking the background leaves the control");
    r = Send (&S, Mouse (EventMouseDown, 5, 5));
    Expect (r.Result == ScreenIdle, "and a second time changes nothing more");
    Send (&S, Key (OkKeyTab, 0));
    Expect (S.nFocus == WStart, "Tab starts again from the first control");
}

int main (void)
{
    CheckTraversal ();
    CheckOperating ();
    CheckList ();
    CheckScroller ();
    CheckField ();
    CheckSelection ();
    CheckMenu ();
    CheckMouse ();

    printf ("\n%u failure(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
