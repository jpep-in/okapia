/*
 * check_pages.cpp — the settings, the information pane and a confirmation,
 * measured and then driven, with no card and no screen.
 *
 * Two questions, and they are not the same one. Does everything fit — in both
 * languages, at both sizes, with nothing over the frame and no two controls
 * touching? And does operating a control mean what the screen says it means?
 * The first is where a translation comes apart; the second is where what goes
 * onto the user's card is decided.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "okapia_confirm.h"
#include "okapia_datetime.h"
#include "okapia_info.h"
#include "okapia_journal.h"
#include "okapia_network.h"
#include "okapia_newvolume.h"
#include "okapia_page.h"
#include "okapia_screen.h"
#include "okapia_settings.h"
#include "okapia_display.h"
#include "okapia_strings.h"
#include "okapia_clock.h"
#include "okapia_icons.h"

void SettingsSample (TSettings *p);
void InfoSample (TInfo *p);
void NetworkSample (TNetwork *p);
void ConfirmSample (TConfirm *p);
void ShutDownSample (TConfirm *p);
void NewVolumeSample (TNewVolume *p);

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s %s\n", bOK ? "ok  " : "FAIL ", pWhat);
    if (!bOK)
    {
        s_nFailures++;
    }
}

/*
 *  Geometry
 *
 *  The same two rules the specimen is held to, applied to a page whoever laid
 *  it out: nothing outside the frame's content area, and no two controls
 *  touching once each is given the room its state draws in.
 */
static void CheckFrame (const TTheme *pTheme, const TRect &rDialog, TWidget *pList,
                        unsigned nCount, const char *pWhat)
{
    const TRect C = ThemeContent (rDialog, pTheme);
    unsigned nOutside = 0;
    for (unsigned i = 0; i < nCount; i++)
    {
        const TWidget &W = pList[i];
        const int r = W.Type == WidgetTabs ? 0 : (int) ThemeReach (pTheme, W.nState);
        const TRect R = RectInset (W.Rect, -r, -r);
        if (   R.nX < C.nX || R.nY < C.nY
            || R.nX + (int) R.nWidth  > C.nX + (int) C.nWidth
            || R.nY + (int) R.nHeight > C.nY + (int) C.nHeight)
        {
            if (nOutside == 0)
            {
                printf ("    outside the frame: %s at (%d,%d %ux%u), content (%d,%d %ux%u)\n",
                        W.pText ? W.pText : "(no text)", R.nX, R.nY, R.nWidth, R.nHeight,
                        C.nX, C.nY, C.nWidth, C.nHeight);
            }
            nOutside++;
        }
    }
    Expect (nOutside == 0, pWhat);
}

static void CheckOverlap (const TTheme *pTheme, TWidget *pList, unsigned nCount,
                          const char *pWhat)
{
    unsigned nOverlaps = 0;
    for (unsigned i = 0; i < nCount; i++)
    {
        for (unsigned j = i + 1; j < nCount; j++)
        {
            const TWidget &A = pList[i];
            const TWidget &B = pList[j];
            // The same exemptions the specimen's own check makes: a label's
            // rectangle is a text box and not an extent, a separator is a rule
            // laid in the space between things, and an icon sits beside a
            // sentence rather than next to a control.
            if (   A.Type == WidgetLabel     || B.Type == WidgetLabel
                || A.Type == WidgetTitle     || B.Type == WidgetTitle
                || A.Type == WidgetSeparator || B.Type == WidgetSeparator
                || A.Type == WidgetGroup     || B.Type == WidgetGroup
                || A.Type == WidgetParagraph || B.Type == WidgetParagraph
                || A.Type == WidgetIcon      || B.Type == WidgetIcon)
            {
                continue;
            }
            // A strip of tabs rings the front tab's label, inside the tab.
            const bool bInA = A.Type == WidgetCheckbox || A.Type == WidgetRadio
                           || A.Type == WidgetTabs;
            const bool bInB = B.Type == WidgetCheckbox || B.Type == WidgetRadio
                           || B.Type == WidgetTabs;
            const int a = bInA ? 0 : (int) ThemeReach (pTheme, A.nState);
            const int b = bInB ? 0 : (int) ThemeReach (pTheme, B.nState);
            const TRect ra = RectInset (A.Rect, -a, -a);
            const TRect rb = RectInset (B.Rect, -b, -b);
            if (   ra.nX < rb.nX + (int) rb.nWidth  && rb.nX < ra.nX + (int) ra.nWidth
                && ra.nY < rb.nY + (int) rb.nHeight && rb.nY < ra.nY + (int) ra.nHeight)
            {
                if (nOverlaps == 0)
                {
                    printf ("    overlap: %s / %s\n",
                            A.pText ? A.pText : "(no text)",
                            B.pText ? B.pText : "(no text)");
                }
                nOverlaps++;
            }
        }
    }
    Expect (nOverlaps == 0, pWhat);
}

static unsigned *s_pPixels;
static TSurface  s_Surface;

