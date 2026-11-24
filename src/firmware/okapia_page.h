/*
 * okapia_page.h — the shape every screen of the firmware shares.
 *
 * A dialogue centred on the desktop, a title, a rule under it, a footer with a
 * rule above it, and a middle that takes what is left. The chooser decided that
 * shape; the settings, the information pane and the confirmations do not get to
 * decide it again. What each screen still owns is its middle and what its
 * buttons mean.
 *
 * This is a builder, not a component: it fills a screen's own array of TWidget
 * and answers indices into it. Nothing here is drawn — that is still the
 * theme's job — and nothing here is remembered between screens.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_page_h
#define _okapia_page_h

#include "okapia_layout.h"
#include "okapia_widgets.h"

struct TPage
{
    TWidget  *pWidgets;                 // the screen's own array
    unsigned  nMax;
    unsigned  nCount;

    TTheme    Theme;
    TRect     Dialog;
    TLayout   Layout;
    TRow      Foot;
    unsigned  nScale16;
    // An alert is a dialogue that interrupts. It says so with its caution mark
    // and its words, never with a heavier frame — so this changes which of the
    // theme's two frames is drawn and nothing else.
    bool      bAlert;

    // What a tabbed page carries besides its own middle — the strip at the top
    // and the machine's buttons at the bottom — or -1 on any other page.
    int       nTabs;
    int       nPower;
    int       nPram;
    int       nWorkspace;               // a desktop's Okapia Workspace button, or -1
    int       nStart;
    int       nNote;
    int       nBootMenu;
    int       nUpdate;                  // a desktop's: a newer Okapia, or -1
};

// A group being laid out: see PageGroupBegin().
struct TPageGroup
{
    int   nIndex;
    int   nTop;
    int   nX;
    unsigned nWidth;
};

/*
 *  One window, tabbed pages
 *
 *  The firmware's window has tabs, the control Mac OS 8's Appearance Manager
 *  brought. The strip says which page is in front. What the machine does — start, shut down, forget its PRAM — is the
 *  same on every page and sits in the same footer, so it never moves under the
 *  hand when the page turns.
 */
enum TPageTab
{
    PageTabSystem,
    PageTabSettings,
    PageTabNetwork,
    PageTabDisplay,
    PageTabInformation,
    PageTabJournal,
    PageTabCount
};

// What a press on the shared parts of a tabbed page means. Numbered far above
// any page's own answers, so a page driver can hand either back through one
// int without the two ranges ever meeting.
enum TPageAnswer
{
    PageAnswerNone      = 0,
    PageAnswerStart     = 1000,         // Start, or Save and restart
    PageAnswerShutDown,                 // asks, and offers Restart as well
    PageAnswerForgetPram,
    PageAnswerBootMenu,                 // the footer's tick box was pressed
    PageAnswerWorkspace,                // a desktop's: the welcome page, to change workspace
    PageAnswerUpdate,                   // a desktop's: a newer Okapia, its page
    PageAnswerTab       = 1100          // + the TPageTab now in front
};

// Says, before a tabbed page is laid out, whether a change made anywhere in
// the window needs Okapia to restart — Start then reads Save and restart, with
// the note beside it — and whether the keyboard should start on the strip,
// which is where it was when the page was turned from it. bCanStart greys
// Start when there is nothing to start from: the button that cannot work says
// so before it is pressed.
void PageChromeSet (bool bRestart, bool bFocusTabs, bool bCanStart);

