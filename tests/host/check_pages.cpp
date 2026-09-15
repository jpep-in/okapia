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
    printf ("  %s %s\n", bOK ? "ok  " : "ECHEC", pWhat);
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
                printf ("    hors cadre : %s en (%d,%d %ux%u), contenu (%d,%d %ux%u)\n",
                        W.pText ? W.pText : "(sans texte)", R.nX, R.nY, R.nWidth, R.nHeight,
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
                    printf ("    chevauchement : %s / %s\n",
                            A.pText ? A.pText : "(sans texte)",
                            B.pText ? B.pText : "(sans texte)");
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
    CheckOverlap (&T, pW, n, "réglages : rien ne se chevauche");
    CheckFrame (&T, SettingsDialog (), pW, n, "réglages : rien ne dépasse");

    TInfo Info;
    InfoSample (&Info);
    InfoDraw (&s_Surface, &Info);
    n = InfoWidgets (&pW);
    CheckOverlap (&T, pW, n, "informations : rien ne se chevauche");
    CheckFrame (&T, InfoDialog (), pW, n, "informations : rien ne dépasse");
    // A page that asks for more bands than it has answers the remainder, so its
    // last rows collapse instead of spilling — which no margin check can see.
    unsigned nFlat = 0;
    for (unsigned i = 0; i < n; i++)
    {
        if (pW[i].Rect.nHeight == 0) nFlat++;
    }
    Expect (nFlat == 0, "informations : toutes les lignes ont leur place");

    TConfirm Confirm;
    ConfirmSample (&Confirm);
    ConfirmDraw (&s_Surface, &Confirm);
    n = ConfirmWidgets (&pW);
    CheckOverlap (&T, pW, n, "confirmation : rien ne se chevauche");
    CheckFrame (&T, ConfirmDialog (), pW, n, "confirmation : rien ne dépasse");

    TNewVolume New;
    NewVolumeSample (&New);
    NewVolumeDraw (&s_Surface, &New);
    n = NewVolumeWidgets (&pW);
    CheckOverlap (&T, pW, n, "nouveau volume : rien ne se chevauche");
    CheckFrame (&T, NewVolumeDialog (), pW, n, "nouveau volume : rien ne dépasse");
    Expect (!NewVolumeOverflowed (), "nouveau volume : la feuille tient");
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

    printf ("\nles réglages\n");
    s_Surface.nWidth  = 640;
    s_Surface.nHeight = 480;
    s_Surface.nPitch  = 640 * (unsigned) sizeof (unsigned);
    Open ();

    // Nothing has been touched, so nothing asks for a restart and the button
    // says the smaller of the two things it can say.
    Expect (!SettingsNeedsRestart (&s_Settings), "à l'ouverture, aucun redémarrage requis");

    // La case qui ouvre le menu à chaque démarrage. Elle est à Okapia et pas au
    // Macintosh, donc elle ne demande aucun redémarrage : le prochain allumage
    // la lit, c'est tout.
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
        Expect (nBox >= 0, "la case du menu de démarrage est là");
        const bool bWas = s_Settings.V.bBootMenu;
        SettingsOperate (&s_Settings, nBox);
        Expect (s_Settings.V.bBootMenu != bWas, "elle bascule");
        Expect (((pC[nBox].nState & StateChecked) != 0) == s_Settings.V.bBootMenu,
                "et la case suit le modèle");
        Expect (!SettingsNeedsRestart (&s_Settings),
                "sans demander de redémarrage : le prochain allumage la lit");
        SettingsOperate (&s_Settings, nBox);
    }
    Expect (Find (WidgetButton, Str (StrSave)) >= 0, "le bouton dit « Enregistrer »");

    // The memory is the one setting the board cannot take at a Macintosh's
    // start, because its block is claimed before any of this runs.
    Choose (Popup (0), 0);
    // The assent is about to change its words, so it is about to change its
    // width: the page says so rather than draw one label in another's button.
    Expect (SettingsOperate (&s_Settings, Popup (0)) == SettingsRelayout,
            "changer la mémoire demande une nouvelle mise en page");
    Expect (s_Settings.V.nMemoryMB == 64, "et la valeur est retenue");
    Expect (SettingsNeedsRestart (&s_Settings), "la mémoire demande un redémarrage");
    SettingsDraw (&s_Surface, &s_Settings);
    Expect (Find (WidgetButton, Str (StrSaveRestart)) >= 0,
            "le bouton devient « Enregistrer et redémarrer »");
    Expect (SettingsOperate (&s_Settings, Find (WidgetButton, Str (StrSaveRestart)))
            == SettingsSaveRestart, "et il répond bien cela");

    // Back to what it was: the question is what changed since the screen
    // opened, not what was ever touched.
    Choose (Popup (0), 2);
    SettingsOperate (&s_Settings, Popup (0));
    Expect (!SettingsNeedsRestart (&s_Settings), "revenu à sa valeur, plus rien à redémarrer");
    SettingsDraw (&s_Surface, &s_Settings);
    Expect (Find (WidgetButton, Str (StrSave)) >= 0, "et le bouton reprend son mot");

    // The refresh rate is a frame count in the file and a rate on the screen.
    // Dynamic is zero, and it is the first entry because it is the default.
    Choose (Popup (1), 0);
    SettingsOperate (&s_Settings, Popup (1));
    Expect (s_Settings.V.nFrameSkip == 0, "Dynamique vaut zéro");
    Choose (Popup (1), 1);
    SettingsOperate (&s_Settings, Popup (1));
    Expect (s_Settings.V.nFrameSkip == 1, "60 Hz vaut une VBL par image");
    Choose (Popup (1), 6);
    SettingsOperate (&s_Settings, Popup (1));
    Expect (s_Settings.V.nFrameSkip == 12, "5 Hz en vaut douze");

    // Off is first, so a value that cannot be read leaves the machine silent —
    // a device claimed and not working is what froze the guest once already.
    Expect (SoundOff == 0, "le son coupé est la première valeur");
    Choose (Popup (3), SoundHDMI);
    SettingsOperate (&s_Settings, Popup (3));
    Expect (s_Settings.V.nSound == SoundHDMI, "on peut choisir la sortie HDMI");

    // A board without USB audio shows the entry and greys it. An option that
    // vanishes between two machines reads as a version difference; a grey one
    // says what is true — and the loop refuses to choose it, which is what
    // makes the grey more than a colour.
    {
        TWidget *pW = 0;
        SettingsWidgets (&pW);
        const TWidget &Menu = pW[Popup (3)];
        Expect ((Menu.pItems[SoundUSB].nState & StateDisabled) != 0,
                "sans USB, l'entrée est là et inactive");
        Expect ((Menu.pItems[SoundJack].nState & StateDisabled) == 0,
                "et la prise casque ne l'est pas");
    }

    // The shared folder's name is a name only while there is a volume to carry
    // it. The field says so by going grey, not by refusing letters.
    const int nShared = Find (WidgetCheckbox, Str (StrSharedFolder));
    Expect (nShared >= 0, "le dossier partagé a sa case");
    SettingsOperate (&s_Settings, nShared);
    Expect (!s_Settings.V.bShared, "on peut le couper");
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
        Expect (nFields == 2, "le partage a deux champs : où il est et comment il s'appelle");
        Expect (nGrey == 2, "et les deux deviennent inactifs, tabulation comprise");
    }

    // The language applies at once and the page has to be laid out again: its
    // labels are not the same width twice.
    {
        const TLanguage Was = StringsLanguage ();
        const unsigned nOther = (Was + 1) % LanguageCount;
        Choose (Popup (4), (int) nOther);
        Expect (SettingsOperate (&s_Settings, Popup (4)) == SettingsRelayout,
                "changer de langue demande une nouvelle mise en page");
        Expect (StringsLanguage () == (TLanguage) nOther, "et la langue a déjà changé");
        StringsSetLanguage (Was);
    }

    printf ("\nla confirmation\n");
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
        Expect (nYes >= 0 && nNo >= 0, "les deux réponses sont là");
        Expect (nYes >= 0 && (pW[nYes].nState & StateDefault) != 0,
                "l'assentiment porte l'anneau par défaut");
        Expect (ConfirmOperate (nYes) == ConfirmYes, "oui répond oui");
        Expect (ConfirmOperate (nNo)  == ConfirmNo,  "non répond non");

        // Return is the default button wherever the focus is — that is what the
        // ring around it promises, and a confirmation is where it matters.
        const TEvent Ret = { EventKeyDown, OkKeyReturn, 0, 0, 0, 0 };
        const TScreenReply R = ScreenEvent (&s_Screen, &Ret);
        Expect (R.Result == ScreenActivated && ConfirmOperate (R.nIndex) == ConfirmYes,
                "Entrée vaut l'assentiment");

        // Et Échap vaut le refus, ce qui est l'autre moitié du marché : deux
        // réponses, deux touches, et donc rien à parcourir au clavier.
        const TEvent Esc = { EventKeyDown, OkKeyEscape, 0, 0, 0, 0 };
        Expect (ScreenEvent (&s_Screen, &Esc).Result == ScreenCancelled,
                "Échap vaut le refus");

        // Aucun des deux boutons ne prend le focus, et l'écran n'en pose nulle
        // part : l'anneau du bouton par défaut disait « Entrée fait ceci », et
        // celui du focus « le clavier est ici » — deux anneaux l'un dans
        // l'autre pour dire la même chose.
        Expect (!WidgetFocusable (&pW[nYes]) && !WidgetFocusable (&pW[nNo]),
                "aucune des deux réponses ne se focalise");
        Expect (s_Screen.nFocus < 0, "et l'alerte n'a pas de focus du tout");
        unsigned nRings = 0;
        for (unsigned i = 0; i < n; i++)
        {
            if ((pW[i].nState & StateFocused) != 0) nRings++;
        }
        Expect (nRings == 0, "donc rien ne porte deux anneaux");

        // Tab ne fait rien non plus, faute de quoi qu'on lui donne.
        const TEvent Tab = { EventKeyDown, OkKeyTab, 0, 0, 0, 0 };
        ScreenEvent (&s_Screen, &Tab);
        Expect (s_Screen.nFocus < 0, "et Tab n'en invente pas");

        // Les choix : quel Macintosh oublier. Des boutons radio qu'on clique et
        // qui ne prennent jamais le clavier — sinon Entrée cocherait au lieu
        // d'acquiescer.
        int nFirst = -1;
        unsigned nRadios = 0, nMarked = 0;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type != WidgetRadio) continue;
            if (nFirst < 0) nFirst = (int) i;
            nRadios++;
            if (pW[i].nState & StateChecked) nMarked++;
            Expect (!WidgetFocusable (&pW[i]), "un choix ne prend pas le focus");
        }
        Expect (nRadios == C.nChoices, "un bouton radio par choix");
        Expect (nMarked == 1 && ConfirmChoice () == C.nChoice,
                "un seul coché, celui qu'on a demandé");

        // Un clic sur « Les deux » le coche, décoche l'autre, et l'alerte reste.
        const TWidget &Both = pW[nFirst + 2];
        const TEvent Down = { EventMouseDown, 0, 0, 0,
                              Both.Rect.nX + 4, Both.Rect.nY + (int) Both.Rect.nHeight / 2 };
        TEvent Up = Down;
        Up.Type = EventMouseUp;
        ScreenEvent (&s_Screen, &Down);
        const TScreenReply Click = ScreenEvent (&s_Screen, &Up);
        Expect (Click.Result == ScreenActivated && ConfirmOperate (Click.nIndex) == ConfirmWaiting,
                "cliquer un choix ne répond pas à la question");
        Expect (ConfirmChoice () == 2, "et c'est lui qui est coché désormais");
        Expect (s_Screen.nFocus < 0, "sans que le focus bouge");
        const TScreenReply R2 = ScreenEvent (&s_Screen, &Ret);
        Expect (R2.Result == ScreenActivated && ConfirmOperate (R2.nIndex) == ConfirmYes,
                "Entrée vaut toujours l'assentiment après un choix");

        // Sans choix, l'alerte n'en montre pas.
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
        Expect (nPlainRadios == 0 && ConfirmChoice () == 0, "et une alerte simple n'a pas de choix");
    }

    printf ("\nles informations\n");
    {
        TInfo Info;
        InfoSample (&Info);
        Expect (Info.nCount > 0, "le volet a des lignes");
        InfoDraw (&s_Surface, &Info);
        TWidget *pW = 0;
        const unsigned n = InfoWidgets (&pW);
        unsigned nFocusable = 0;
        for (unsigned i = 0; i < n; i++)
        {
            if (WidgetFocusable (&pW[i])) nFocusable++;
        }
        // Une page qu'on lit : une seule sortie, et donc rien à parcourir au
        // clavier. Entrée en sort, Échap aussi, et aucun anneau ne se superpose
        // à celui du bouton par défaut.
        Expect (nFocusable == 0, "rien ne s'y focalise");
        int nBack = -1;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type == WidgetButton) nBack = (int) i;
        }
        Expect (nBack >= 0 && InfoIsBack (nBack), "et la seule sortie est le retour");
        Expect (nBack >= 0 && (pW[nBack].nState & StateDefault) != 0,
                "qui porte l'anneau par défaut, et lui seul");

        ScreenInit (&s_Screen, &s_Theme, pW, n);
        Expect (s_Screen.nFocus < 0, "l'écran s'ouvre sans focus");
        const TEvent Ret2 = { EventKeyDown, OkKeyReturn, 0, 0, 0, 0 };
        const TScreenReply RB = ScreenEvent (&s_Screen, &Ret2);
        Expect (RB.Result == ScreenActivated && InfoIsBack (RB.nIndex),
                "Entrée sort du volet");

        // A value too long is truncated rather than refused: a pane that will
        // not open is worth nothing.
        TInfo Long;
        Long.nCount = 0;
        char Big[4 * INFO_VALUE];
        for (unsigned i = 0; i + 1 < sizeof Big; i++) Big[i] = 'x';
        Big[sizeof Big - 1] = '\0';
        InfoAdd (&Long, "label", Big);
        Expect (Long.nCount == 1 && strlen (Long.Lines[0].Value) == INFO_VALUE - 1,
                "une valeur trop longue est coupée, pas refusée");
    }

    free (s_pPixels);
    printf ("\n%u écart(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