static void Geometry (unsigned nScale)
{
    const unsigned w = 640 * nScale, h = 480 * nScale;
    s_Surface.pPixels = (unsigned char *) s_pPixels;
    s_Surface.nWidth  = w;
    s_Surface.nHeight = h;
    s_Surface.nPitch  = w * (unsigned) sizeof (unsigned);

    TTheme T;
    ThemeMake (ThemeScaleFor (w, h), &T);
    TWidget *pW = 0;
    unsigned n;

    TSettings Settings;
    SettingsSample (&Settings);
    SettingsDraw (&s_Surface, &Settings);
    n = SettingsWidgets (&pW);
    CheckOverlap (&T, pW, n, "settings: nothing overlaps");
    CheckFrame (&T, SettingsDialog (), pW, n, "settings: nothing overflows");
    Expect (!SettingsOverflowed (), "settings: the page fits under its tabs");
    {
        unsigned nFlat = 0, nGroups = 0;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Rect.nHeight == 0) nFlat++;
            if (pW[i].Type == WidgetGroup) nGroups++;
        }
        Expect (nFlat == 0, "settings: every row has its room");
        Expect (nGroups == 2, "settings: two groups, the shared folder and the date");
        // Everything after a group's frame and inside its band is inside the
        // frame, by a gap on each side.
        unsigned nOutside = 0;
        for (unsigned g = 0; g < n; g++)
        {
            if (pW[g].Type != WidgetGroup) continue;
            const TRect &G = pW[g].Rect;
            for (unsigned i = g + 1; i < n; i++)
            {
                const TRect &R = pW[i].Rect;
                if (pW[i].Type == WidgetGroup || R.nY >= G.nY + (int) G.nHeight) break;
                if (R.nX < G.nX || R.nX + (int) R.nWidth > G.nX + (int) G.nWidth
                    || R.nY + (int) R.nHeight > G.nY + (int) G.nHeight)
                {
                    nOutside++;
                }
            }
        }
        Expect (nOutside == 0, "settings: what a group holds stays inside its frame");
    }
    // The footer with Save and restart, its note and the tick box together:
    // the widest it gets.
    PageChromeSet (true, false, true);
    SettingsDraw (&s_Surface, &Settings);
    n = SettingsWidgets (&pW);
    CheckOverlap (&T, pW, n, "settings, restart owed: nothing overlaps");
    CheckFrame (&T, SettingsDialog (), pW, n, "settings, restart owed: nothing overflows");
    {
        int nNote = -1;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type == WidgetLabel && pW[i].pText == Str (StrRestartRequired)) nNote = (int) i;
        }
        if (nNote >= 0 && GfxTextWidth (T.pBodyFont, pW[nNote].pText) > pW[nNote].Rect.nWidth)
        {
            printf ("    note: %u wanted, %u given\n", GfxTextWidth (T.pBodyFont, pW[nNote].pText),
                    pW[nNote].Rect.nWidth);
        }
        Expect (nNote >= 0 && GfxTextWidth (T.pBodyFont, pW[nNote].pText) <= pW[nNote].Rect.nWidth,
                "settings, restart owed: the note is read whole beside the tick box");
        // The way back to the menu, said under the tick box while it is
        // unticked: whole, and clear of the restart note's words (the note's
        // rectangle runs to the box, so it is its text that is measured).
        int nHint = -1;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type == WidgetLabel && pW[i].pText == Str (StrBootMenuHint)) nHint = (int) i;
        }
        Expect (nHint >= 0 && GfxTextWidth (T.pBodyFont, pW[nHint].pText) <= pW[nHint].Rect.nWidth,
                "the hint under the tick box is read whole");
        if (nHint >= 0 && nNote >= 0)
        {
            const TRect &H = pW[nHint].Rect;
            const TRect &N = pW[nNote].Rect;
            const int nNoteEnd = N.nX + (int) GfxTextWidth (T.pBodyFont, pW[nNote].pText);
            const bool bApart = nNoteEnd <= H.nX || N.nY + (int) N.nHeight <= H.nY
                                || H.nY + (int) H.nHeight <= N.nY;
            if (!bApart)
                printf ("    hint at %d, note ends at %d\n", H.nX, nNoteEnd);
            Expect (bApart, "the hint clears the restart note's words");
        }
    }
    PageChromeSet (false, false, true);

    // The network page, with a street's worth of networks and the longest
    // name 802.11 allows, and again with Save and restart in the footer.
    // The second time with the keyboard on the strip, as after a page is
    // turned from it.
    for (int nRestart = 0; nRestart < 2; nRestart++)
    {
        TNetwork Net;
        NetworkSample (&Net);
        PageChromeSet (nRestart != 0, nRestart != 0, true);
        NetworkDraw (&s_Surface, &Net);
        n = NetworkWidgets (&pW);
        CheckOverlap (&T, pW, n, nRestart ? "network, restart owed: nothing overlaps"
                                          : "network: nothing overlaps");
        CheckFrame (&T, NetworkDialog (), pW, n, nRestart ? "network, restart owed: nothing overflows"
                                                          : "network: nothing overflows");
        Expect (!NetworkOverflowed (), "network: the page fits under its tabs");
        unsigned nFlat = 0;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Rect.nHeight == 0) nFlat++;
        }
        Expect (nFlat == 0, "network: every row has its room");
        int nList = -1;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type == WidgetList) nList = (int) i;
        }
        Expect (nList >= 0 && WidgetListVisible (&pW[nList], &T) >= 3,
                "network: the list shows at least three networks");
    }
    PageChromeSet (false, false, true);

    TInfo Info;
    InfoSample (&Info);
    InfoDraw (&s_Surface, &Info);
    n = InfoWidgets (&pW);
    CheckOverlap (&T, pW, n, "information: nothing overlaps");
    CheckFrame (&T, InfoDialog (), pW, n, "information: nothing overflows");
    Expect (!InfoOverflowed (), "information: the page fits under its tabs");
    // A page that asks for more bands than it has answers the remainder, so its
    // last rows collapse instead of spilling — which no margin check can see.
    unsigned nFlat = 0;
    for (unsigned i = 0; i < n; i++)
    {
        if (pW[i].Rect.nHeight == 0) nFlat++;
    }
    Expect (nFlat == 0, "information: every row has its room");
    // Labels are exempt from the overlap check above, because a label's
    // rectangle is a text box — which is exactly why the logo needs a check of
    // its own: a value written under it would pass every other one.
    unsigned nLogos = 0, nUnder = 0;
    for (unsigned i = 0; i < n; i++)
    {
        if (pW[i].Type != WidgetIcon) continue;
        nLogos++;
        const TRect &L = pW[i].Rect;
        for (unsigned j = 0; j < n; j++)
        {
            const TRect &R = pW[j].Rect;
            if (   j != i && pW[j].Type != WidgetSeparator
                && L.nX < R.nX + (int) R.nWidth  && R.nX < L.nX + (int) L.nWidth
                && L.nY < R.nY + (int) R.nHeight && R.nY < L.nY + (int) L.nHeight)
            {
                nUnder++;
            }
        }
    }
    Expect (nLogos == 1 && nUnder == 0, "information: the logo touches no text");

    // The Journal, at its fullest: a log longer than the list holds.
    {
        static TJournal Journal;
        JournalClear (&Journal);
        for (unsigned i = 0; i < 40; i++)
        {
            char Line[96];
            snprintf (Line, sizeof Line, "Sep 15 02:36:%02u.%02u okapia-video: line %u of the sample\n",
                      i % 60, i, i);
            JournalAppend (&Journal, Line, (unsigned) strlen (Line));
        }
        {
            JournalDraw (&s_Surface, &Journal);
            n = JournalWidgets (&pW);
            CheckOverlap (&T, pW, n, "journal: nothing overlaps");
            CheckFrame (&T, JournalDialog (), pW, n, "journal: nothing overflows");
            Expect (!JournalOverflowed (), "journal: the page fits under its tabs");
            unsigned nFlat = 0;
            int nList = -1;
            for (unsigned i = 0; i < n; i++)
            {
                if (pW[i].Rect.nHeight == 0) nFlat++;
                if (pW[i].Type == WidgetList) nList = (int) i;
            }
            Expect (nFlat == 0, "journal: every row has its room");
            Expect (nList >= 0 && WidgetListVisible (&pW[nList], &T) >= 3,
                    "journal: the list shows at least three lines");
        }
    }

    TConfirm Confirm;
    ConfirmSample (&Confirm);
    ConfirmDraw (&s_Surface, &Confirm);
    n = ConfirmWidgets (&pW);
    CheckOverlap (&T, pW, n, "confirmation: nothing overlaps");
    CheckFrame (&T, ConfirmDialog (), pW, n, "confirmation: nothing overflows");

    // The date's (i): the longest sentence any alert says, with one answer.
    {
        TConfirm Help;
        memset (&Help, 0, sizeof Help);
        Help.Level  = ConfirmNote;
        Help.pTitle = Str (StrClockGroup);
        Help.pBody  = Str (StrClockHelp);
        Help.pYes   = Str (StrOk);
        ConfirmDraw (&s_Surface, &Help);
        n = ConfirmWidgets (&pW);
        CheckOverlap (&T, pW, n, "the clock's note: nothing overlaps");
        CheckFrame (&T, ConfirmDialog (), pW, n, "the clock's note: nothing overflows");
        unsigned nShort = 0;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type == WidgetParagraph
                && GfxTextWrapHeight (T.pBodyFont, pW[i].Rect.nWidth, pW[i].pText, T.M.nLineHeight)
                   > pW[i].Rect.nHeight)
            {
                nShort++;
            }
        }
        Expect (nShort == 0, "the clock's note: every line of it is read");
        Expect (GfxTextWrapHeight (T.pBodyFont, 1000, "a\n\nb", 10) == 30,
                "a line break ends a line, and two leave a blank one");
    }

    // Shut down, with Restart as a third answer at the left: three buttons on
    // the footer's one line, in both languages.
    ShutDownSample (&Confirm);
    ConfirmDraw (&s_Surface, &Confirm);
    n = ConfirmWidgets (&pW);
    CheckOverlap (&T, pW, n, "shut down: nothing overlaps");
    CheckFrame (&T, ConfirmDialog (), pW, n, "shut down: nothing overflows");
    {
        int nRestart = -1, nShut = -1, nCancel = -1;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type != WidgetButton || pW[i].pText == 0) continue;
            if (strcmp (pW[i].pText, Str (StrRestart)) == 0)  nRestart = (int) i;
            if (strcmp (pW[i].pText, Str (StrShutDown)) == 0) nShut = (int) i;
            if (strcmp (pW[i].pText, Str (StrCancel)) == 0)   nCancel = (int) i;
        }
        Expect (nRestart >= 0 && ConfirmOperate (nRestart) == ConfirmOther,
                "shut down: Restart is the third answer");
        Expect (nShut >= 0 && ConfirmOperate (nShut) == ConfirmYes
                && (pW[nShut].nState & StateDefault) != 0,
                "shut down: Shut down stays the default");
        Expect (nRestart >= 0 && nCancel >= 0 && nShut >= 0
                && pW[nCancel].Rect.nX < pW[nRestart].Rect.nX
                && pW[nRestart].Rect.nX < pW[nShut].Rect.nX,
                "shut down: Cancel, Restart, Shut down, in that order");
    }

    TNewVolume New;
    NewVolumeSample (&New);
    NewVolumeDraw (&s_Surface, &New);
    n = NewVolumeWidgets (&pW);
    CheckOverlap (&T, pW, n, "new volume: nothing overlaps");
    CheckFrame (&T, NewVolumeDialog (), pW, n, "new volume: nothing overflows");
    Expect (!NewVolumeOverflowed (), "new volume: the sheet fits");
}