// The footer's tick box, "always show this menu at startup": whether it is
// ticked, and whether the keyboard starts on it — which is where it was when
// it was pressed, since pressing it lays the window out again.
void PageChromeBootMenu (bool bOn, bool bFocus);
// The grey line under the box, saying how to ask for the menu: Alt on the
// board's PC keyboard, Option on a desktop Mac. 0 for the board's.
void PageChromeBootMenuHint (const char *pHint);
// Whether the footer offers the Okapia Workspace button (the logo), beside the
// parameter RAM and the power: a desktop's, which has more than one card.
// A tab left out of the strip (a desktop's Network: its one setting is on the
// Settings page). The others keep their order.
void PageChromeHideTab (TPageTab Tab, bool bHidden);
void PageChromeWorkspace (bool bOffered);
// A newer Okapia, on a desktop: its button at the right end of the tab strip,
// saying which ("Okapia 26.12.1 available…"); 0 for none. With a note, the
// note on a line of its own above the strip, right-aligned over the button
// ("Okapia 26.12.1 is available for this card.", over "Update").
void PageChromeUpdate (const char *pLabel, const char *pNote = 0);

// What the last PageChromeSet said about restarting. Saving and restarting
// needs no startup volume, so a page that greys Start for want of one asks
// this first.
bool PageChromeRestart (void);

// The window's frame with tab nTab in front and the shared footer already in
// place; the page lays its middle out in pPage->Layout as before.
void PageBeginTabbed (TPage *pPage, TSurface *pSurface, TWidget *pWidgets, unsigned nMax,
                      TPageTab Tab);

// What the component at nIndex means if it is one of the shared parts of the
// tabbed page laid out last, or PageAnswerNone. A page that is not tabbed — an
// alert, the new volume dialogue — never answers anything here.
int PageChromeAnswer (int nIndex);

// The firmware's own window, in the 640x480 design units the theme was drawn
// against. The chooser, the settings and the information pane all use it and
// none of them may choose their own: three dialogues of three sizes read as
// three programs, while one frame whose contents change reads as pages of one.
// An alert is the exception, and it earns it by interrupting.
// The scale every page takes, in sixteenths, instead of the one the surface's
// size gives; 0 to follow the surface again. A window the person resizes keeps
// its text the same size.
void PageFixScale (unsigned nScale16);

static const unsigned PAGE_WIDTH  = 608;
static const unsigned PAGE_HEIGHT = 458;

// Centres a dialogue of nWide by nHigh design units on the surface, puts the
// title in it and rules under it, and leaves the rest to LayoutTop/Bottom
// through pPage->Layout. nWide 0: the dialogue is the whole surface (a
// desktop's welcome window, whose edge is the page's frame).
void PageBegin (TPage *pPage, TSurface *pSurface, TWidget *pWidgets, unsigned nMax,
                unsigned nWide, unsigned nHigh, const char *pTitle);
void PageBeginAlert (TPage *pPage, TSurface *pSurface, TWidget *pWidgets, unsigned nMax,
                     unsigned nWide, unsigned nHigh, const char *pTitle);

TWidget *PageAdd (TPage *pPage, TWidgetType Type, const TRect &rRect,
                  const char *pText, unsigned nState);

// Claims the footer band off the bottom, with its rule and the air around it.
// Call it before laying the middle out: the middle takes what is left, which is
// the whole point of claiming both ends first.
void PageFooter (TPage *pPage);

// A button in the footer, from the right — a dialogue's assent is on the right
// and everything else is not, so they are all placed from that end. It answers
// the index of what it added, which is what a screen compares against when a
// control is operated.
int PageLast (TPage *pPage, const char *pText, unsigned nState);
int PageIconButton (TPage *pPage, TIconPainter Paint, unsigned nState);

// A remark in the footer, on the buttons' own line, taking whatever they left.
// It belongs there and not above the rule: it is a note about the assent, and a
// note about a button reads as one when it sits beside it.
int PageNote (TPage *pPage, const char *pText, unsigned nState);

// A named frame around what follows, until PageGroupEnd(): the layout is
// narrowed inside it and given back after, and the frame takes the height of
// whatever was laid out in between.
void PageGroupBegin (TPage *pPage, TPageGroup *pGroup, const char *pTitle);
void PageGroupEnd (TPage *pPage, TPageGroup *pGroup);

// Desktop, frame, and every component as it now stands.
void PagePaint (TSurface *pSurface, const TPage *pPage);

#endif
