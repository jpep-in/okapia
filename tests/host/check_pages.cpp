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
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "okapia_confirm.h"
#include "okapia_info.h"
#include "okapia_newvolume.h"
#include "okapia_screen.h"
#include "okapia_settings.h"
#include "okapia_strings.h"

void SettingsSample (TSettings *p);
void InfoSample (TInfo *p);
void ConfirmSample (TConfirm *p);
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
        const int r = (int) ThemeReach (pTheme, W.nState);
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
                || A.Type == WidgetParagraph || B.Type == WidgetParagraph
                || A.Type == WidgetIcon      || B.Type == WidgetIcon)
            {
                continue;
            }
            const bool bInA = A.Type == WidgetCheckbox || A.Type == WidgetRadio;
            const bool bInB = B.Type == WidgetCheckbox || B.Type == WidgetRadio;
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

    TInfo Info;
    InfoSample (&Info);
    InfoDraw (&s_Surface, &Info);
    n = InfoWidgets (&pW);
    CheckOverlap (&T, pW, n, "information: nothing overlaps");
    CheckFrame (&T, InfoDialog (), pW, n, "information: nothing overflows");
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

    TConfirm Confirm;
    ConfirmSample (&Confirm);
    ConfirmDraw (&s_Surface, &Confirm);
    n = ConfirmWidgets (&pW);
    CheckOverlap (&T, pW, n, "confirmation: nothing overlaps");
    CheckFrame (&T, ConfirmDialog (), pW, n, "confirmation: nothing overflows");

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

    // The tick box that opens the menu at every startup. It belongs to Okapia,
    // not to the Macintosh, so it asks for no restart: the next power-on reads
    // it, and that is all.
    {
        TWidget *pC = 0;
        const unsigned nAll = SettingsWidgets (&pC);
        int nBox = -1;
        for (unsigned i = 0; i < nAll; i++)
        {
            if (pC[i].Type == WidgetCheckbox && pC[i].pText != 0
                && strcmp (pC[i].pText, Str (StrBootMenu)) == 0)
            {
                nBox = (int) i;
            }
        }
        Expect (nBox >= 0, "the boot menu tick box is there");
        const bool bWas = s_Settings.V.bBootMenu;
        SettingsOperate (&s_Settings, nBox);
        Expect (s_Settings.V.bBootMenu != bWas, "it toggles");
        Expect (((pC[nBox].nState & StateChecked) != 0) == s_Settings.V.bBootMenu,
                "and the box follows the model");
        Expect (!SettingsNeedsRestart (&s_Settings),
                "without asking for a restart: the next power-on reads it");
        SettingsOperate (&s_Settings, nBox);
    }
    Expect (Find (WidgetButton, Str (StrSave)) >= 0, "the button says Save");

    // The memory is the one setting the board cannot take at a Macintosh's
    // start, because its block is claimed before any of this runs.
    Choose (Popup (0), 0);
    // The assent is about to change its words, so it is about to change its
    // width: the page says so rather than draw one label in another's button.
    Expect (SettingsOperate (&s_Settings, Popup (0)) == SettingsRelayout,
            "changing the memory asks for a new layout");
    Expect (s_Settings.V.nMemoryMB == 64, "and the value is kept");
    Expect (SettingsNeedsRestart (&s_Settings), "the memory asks for a restart");
    SettingsDraw (&s_Surface, &s_Settings);
    Expect (Find (WidgetButton, Str (StrSaveRestart)) >= 0,
            "the button becomes Save and Restart");
    Expect (SettingsOperate (&s_Settings, Find (WidgetButton, Str (StrSaveRestart)))
            == SettingsSaveRestart, "and it answers exactly that");

    // Back to what it was: the question is what changed since the screen
    // opened, not what was ever touched.
    Choose (Popup (0), 2);
    SettingsOperate (&s_Settings, Popup (0));
    Expect (!SettingsNeedsRestart (&s_Settings), "back to its value, nothing left to restart");
    SettingsDraw (&s_Surface, &s_Settings);
    Expect (Find (WidgetButton, Str (StrSave)) >= 0, "and the button takes its word back");

    // The refresh rate is a frame count in the file and a rate on the screen.
    // Dynamic is zero, and it is the first entry because it is the default.
    Choose (Popup (1), 0);
    SettingsOperate (&s_Settings, Popup (1));
    Expect (s_Settings.V.nFrameSkip == 0, "Dynamic is zero");
    Choose (Popup (1), 1);
    SettingsOperate (&s_Settings, Popup (1));
    Expect (s_Settings.V.nFrameSkip == 1, "60 Hz is one VBL per frame");
    Choose (Popup (1), 6);
    SettingsOperate (&s_Settings, Popup (1));
    Expect (s_Settings.V.nFrameSkip == 12, "5 Hz is twelve");

    // Off is first, so a value that cannot be read leaves the machine silent —
    // a device claimed and not working is what froze the guest once already.
    Expect (SoundOff == 0, "sound off is the first value");
    Choose (Popup (3), SoundHDMI);
    SettingsOperate (&s_Settings, Popup (3));
    Expect (s_Settings.V.nSound == SoundHDMI, "the HDMI output can be chosen");

    // A board without USB audio shows the entry and greys it. An option that
    // vanishes between two machines reads as a version difference; a grey one
    // says what is true — and the loop refuses to choose it, which is what
    // makes the grey more than a colour.
    {
        TWidget *pW = 0;
        SettingsWidgets (&pW);
        const TWidget &Menu = pW[Popup (3)];
        Expect ((Menu.pItems[SoundUSB].nState & StateDisabled) != 0,
                "without USB, the entry is there and disabled");
        Expect ((Menu.pItems[SoundJack].nState & StateDisabled) == 0,
                "and the headphone jack is not");
    }

    // The shared folder's name is a name only while there is a volume to carry
    // it. The field says so by going grey, not by refusing letters.
    const int nShared = Find (WidgetCheckbox, Str (StrSharedFolder));
    Expect (nShared >= 0, "the shared folder has its tick box");
    SettingsOperate (&s_Settings, nShared);
    Expect (!s_Settings.V.bShared, "it can be turned off");
    {
        TWidget *pW = 0;
        const unsigned n = SettingsWidgets (&pW);
        unsigned nFields = 0, nGrey = 0;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type != WidgetField) continue;
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
        Choose (Popup (4), (int) nOther);
        Expect (SettingsOperate (&s_Settings, Popup (4)) == SettingsRelayout,
                "changing the language asks for a new layout");
        Expect (StringsLanguage () == (TLanguage) nOther, "and the language has already changed");
        StringsSetLanguage (Was);
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

    printf ("\ninformation\n");
    {
        TInfo Info;
        InfoSample (&Info);
        Expect (Info.nCount > 0, "the pane has rows");
        InfoDraw (&s_Surface, &Info);
        TWidget *pW = 0;
        const unsigned n = InfoWidgets (&pW);
        unsigned nFocusable = 0;
        for (unsigned i = 0; i < n; i++)
        {
            if (WidgetFocusable (&pW[i])) nFocusable++;
        }
        // A page one reads: a single way out, and so nothing to walk through
        // with the keyboard. Return leaves it, so does Escape, and no ring lies
        // over the default button's.
        Expect (nFocusable == 0, "nothing there takes the focus");
        int nBack = -1;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type == WidgetButton) nBack = (int) i;
        }
        Expect (nBack >= 0 && InfoIsBack (nBack), "and the only way out is Back");
        Expect (nBack >= 0 && (pW[nBack].nState & StateDefault) != 0,
                "which wears the default ring, and it alone");

        ScreenInit (&s_Screen, &s_Theme, pW, n);
        Expect (s_Screen.nFocus < 0, "the screen opens with no focus");
        const TEvent Ret2 = { EventKeyDown, OkKeyReturn, 0, 0, 0, 0 };
        const TScreenReply RB = ScreenEvent (&s_Screen, &Ret2);
        Expect (RB.Result == ScreenActivated && InfoIsBack (RB.nIndex),
                "Return leaves the pane");

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

    free (s_pPixels);
    printf ("\n%u failure(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