/*
 *  Behaviour
 */
static TTheme    s_Theme;
static TScreen   s_Screen;
static TSettings s_Settings;

static void Open (void)
{
    SettingsSample (&s_Settings);
    SettingsDraw (&s_Surface, &s_Settings);
    TWidget *pW = 0;
    const unsigned n = SettingsWidgets (&pW);
    ScreenInit (&s_Screen, &s_Theme, pW, n);
}

static int Find (TWidgetType Type, const char *pText)
{
    TWidget *pW = 0;
    const unsigned n = SettingsWidgets (&pW);
    for (unsigned i = 0; i < n; i++)
    {
        if (pW[i].Type == Type && pW[i].pText != 0 && strcmp (pW[i].pText, pText) == 0)
        {
            return (int) i;
        }
    }
    return -1;
}

// The nth pop-up on the page, in layout order — which is the order they are
// read in, and the only stable way to name one: their text is their value.
// The nth popup down the settings page, in the order it lays them out:
// 0 memory, 1 screen refresh, 2 mouse, 3 sound, 4 language. Positional, so
// inserting a row shifts everything below it — which is exactly what happened
// when the mouse row arrived, and the four failures all said "sound" while
// meaning "the row above sound".
static int Popup (unsigned nWhich)
{
    TWidget *pW = 0;
    const unsigned n = SettingsWidgets (&pW);
    unsigned nSeen = 0;
    for (unsigned i = 0; i < n; i++)
    {
        if (pW[i].Type == WidgetPopup && nSeen++ == nWhich)
        {
            return (int) i;
        }
    }
    return -1;
}

static void Choose (int nPopup, int nItem)
{
    TWidget *pW = 0;
    SettingsWidgets (&pW);
    pW[nPopup].nChoice = nItem;
}

