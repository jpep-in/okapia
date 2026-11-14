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
    int       nStart;
    int       nNote;
    int       nBootMenu;
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
 *  One window, four pages
 *
 *  The firmware's window has tabs, the control Mac OS 8's Appearance Manager
 *  brought: the startup disk, the settings, the network and what Okapia found
 *  are four pages of one window, and the strip says which is in
 *  front. What the machine does — start, shut down, forget its PRAM — is the
 *  same on every page and sits in the same footer, so it never moves under the
 *  hand when the page turns.
 */
enum TPageTab
{
    PageTabSystem,
    PageTabSettings,
    PageTabNetwork,
    PageTabInformation,
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
static const unsigned PAGE_WIDTH  = 608;
static const unsigned PAGE_HEIGHT = 458;

// Centres a dialogue of nWide by nHigh design units on the surface, puts the
// title in it and rules under it, and leaves the rest to LayoutTop/Bottom
// through pPage->Layout.
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