int main (void)
{
    s_pPixels = (unsigned *) calloc (1280 * 960, sizeof (unsigned));
    if (s_pPixels == 0)
    {
        return 1;
    }
    ThemeMake (16, &s_Theme);

    // Every page, at both sizes, in every language. That last one is the whole
    // reason the translations came before the screens.
    for (unsigned nLang = 0; nLang < LanguageCount; nLang++)
    {
        StringsSetLanguage ((TLanguage) nLang);
        for (unsigned nScale = 1; nScale <= 2; nScale++)
        {
            printf ("\n[%s] %ux\n", StringsCode (StringsLanguage ()), nScale);
            Geometry (nScale);
        }
    }
    StringsSetLanguage ((TLanguage) 0);

    printf ("\nsettings\n");
    s_Surface.nWidth  = 640;
    s_Surface.nHeight = 480;
    s_Surface.nPitch  = 640 * (unsigned) sizeof (unsigned);
    Open ();

    // Nothing has been touched, so nothing asks for a restart and the button
    // says the smaller of the two things it can say.
    Expect (!SettingsNeedsRestart (&s_Settings), "on opening, no restart required");

    // The tick box that opens the menu at every startup is the window's, in the
    // footer beside Start, on every page; pressing it is the window's to answer.
    {
        TWidget *pC = 0;
        const unsigned nAll = SettingsWidgets (&pC);
        int nBox = -1, nStart = -1;
        for (unsigned i = 0; i < nAll; i++)
        {
            if (pC[i].Type == WidgetCheckbox && pC[i].pText != 0
                && strcmp (pC[i].pText, Str (StrBootMenu)) == 0)
            {
                nBox = (int) i;
            }
            if (pC[i].Type == WidgetButton && pC[i].pText == Str (StrStart)) nStart = (int) i;
        }
        Expect (nBox >= 0 && PageChromeAnswer (nBox) == PageAnswerBootMenu,
                "the boot menu tick box is the footer's, and answers as such");
        Expect (nStart >= 0 && pC[nBox].Rect.nX + (int) pC[nBox].Rect.nWidth <= pC[nStart].Rect.nX,
                "to the left of Start");
        // Unticked, the way back to the menu is said under the box, in grey,
        // inside the footer: it ends where the box ends and touches no button.
        for (unsigned nRestart = 0; nRestart < 1; nRestart++)
        {
            PageChromeSet (nRestart != 0, false, true);
            PageChromeBootMenu (false, false);
            SettingsDraw (&s_Surface, &s_Settings);
            const unsigned nNow = SettingsWidgets (&pC);
            int nHint = -1, nCheck = -1;
            for (unsigned i = 0; i < nNow; i++)
            {
                if (pC[i].Type == WidgetLabel && pC[i].pText == Str (StrBootMenuHint)) nHint = (int) i;
                if (pC[i].Type == WidgetCheckbox && pC[i].pText == Str (StrBootMenu)) nCheck = (int) i;
            }
            Expect (nHint >= 0 && nCheck >= 0 && (pC[nHint].nState & StateDisabled) != 0,
                    "unticked, a grey line says how to get the menu back");
            if (nHint < 0 || nCheck < 0) continue;
            const TRect &H = pC[nHint].Rect;
            const TRect &B = pC[nCheck].Rect;
            Expect (H.nY >= B.nY + (int) B.nHeight && H.nX + (int) H.nWidth == B.nX + (int) B.nWidth
                    && H.nWidth > 0,
                    "it sits under the box, ends where the box ends, and has the room it needs");
            unsigned nTouch = 0;
            for (unsigned i = 0; i < nNow; i++)
            {
                if ((int) i == nHint) continue;
                const bool bRestartNote = pC[i].Type == WidgetLabel
                                          && pC[i].pText == Str (StrRestartRequired);
                if (pC[i].Type != WidgetButton && pC[i].Type != WidgetIconButton
                    && pC[i].Type != WidgetCheckbox && !bRestartNote)
                    continue;
                const TRect &R = pC[i].Rect;
                if (H.nX < R.nX + (int) R.nWidth && R.nX < H.nX + (int) H.nWidth
                    && H.nY < R.nY + (int) R.nHeight && R.nY < H.nY + (int) H.nHeight)
                {
                    printf ("    hint (%d,%d %ux%u) touches %s (%d,%d %ux%u)\n", H.nX, H.nY, H.nWidth,
                            H.nHeight, pC[i].pText ? pC[i].pText : "(button)", R.nX, R.nY, R.nWidth,
                            R.nHeight);
                    nTouch++;
                }
            }
            Expect (nTouch == 0, nRestart ? "and clears the restart note"
                                          : "and clears the buttons");
        }
        PageChromeSet (false, false, true);
        PageChromeBootMenu (true, false);
        SettingsDraw (&s_Surface, &s_Settings);
        SettingsWidgets (&pC);
        Expect (nBox >= 0 && pC[nBox].Rect.nY >= pC[nStart].Rect.nY
                && pC[nBox].Rect.nY + (int) pC[nBox].Rect.nHeight
                   <= pC[nStart].Rect.nY + (int) pC[nStart].Rect.nHeight,
                "ticked, it is back on Start's line and the grey line is gone");
        Expect (Find (WidgetLabel, Str (StrBootMenuHint)) < 0, "ticked, nothing is said");
        PageChromeBootMenu (true, true);
        SettingsDraw (&s_Surface, &s_Settings);
        SettingsWidgets (&pC);
        Expect ((pC[nBox].nState & (StateChecked | StateFocused)) == (StateChecked | StateFocused)
                && (pC[nStart].nState & StateFocused) == 0,
                "ticked, it keeps the keyboard and Start gives it up");
        PageChromeBootMenu (false, false);
        SettingsDraw (&s_Surface, &s_Settings);
    }
    Expect (Find (WidgetButton, Str (StrStart)) >= 0, "the window's button says Start");

    // The memory is the one setting the board cannot take at a Macintosh's
    // start, because its block is claimed before any of this runs.
    Choose (Popup (0), 0);
    // The assent is about to change its words, so it is about to change its
    // width: the page says so rather than draw one label in another's button.
    Expect (SettingsOperate (&s_Settings, Popup (0)) == SettingsRelayout,
            "changing the memory asks for a new layout");
    Expect (s_Settings.V.nMemoryMB == 64, "and the value is kept");
    Expect (SettingsNeedsRestart (&s_Settings), "the memory asks for a restart");
    // The firmware tells the frame what the window owes before laying out.
    PageChromeSet (SettingsNeedsRestart (&s_Settings), false, true);
    SettingsDraw (&s_Surface, &s_Settings);
    Expect (Find (WidgetButton, Str (StrSaveRestart)) >= 0,
            "the button becomes Save and Restart");
    Expect (PageChromeAnswer (Find (WidgetButton, Str (StrSaveRestart))) == PageAnswerStart,
            "and it is still the window's Start");

    // Back to what it was: the question is what changed since the screen
    // opened, not what was ever touched.
    Choose (Popup (0), 2);
    SettingsOperate (&s_Settings, Popup (0));
    Expect (!SettingsNeedsRestart (&s_Settings), "back to its value, nothing left to restart");
    PageChromeSet (SettingsNeedsRestart (&s_Settings), false, true);
    SettingsDraw (&s_Surface, &s_Settings);
    Expect (Find (WidgetButton, Str (StrStart)) >= 0, "and the button takes its word back");

    // Off is first, so a value that cannot be read leaves the machine silent —
    // a device claimed and not working is what froze the guest once already.
    Expect (SoundOff == 0, "sound off is the first value");
    Choose (Popup (2), SoundHDMI);
    SettingsOperate (&s_Settings, Popup (2));
    Expect (s_Settings.V.nSound == SoundHDMI, "the HDMI output can be chosen");

    // A board without USB audio shows the entry and greys it. An option that
    // vanishes between two machines reads as a version difference; a grey one
    // says what is true — and the loop refuses to choose it, which is what
    // makes the grey more than a colour.
    {
        TWidget *pW = 0;
        SettingsWidgets (&pW);
        const TWidget &Menu = pW[Popup (2)];
        Expect ((Menu.pItems[SoundUSB].nState & StateDisabled) != 0,
                "without USB, the entry is there and disabled");
        Expect ((Menu.pItems[SoundJack].nState & StateDisabled) == 0,
                "and the headphone jack is not");
    }

    // The 68k engine's translator, read at every start of the Macintosh.
    {
        TWidget *pW = 0;
        SettingsWidgets (&pW);

        // An image without the translator shows the box grey, still saying what
        // the file holds, and the loop cannot change it.
        const int nJit = Find (WidgetCheckbox, Str (StrJit));
        Expect (nJit >= 0, "the translator has its tick box");
        Expect ((pW[nJit].nState & (StateDisabled | StateChecked)) == (StateDisabled | StateChecked)
                && !WidgetFocusable (&pW[nJit]),
                "without a translator it is grey, ticked as the file says, Tab included");
        SettingsOperate (&s_Settings, nJit);
        Expect (s_Settings.V.bJit, "and pressing it changes nothing");
        s_Settings.bJitAvailable = true;
        SettingsOperate (&s_Settings, nJit);
        Expect (!s_Settings.V.bJit && (pW[nJit].nState & (StateDisabled | StateChecked)) == 0,
                "with one, it can be turned off");
        Expect (!SettingsNeedsRestart (&s_Settings), "and that owes no restart either");
        s_Settings.bJitAvailable = false;
        s_Settings.V.bJit = true;
        SettingsSync (&s_Settings);
    }

    // The shared folder's name is a name only while there is a volume to carry
    // it. The field says so by going grey, not by refusing letters.
    const int nShared = Find (WidgetCheckbox, Str (StrShare));
    Expect (nShared >= 0, "the shared folder has its tick box");
    SettingsOperate (&s_Settings, nShared);
    Expect (!s_Settings.V.bShared, "it can be turned off");
    {
        TWidget *pW = 0;
        const unsigned n = SettingsWidgets (&pW);
        unsigned nFields = 0, nGrey = 0;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type != WidgetField
                || (pW[i].pEdit != s_Settings.V.SharedPath && pW[i].pEdit != s_Settings.V.SharedName))
            {
                continue;
            }
            nFields++;
            if ((pW[i].nState & StateDisabled) != 0 && !WidgetFocusable (&pW[i]))
            {
                nGrey++;
            }
        }
        Expect (nFields == 2, "sharing has two fields: where it is and what it is called");
        Expect (nGrey == 2, "and both become disabled, Tab included");
    }

    // The language applies at once and the page has to be laid out again: its
    // labels are not the same width twice.
    {
        const TLanguage Was = StringsLanguage ();
        const unsigned nOther = (Was + 1) % LanguageCount;
        Choose (Popup (3), (int) nOther);
        Expect (SettingsOperate (&s_Settings, Popup (3)) == SettingsRelayout,
                "changing the language asks for a new layout");
        Expect (StringsLanguage () == (TLanguage) nOther, "and the language has already changed");
        StringsSetLanguage (Was);
    }

    printf ("\ndate and time\n");
    {
        TSettings Set;
        SettingsSample (&Set);
        TDateTime &DT = Set.Clock;
        SettingsDraw (&s_Surface, &Set);
        TWidget *pW = 0;
        unsigned n = SettingsWidgets (&pW);
        int nZone = -1, nNet = -1, nServer = -1, nNow = -1;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type == WidgetPopup && pW[i].pItems != 0 && pW[i].nItems >= ClockZoneCount ())
                nZone = (int) i;
            if (pW[i].Type == WidgetCheckbox && pW[i].pText == Str (StrNetTime)) nNet = (int) i;
            if (pW[i].Type == WidgetField && pW[i].pEdit == DT.V.Server) nServer = (int) i;
            if (pW[i].Type == WidgetLabel && (pW[i].nState & StateStrong) != 0) nNow = (int) i;
        }
        const char *pWanted = StringsLanguage () == LangFR ? "mercredi 30 septembre 2026, 23:05"
                                                           : "Wednesday 30 September 2026, 23:05";
        Expect (nNow >= 0 && strcmp (pW[nNow].pText, pWanted) == 0,
                "the date and time, said in words, in bold");
        Expect (nNow >= 0 && GfxTextWidth (s_Theme.pBodyBoldFont, pWanted) <= pW[nNow].Rect.nWidth,
                "and whole, the source after it");
        Expect (nZone >= 0 && pW[nZone].nItems == ClockZoneCount (),
                "the time zone lists every zone, and no fixed offset the card did not carry");
        Expect (pW[nZone].nChoice == ClockZoneFind ("paris"), "and shows the one chosen");
        pW[nZone].nChoice = ClockZoneFind ("newyork");
        Expect (SettingsOperate (&Set, nZone) == SettingsClock
                && DT.V.nZone == (unsigned) ClockZoneFind ("newyork"),
                "choosing another zone keeps it and says the clock may move");
        Expect (!SettingsNeedsRestart (&Set), "and asks for no restart");
        Expect (SettingsOperate (&Set, nZone) != SettingsClock, "choosing it again changes nothing");

        Expect (nServer >= 0 && WidgetFocusable (&pW[nServer]), "the time server can be typed");
        Expect (SettingsOperate (&Set, nNet) == SettingsClock && !DT.V.bNetTime
                && !WidgetFocusable (&pW[nServer]), "unticked, the server goes grey");
        Expect (strcmp (DT.V.Server, "pool.ntp.org") == 0, "and keeps its name for later");

        int nHelp = -1;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type == WidgetIconButton && pW[i].Paint == OkapiaPaintInfo) nHelp = (int) i;
        }
        Expect (nHelp >= 0 && nNow >= 0 && pW[nHelp].Rect.nY <= pW[nNow].Rect.nY
                && pW[nHelp].Rect.nY + (int) pW[nHelp].Rect.nHeight
                   >= pW[nNow].Rect.nY + (int) pW[nNow].Rect.nHeight
                && pW[nHelp].Rect.nX > pW[nNow].Rect.nX,
                "an (i) at the end of the date's line");
        Expect (nHelp >= 0 && SettingsOperate (&Set, nHelp) == SettingsClockHelp,
                "which asks for the note on how the clock is kept");

        // An older card's fixed offset is offered as itself.
        TSettings Older;
        SettingsSample (&Older);
        Older.Clock.V.nZone = ClockZoneCount ();
        Older.Clock.V.nFixedMinutes = 120;
        Older.Clock.Opened = Older.Clock.V;
        SettingsDraw (&s_Surface, &Older);
        n = SettingsWidgets (&pW);
        nZone = -1;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type == WidgetPopup && pW[i].nItems > ClockZoneCount ()) nZone = (int) i;
        }
        Expect (nZone >= 0 && pW[nZone].nChoice == (int) ClockZoneCount ()
                && strstr (pW[nZone].pItems[ClockZoneCount ()].pText, "UTC+2") != 0,
                "an older card's fixed offset is offered as itself, not as a city");
        SettingsDraw (&s_Surface, &s_Settings);
    }

    printf ("\nconfirmation\n");
    {
        TConfirm C;
        ConfirmSample (&C);
        ConfirmDraw (&s_Surface, &C);
        TWidget *pW = 0;
        const unsigned n = ConfirmWidgets (&pW);
        ScreenInit (&s_Screen, &s_Theme, pW, n);

        int nYes = -1, nNo = -1;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type != WidgetButton) continue;
            if (pW[i].pText == C.pYes) nYes = (int) i;
            if (pW[i].pText == C.pNo)  nNo  = (int) i;
        }
        Expect (nYes >= 0 && nNo >= 0, "both answers are there");
        Expect (nYes >= 0 && (pW[nYes].nState & StateDefault) != 0,
                "the assent wears the default ring");
        Expect (ConfirmOperate (nYes) == ConfirmYes, "yes answers yes");
        Expect (ConfirmOperate (nNo)  == ConfirmNo,  "no answers no");

        // Return is the default button wherever the focus is — that is what the
        // ring around it promises, and a confirmation is where it matters.
        const TEvent Ret = { EventKeyDown, OkKeyReturn, 0, 0, 0, 0 };
        const TScreenReply R = ScreenEvent (&s_Screen, &Ret);
        Expect (R.Result == ScreenActivated && ConfirmOperate (R.nIndex) == ConfirmYes,
                "Return is the assent");

        // And Escape is the refusal, the other half of the bargain: two
        // answers, two keys, and so nothing to walk through with the keyboard.
        const TEvent Esc = { EventKeyDown, OkKeyEscape, 0, 0, 0, 0 };
        Expect (ScreenEvent (&s_Screen, &Esc).Result == ScreenCancelled,
                "Escape is the refusal");

        // Neither button takes the focus, and the screen puts it nowhere: the
        // default button's ring said "Return does this", and the focus ring
        // "the keyboard is here" — two rings one inside the other saying the
        // same thing.
        Expect (!WidgetFocusable (&pW[nYes]) && !WidgetFocusable (&pW[nNo]),
                "neither answer takes the focus");
        Expect (s_Screen.nFocus < 0, "and the alert has no focus at all");
        unsigned nRings = 0;
        for (unsigned i = 0; i < n; i++)
        {
            if ((pW[i].nState & StateFocused) != 0) nRings++;
        }
        Expect (nRings == 0, "so nothing wears two rings");

        // Tab does nothing either, for lack of anything to give it.
        const TEvent Tab = { EventKeyDown, OkKeyTab, 0, 0, 0, 0 };
        ScreenEvent (&s_Screen, &Tab);
        Expect (s_Screen.nFocus < 0, "and Tab does not invent one");

        // The choices: which Macintosh to forget. Radio buttons that are clicked
        // and never take the keyboard — otherwise Return would mark one instead
        // of assenting.
        int nFirst = -1;
        unsigned nRadios = 0, nMarked = 0;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type != WidgetRadio) continue;
            if (nFirst < 0) nFirst = (int) i;
            nRadios++;
            if (pW[i].nState & StateChecked) nMarked++;
            Expect (!WidgetFocusable (&pW[i]), "a choice does not take the focus");
        }
        Expect (nRadios == C.nChoices, "one radio button per choice");
        Expect (nMarked == 1 && ConfirmChoice () == C.nChoice,
                "only one marked, the one asked for");

        // A click on "Both" marks it, unmarks the other, and the alert stays.
        const TWidget &Both = pW[nFirst + 2];
        const TEvent Down = { EventMouseDown, 0, 0, 0,
                              Both.Rect.nX + 4, Both.Rect.nY + (int) Both.Rect.nHeight / 2 };
        TEvent Up = Down;
        Up.Type = EventMouseUp;
        ScreenEvent (&s_Screen, &Down);
        const TScreenReply Click = ScreenEvent (&s_Screen, &Up);
        Expect (Click.Result == ScreenActivated && ConfirmOperate (Click.nIndex) == ConfirmWaiting,
                "clicking a choice does not answer the question");
        Expect (ConfirmChoice () == 2, "and it is the one marked now");
        Expect (s_Screen.nFocus < 0, "without the focus moving");
        const TScreenReply R2 = ScreenEvent (&s_Screen, &Ret);
        Expect (R2.Result == ScreenActivated && ConfirmOperate (R2.nIndex) == ConfirmYes,
                "Return is still the assent after a choice");

        // With no choices, the alert shows none.
        TConfirm Plain = C;
        Plain.nChoices = 0;
        ConfirmDraw (&s_Surface, &Plain);
        TWidget *pP = 0;
        const unsigned nP = ConfirmWidgets (&pP);
        unsigned nPlainRadios = 0;
        for (unsigned i = 0; i < nP; i++)
        {
            if (pP[i].Type == WidgetRadio) nPlainRadios++;
        }
        Expect (nPlainRadios == 0 && ConfirmChoice () == 0, "and a plain alert has no choices");
    }

    printf ("\nnetwork\n");
    {
        TNetwork Net;
        NetworkSample (&Net);
        Expect (Net.nSeen == 6, "a hidden network is dropped, a network heard twice is one line");
        Expect (strcmp (Net.Seen[0].SSID, "Maison-5G") == 0 && Net.Seen[0].nDbm == -48,
                "strongest first, each name at its strongest");
        bool bSorted = true;
        for (unsigned i = 1; i < Net.nSeen; i++)
        {
            bSorted = bSorted && Net.Seen[i - 1].nDbm >= Net.Seen[i].nDbm;
        }
        Expect (bSorted, "and in order of signal all the way down");
        Expect (NetworkBars (-48) == 4 && NetworkBars (-65) == 3 && NetworkBars (-70) == 2
                && NetworkBars (-90) == 1, "four bars, three, two, one");

        Expect (NetworkPasswordUsable (""), "no password is an open network");
        Expect (!NetworkPasswordUsable ("short"), "seven characters are too few");
        Expect (NetworkPasswordUsable ("correct horse"), "eight to 63 printable characters");
        Expect (!NetworkPasswordUsable ("mot de passe é"), "an accent cannot be hashed the same everywhere");
        char Hex[65];
        for (unsigned i = 0; i < 64; i++) Hex[i] = "0123456789abcdef"[i % 16];
        Hex[64] = '\0';
        Expect (NetworkPasswordUsable (Hex), "64 hex digits are a key given as such");

        PageChromeSet (false, false, true);
        NetworkDraw (&s_Surface, &Net);
        TWidget *pW = 0;
        unsigned n = NetworkWidgets (&pW);
        int nList = -1, nPassword = -1, nJoin = -1, nShow = -1;
        int nRadio[3] = { -1, -1, -1 };
        unsigned nRadios = 0;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type == WidgetList) nList = (int) i;
            if (pW[i].Type == WidgetField && pW[i].bSecret) nPassword = (int) i;
            if (pW[i].Type == WidgetButton && pW[i].pText != 0
                && strcmp (pW[i].pText, Str (StrNetJoin)) == 0) nJoin = (int) i;
            if (pW[i].Type == WidgetCheckbox) nShow = (int) i;
            if (pW[i].Type == WidgetRadio && nRadios < 3) nRadio[nRadios++] = (int) i;
        }
        Expect (nList >= 0 && pW[nList].nChoice == 0, "the network chosen is the one selected");
        Expect ((Net.Seen[0].bSecured ? pW[nList].pItems[0].Marks[0] != 0 : true)
                && pW[nList].pItems[0].Marks[1] != 0, "with its lock and its bars");
        Expect ((pW[nList].pItems[0].nState & StateChecked) != 0,
                "and the bullet of the network joined");
        Expect (nRadios == 3 && (pW[nRadio[NetworkWiFi]].nState & StateChecked) != 0,
                "Wi-Fi is the connection ticked");

        // The password is never drawn as itself: rendered, no pixel row of the
        // field may match a field showing the plain text.
        Expect (nPassword >= 0, "the password field is secret");
        TTheme T;
        ThemeMake (ThemeScaleFor (s_Surface.nWidth, s_Surface.nHeight), &T);
        GfxClear (&s_Surface, ColorWhite);
        WidgetDraw (&s_Surface, &T, &pW[nPassword]);
        const TRect F = pW[nPassword].Rect;
        static unsigned Masked[640 * 40], Plain[640 * 40];
        const unsigned nW = F.nWidth < 640 ? F.nWidth : 640;
        const unsigned nH = F.nHeight < 40 ? F.nHeight : 40;
        for (unsigned y = 0; y < nH; y++)
            for (unsigned x = 0; x < nW; x++)
                Masked[y * nW + x] = ((unsigned *) s_Surface.pPixels)
                                     [(F.nY + (int) y) * (int) s_Surface.nWidth + F.nX + (int) x];
        pW[nPassword].bSecret = false;
        GfxClear (&s_Surface, ColorWhite);
        WidgetDraw (&s_Surface, &T, &pW[nPassword]);
        for (unsigned y = 0; y < nH; y++)
            for (unsigned x = 0; x < nW; x++)
                Plain[y * nW + x] = ((unsigned *) s_Surface.pPixels)
                                    [(F.nY + (int) y) * (int) s_Surface.nWidth + F.nX + (int) x];
        pW[nPassword].bSecret = true;
        Expect (memcmp (Masked, Plain, nW * nH * sizeof Masked[0]) != 0,
                "the password is drawn as stars, not as itself");

        // Nor can it be copied out.
        WidgetFieldSelectAll (&pW[nPassword]);
        WidgetFieldCopy (&pW[nPassword]);
        TWidget Probe;
        WidgetClear (&Probe);
        char Pasted[80] = "";
        Probe.pEdit = Pasted;
        Probe.nEditSize = sizeof Pasted;
        WidgetFieldPaste (&Probe);
        Expect (strstr (Pasted, "horse") == 0, "and Copy carries nothing out of it");

        // Show turns it into text, and back.
        NetworkOperate (&Net, nShow);
        Expect (!pW[nPassword].bSecret, "Show shows it");
        NetworkOperate (&Net, nShow);
        Expect (pW[nPassword].bSecret, "and hides it again");

        // Another network clears the password typed for the last one.
        pW[nList].nChoice = 2;
        NetworkOperate (&Net, nList);
        Expect (strcmp (Net.V.SSID, Net.Seen[2].SSID) == 0 && Net.V.Password[0] == '\0',
                "choosing another network takes its name and drops the old password");

        // Join refuses what WPA would, and says so.
        strcpy (Net.V.Password, "short");
        Expect (NetworkOperate (&Net, nJoin) == NetworkNothing && Net.bBadPassword,
                "a password too short is refused before anything is tried");
        strcpy (Net.V.Password, "long enough now");
        Expect (NetworkOperate (&Net, nJoin) == NetworkJoin, "a usable one is joined");

        // Changing the connection once one runs costs a restart, and Start
        // changes its words to say so.
        Expect (!NetworkNeedsRestart (&Net), "the running connection needs nothing");
        Expect (NetworkOperate (&Net, nRadio[NetworkCable]) == NetworkRelayout
                && NetworkNeedsRestart (&Net),
                "the cable, with the Wi-Fi running, asks for a restart");
        Expect (NetworkOperate (&Net, nRadio[NetworkWiFi]) == NetworkRelayout
                && !NetworkNeedsRestart (&Net), "and back, it does not");

        // From a board with nothing running, choosing is starting.
        Net.nRunning = NetworkOff;
        Expect (NetworkOperate (&Net, nRadio[NetworkCable]) == NetworkKindChanged,
                "with nothing running, a connection chosen can start now");

        // A board without Wi-Fi offers it greyed, not hidden.
        NetworkSample (&Net);
        Net.nAvailable = (1u << NetworkOff) | (1u << NetworkCable);
        Net.nRunning = NetworkOff;
        Net.State = NetStateIdle;
        NetworkDraw (&s_Surface, &Net);
        n = NetworkWidgets (&pW);
        Expect ((pW[nRadio[NetworkWiFi]].nState & StateDisabled) != 0,
                "a board without Wi-Fi greys it");
        Expect ((pW[nList].nState & StateDisabled) != 0, "and the list with it");
    }

    printf ("\ndisplay modes\n");
    {
        TDisplaySettings Display = {};
        Display.Output = {1920, 1200};
        Display.Preferred = {1920, 1200};
        Display.Start = {640, 480};
        DisplayDraw (&s_Surface, &Display);
        TWidget *pW;
        const unsigned n = DisplayWidgets (&pW);
        int nList = -1, nRefresh = -1;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type == WidgetList)
                nList = (int) i;
            if (pW[i].Type == WidgetPopup)
                nRefresh = (int) i;
        }

        Expect (nList >= 0 && nRefresh >= 0, "the mode list and refresh menu are on the display page");
        if (nList >= 0 && nRefresh >= 0)
        {
            TWidget DefaultRows = pW[nList];
            DefaultRows.nListRowHeight = 0;
            Expect (WidgetListVisible (&pW[nList], &s_Theme) > WidgetListVisible (&DefaultRows, &s_Theme),
                    "compact resolution rows show more entries");
            const int nFirstY = pW[nList].Rect.nY + 1 + s_Theme.M.nLineHeight + s_Theme.M.nStroke;
            Expect (WidgetListItemAt (&pW[nList], &s_Theme, pW[nList].Rect.nX + 2,
                                     nFirstY + (int) pW[nList].nListRowHeight) == (int) pW[nList].nTop + 1,
                    "clicks follow the compact row height");
            const int Picks[] = {0, 1, 6};
            const int Skips[] = {0, 1, 12};
            for (unsigned i = 0; i < 3; i++)
            {
                pW[nRefresh].nChoice = Picks[i];
                DisplayOperate (&Display, nRefresh, 0);
                Expect (Display.bRefreshChanged && Display.nFrameSkip == Skips[i],
                        "the display popup stores the refresh period in VBLs");
            }
            pW[nList].nChoice = 0;
            Expect (pW[nList].pItems[0].bAction && DisplayOperate (&Display, nList, 0) == DisplayNewResolution,
                    "the first row opens the new resolution dialog");

            NewResolutionDraw (&s_Surface, &Display);
            TWidget *pNew;
            const unsigned nNew = NewResolutionWidgets (&pNew);
            int nField = 0, nCreate = -1, nCancel = -1;
            for (unsigned i = 0; i < nNew; i++)
            {
                if (pNew[i].Type == WidgetField)
                    StrAppend (pNew[i].pEdit, pNew[i].nEditSize, 0, nField++ == 0 ? "1000" : "600");
                if (pNew[i].Type == WidgetButton && pNew[i].pText == Str (StrCreate))
                    nCreate = (int) i;
                if (pNew[i].Type == WidgetButton && pNew[i].pText == Str (StrCancel))
                    nCancel = (int) i;
            }
            Expect (nCreate >= 0 && nCancel >= 0 && nField == 2, "the dialog offers two fields and both actions");
            Expect (NewResolutionOperate (&Display, nCancel) == NewResolutionCancel
                    && Display.Modes.Custom[0].nWidth == 0, "cancel preserves the existing mode list");
            NewResolutionSync (&Display);
            Expect (NewResolutionOperate (&Display, nCreate) == NewResolutionCreate,
                    "create accepts a valid resolution");
            DisplayDraw (&s_Surface, &Display);
            const unsigned nCustom = (unsigned) pW[nList].nChoice;
            Expect (Display.bModesChanged && VideoSizeEqual (Display.Modes.Custom[0], {1000, 600})
                    && (pW[nList].pItems[nCustom].nCell[0] & StateChecked) != 0,
                    "adding 1000x600 checks it in Monitors");
            DisplayOperate (&Display, nList, 2);
            Expect (Display.bStartChanged && VideoSizeEqual (Display.Start, {1000, 600}),
                    "the new resolution can start the Mac");
            DisplayOperate (&Display, nList, 1);
            Expect (VideoModeHidden (Display.Modes, {1000, 600})
                    && (pW[nList].pItems[nCustom].nCell[1] & StateDisabled) != 0,
                    "hiding it disables its startup choice");
            Expect ((pW[nList].pItems[2].nCell[0] & (StateChecked | StateDisabled))
                    == (StateChecked | StateDisabled), "VGA remains available for recovery");

            NewResolutionDraw (&s_Surface, &Display);
            nField = 0;
            for (unsigned i = 0; i < nNew; i++)
                if (pNew[i].Type == WidgetField)
                    StrAppend (pNew[i].pEdit, pNew[i].nEditSize, 0, nField++ == 0 ? "1000" : "600");
            NewResolutionSync (&Display);
            Expect (NewResolutionOperate (&Display, nCreate) == NewResolutionNothing
                    && Display.Modes.Custom[1].nWidth == 0, "a duplicate consumes no custom slot");
        }
    }

    printf ("\ninformation\n");
    {
        TInfo Info;
        InfoSample (&Info);
        Expect (Info.nCount > 0, "the pane has rows");
        InfoDraw (&s_Surface, &Info);
        TWidget *pW = 0;
        const unsigned n = InfoWidgets (&pW);
        // A page one reads: nothing of its own takes the keyboard. What does
        // is the window's — the strip, the two marks and Start — and Return
        // starts the Macintosh from here as from any page.
        unsigned nFocusable = 0, nChrome = 0;
        int nStart = -1;
        for (unsigned i = 0; i < n; i++)
        {
            if (!WidgetFocusable (&pW[i])) continue;
            nFocusable++;
            const int nAnswer = PageChromeAnswer ((int) i);
            if (nAnswer != PageAnswerNone) nChrome++;
            if (nAnswer == PageAnswerStart) nStart = (int) i;
        }
        Expect (nFocusable == nChrome, "only the window's controls take the focus");
        Expect (nStart >= 0 && (pW[nStart].nState & StateDefault) != 0,
                "Start wears the default ring");

        ScreenInit (&s_Screen, &s_Theme, pW, n);
        Expect (s_Screen.nFocus == nStart, "the keyboard starts on Start");
        const TEvent Ret2 = { EventKeyDown, OkKeyReturn, 0, 0, 0, 0 };
        const TScreenReply RB = ScreenEvent (&s_Screen, &Ret2);
        Expect (RB.Result == ScreenActivated && PageChromeAnswer (RB.nIndex) == PageAnswerStart,
                "Return starts the Macintosh");

        // A value too long is truncated rather than refused: a pane that will
        // not open is worth nothing.
        TInfo Long;
        Long.nCount = 0;
        char Big[4 * INFO_VALUE];
        for (unsigned i = 0; i + 1 < sizeof Big; i++) Big[i] = 'x';
        Big[sizeof Big - 1] = '\0';
        InfoAdd (&Long, "label", Big);
        Expect (Long.nCount == 1 && strlen (Long.Lines[0].Value) == INFO_VALUE - 1,
                "a value too long is cut, not refused");
    }

    printf ("\njournal\n");
    {
        static TJournal Journal;
        JournalClear (&Journal);
        Journal.bPrevious = false;
        // Padding past the end of the text, a colour sequence split between two
        // pieces, a carriage return and a control character: what a real log
        // file hands over.
        const char Part1[] = "first line\r\n\x1b[1mpanic: mid-";
        const char Part2[] = "\x1b[0m\n\ttabbed\nlast line\n      ";
        JournalAppend (&Journal, Part1, (unsigned) sizeof Part1 - 1);
        JournalAppend (&Journal, Part2, (unsigned) sizeof Part2 - 1);
        JournalDraw (&s_Surface, &Journal);
        TWidget *pW = 0;
        unsigned n = JournalWidgets (&pW);
        int nList = -1, nSerial = -1, nCounters = -1, nPrevious = -1;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type == WidgetList) nList = (int) i;
            if (pW[i].Type == WidgetCheckbox)
            {
                if (pW[i].pText == Str (StrJournalSerial)) nSerial = (int) i;
                if (pW[i].pText == Str (StrJournalCounters)) nCounters = (int) i;
                if (pW[i].pText == Str (StrJournalPrevious)) nPrevious = (int) i;
            }
        }
        Expect (nList >= 0 && nSerial >= 0 && nCounters >= 0 && nPrevious >= 0,
                "the page has its list and its three boxes");
        Expect (JournalLines () == 4 && pW[nList].nItems == 4,
                "four lines: the padding, the carriage return and the colours are gone");
        Expect (strcmp (pW[nList].pItems[1].pText, "panic: mid-") == 0,
                "a colour sequence split between two reads leaves no trace");
        Expect (strcmp (pW[nList].pItems[2].pText, " tabbed") == 0, "a control character is a blank");
        Expect (pW[nList].nChoice == 3, "the list opens on the newest line");

        // The switches act on the model, and the page says so.
        Expect (JournalOperate (&Journal, nSerial) == JournalNothing
                && Journal.bSerial && Journal.bSerialChanged && !Journal.bCountersChanged,
                "Serial output ticks and says it changed");
        Expect ((pW[nSerial].nState & StateChecked) != 0, "and shows it");
        JournalOperate (&Journal, nCounters);
        Expect (Journal.bCounters && Journal.bCountersChanged
                && (pW[nCounters].nState & StateChecked) != 0, "so do the counters");
        JournalOperate (&Journal, nSerial);
        Expect (!Journal.bSerial && (pW[nSerial].nState & StateChecked) == 0,
                "and a second press unticks it");
        Expect (JournalOperate (&Journal, nPrevious) == JournalReload && Journal.bPrevious
                && (pW[nPrevious].nState & StateChecked) != 0,
                "Previous session asks for the other file's text");

        // No text: one grey line that says so, and nothing to scroll.
        JournalClear (&Journal);
        JournalDraw (&s_Surface, &Journal);
        n = JournalWidgets (&pW);
        for (unsigned i = 0; i < n; i++)
            if (pW[i].Type == WidgetList) nList = (int) i;
        Expect (pW[nList].nItems == 1 && (pW[nList].pItems[0].nState & StateDisabled) != 0
                && JournalLines () == 0, "an empty log says there is none");

        // Past the room: the newest lines are kept, whole.
        JournalClear (&Journal);
        for (unsigned i = 0; i < 3000; i++)
        {
            char Line[48];
            snprintf (Line, sizeof Line, "line %04u of a long session\n", i);
            JournalAppend (&Journal, Line, (unsigned) strlen (Line));
        }
        JournalDraw (&s_Surface, &Journal);
        n = JournalWidgets (&pW);
        for (unsigned i = 0; i < n; i++)
            if (pW[i].Type == WidgetList) nList = (int) i;
        Expect (pW[nList].nItems == JOURNAL_LINES
                && strcmp (pW[nList].pItems[pW[nList].nItems - 1].pText,
                           "line 2999 of a long session") == 0,
                "a long log shows its end");
        Expect (pW[nList].pItems[0].pText[0] == 'l'
                && strncmp (pW[nList].pItems[0].pText, "line ", 5) == 0,
                "and begins on a whole line");
        // Drawn again, as a relayout does: the same lines, nothing lost twice.
        const unsigned nFirst = JournalLines ();
        const char *pFirst = pW[nList].pItems[0].pText;
        char Saved[40];
        snprintf (Saved, sizeof Saved, "%s", pFirst);
        JournalDraw (&s_Surface, &Journal);
        n = JournalWidgets (&pW);
        for (unsigned i = 0; i < n; i++)
            if (pW[i].Type == WidgetList) nList = (int) i;
        Expect (JournalLines () == nFirst && strcmp (pW[nList].pItems[0].pText, Saved) == 0,
                "a second layout finds the same lines");
    }

    free (s_pPixels);
    printf ("\n%u failure(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
