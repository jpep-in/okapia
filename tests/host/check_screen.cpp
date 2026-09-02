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
    printf ("  %s %s\n", bOK ? "ok  " : "ECHEC", pWhat);
    if (!bOK)
    {
        s_nFailures++;
    }
}

// The screen every check below runs on: something of each kind, a list of three
// rows, two radio groups, and one control nobody can reach.
//
//   0 label      (never focusable)
//   1 button "Démarrer", the default one
//   2 button "Réglages"
//   3 button "Éteinte", disabled
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
    { "Système 7.1.2",   0, StateNormal   },
    { "Mac OS 8.1",      0, StateNormal   },
    { "Système 6.0.8",   0, StateNormal   },
    { "Mac OS 9.1",      0, StateNormal   },
    { "Sauvegarde",      0, StateNormal   },
    { "Données",         0, StateDisabled },
    { "Travaux",         0, StateNormal   }
};
static const unsigned ITEMS = sizeof s_Items / sizeof s_Items[0];
static const unsigned VISIBLE = 3;

static char s_Edit[16];

// A pop-up's choices. Four, one of them out of reach, so that both the walk and
// the refusal are exercised.
static const TListItem s_Rates[] =
{
    { "Dynamique",   0, StateNormal   },
    { "60 images/s", 0, StateNormal   },
    { "30 images/s", 0, StateDisabled },
    { "15 images/s", 0, StateNormal   }
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

    printf ("Parcours du focus\n");
    Expect (FocusIs (&S, WStart), "le focus part sur le premier contrôle, pas sur l'étiquette");

    Send (&S, Key (OkKeyTab, 0));
    Expect (FocusIs (&S, WSettings), "Tab avance");

    Send (&S, Key (OkKeyTab, 0));
    Expect (FocusIs (&S, WCheck), "Tab saute le bouton inactif");

    Send (&S, Key (OkKeyTab, ModShift));
    Expect (FocusIs (&S, WSettings), "Maj-Tab revient");

    // A screen that says where its focus starts is obeyed: the alert of phase
    // 16i opens on its field, not on whichever control it placed first.
    {
        TWidget V[WCount];
        TScreen T;
        Build (V);
        V[WCheck].nState |= StateFocused;
        ScreenInit (&T, &s_Theme, V, WCount);
        Expect (T.nFocus == WCheck, "un écran qui déclare son focus est suivi");
        Send (&T, Key (OkKeyTab, 0));
        Expect (FocusIs (&T, WRadioA), "et Tab repart de là");
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
    Expect (FocusIs (&S, nWas), "Tab fait le tour et revient au même");
}

static void CheckOperating (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);

    printf ("Actionner au clavier\n");

    TScreenReply r = Send (&S, Key (OkKeyReturn, 0));
    Expect (r.Result == ScreenActivated && r.nIndex == WStart,
            "Retour actionne le bouton par défaut");

    Send (&S, Key (OkKeyTab, 0));                 // Réglages
    r = Send (&S, Key (OkKeyReturn, 0));
    Expect (r.Result == ScreenActivated && r.nIndex == WStart,
            "Retour reste sur le bouton par défaut, pas sur le focus");

    r = Send (&S, Key (OkKeySpace, 0));
    Expect (r.Result == ScreenActivated && r.nIndex == WSettings,
            "Espace actionne le contrôle qui a le focus");

    Send (&S, Key (OkKeyTab, 0));                 // la case
    Expect (!Checked (&S, WCheck), "la case part décochée");
    Send (&S, Key (OkKeySpace, 0));
    Expect (Checked (&S, WCheck), "Espace coche");
    Send (&S, Key (OkKeySpace, 0));
    Expect (!Checked (&S, WCheck), "Espace décoche");

    Send (&S, Key (OkKeyTab, 0));                 // radio A, déjà choisi
    Send (&S, Key (OkKeyTab, 0));                 // radio B
    Send (&S, Key (OkKeySpace, 0));
    Expect (Checked (&S, WRadioB) && !Checked (&S, WRadioA),
            "un radio en chasse un autre");
    Expect (Checked (&S, WOther), "et laisse l'autre groupe tranquille");

    r = Send (&S, Key (OkKeyEscape, 0));
    Expect (r.Result == ScreenCancelled, "Échap annule");
}

static void CheckList (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);

    printf ("La liste, et son défilement\n");

    Send (&S, Key (OkKeyDown, 0));
    Expect (W[WList].nChoice < 0, "une flèche hors d'une liste ne fait rien");

    // Bounded, so that a loop that can no longer reach the list fails here
    // instead of hanging the build.
    for (unsigned i = 0; i < WCount && S.nFocus != WList; i++)
    {
        Send (&S, Key (OkKeyTab, 0));
    }
    Expect (FocusIs (&S, WList), "le focus se pose sur la liste");
    Expect (WidgetListVisible (&W[WList], &s_Theme) == VISIBLE,
            "trois lignes visibles sur sept");

    Send (&S, Key (OkKeyDown, 0));
    Expect (W[WList].nChoice == 0, "Bas choisit la première ligne");
    Send (&S, Key (OkKeyDown, 0));
    Send (&S, Key (OkKeyDown, 0));
    Expect (W[WList].nChoice == 2 && W[WList].nTop == 0,
            "descendre dans ce qui est visible ne fait pas défiler");

    Send (&S, Key (OkKeyDown, 0));
    Expect (W[WList].nChoice == 3 && W[WList].nTop == 1,
            "passer la dernière ligne visible fait défiler d'une ligne");

    Send (&S, Key (OkKeyDown, 0));
    Send (&S, Key (OkKeyDown, 0));
    Expect (W[WList].nChoice == 6, "une ligne inactive est enjambée");

    TScreenReply r = Send (&S, Key (OkKeyDown, 0));
    Expect (W[WList].nChoice == 6 && r.Result == ScreenIdle,
            "la liste s'arrête en bas au lieu de reboucler");
    Expect (W[WList].nTop == ITEMS - VISIBLE, "et le défilement s'arrête avec elle");

    Send (&S, Key (OkKeyHome, 0));
    Expect (W[WList].nChoice == 0 && W[WList].nTop == 0, "Début revient au sommet");

    Send (&S, Key (OkKeyPageDown, 0));
    Expect (W[WList].nChoice == 2, "Page suivante avance d'une page moins une ligne");

    Send (&S, Key (OkKeyEnd, 0));
    Expect (W[WList].nChoice == 6 && W[WList].nTop == ITEMS - VISIBLE,
            "Fin va à la dernière et l'amène en vue");
}

static void CheckScroller (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);

    printf ("L'ascenseur de la liste\n");

    const TRect Bar = WidgetListScroller (&W[WList], &s_Theme);
    Expect (Bar.nWidth != 0, "sept articles pour trois lignes : il y a un ascenseur");

    const TRect Thumb = ThemeScrollThumb (&s_Theme, Bar, W[WList].nTop, VISIBLE, ITEMS);
    Expect (Thumb.nHeight * ITEMS >= (Bar.nHeight - 2) * VISIBLE - ITEMS
            && Thumb.nHeight * ITEMS <= (Bar.nHeight - 2) * VISIBLE + ITEMS,
            "le curseur occupe de la piste ce que la vue occupe du tout");

    const int nMid = Bar.nX + (int) Bar.nWidth / 2;

    // Below the thumb: a page down, not a jump to where the click landed.
    Send (&S, Mouse (EventMouseDown, nMid, Bar.nY + (int) Bar.nHeight - 2));
    Send (&S, Mouse (EventMouseUp,   nMid, Bar.nY + (int) Bar.nHeight - 2));
    Expect (W[WList].nTop == VISIBLE - 1, "cliquer sous le curseur avance d'une page");
    Expect (W[WList].nChoice < 0, "et ne choisit rien : on a bougé la vue, pas la sélection");

    Send (&S, Mouse (EventMouseDown, nMid, Bar.nY + 1));
    Send (&S, Mouse (EventMouseUp,   nMid, Bar.nY + 1));
    Expect (W[WList].nTop == 0, "cliquer au-dessus recule d'autant");

    // Taking hold of the thumb and dragging it to the bottom.
    const TRect T0 = ThemeScrollThumb (&s_Theme, Bar, 0, VISIBLE, ITEMS);
    Send (&S, Mouse (EventMouseDown, nMid, T0.nY + (int) T0.nHeight / 2));
    Send (&S, Mouse (EventMouseMove, nMid, Bar.nY + (int) Bar.nHeight));
    Expect (W[WList].nTop == ITEMS - VISIBLE, "tirer le curseur en bas défile jusqu'au bout");
    Send (&S, Mouse (EventMouseMove, nMid, Bar.nY - 20));
    Expect (W[WList].nTop == 0, "et le remonter revient au sommet");
    Send (&S, Mouse (EventMouseUp, nMid, Bar.nY - 20));
    Expect (S.nDragList < 0, "relâcher lâche le curseur");
}

static void CheckField (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);

    printf ("Le champ éditable\n");

    for (unsigned i = 0; i < WCount && S.nFocus != WField; i++)
    {
        Send (&S, Key (OkKeyTab, 0));
    }
    Expect (FocusIs (&S, WField), "le focus atteint le champ");

    Send (&S, Char ('!'));
    Expect (strcmp (s_Edit, "Okapia!") == 0 && W[WField].nCaret == 7,
            "une frappe s'insère au curseur");

    Send (&S, Key (OkKeyBackspace, 0));
    Expect (strcmp (s_Edit, "Okapia") == 0, "retour arrière efface");

    Send (&S, Key (OkKeyLeft, 0));
    Send (&S, Key (OkKeyLeft, 0));
    Send (&S, Char ('X'));
    Expect (strcmp (s_Edit, "OkapXia") == 0, "on tape au milieu, pas à la fin");

    Send (&S, Key (OkKeyDelete, 0));
    Expect (strcmp (s_Edit, "OkapXa") == 0, "Suppr efface devant");

    Send (&S, Key (OkKeyHome, 0));
    Send (&S, Char (0xE9));                             // é
    Expect (strcmp (s_Edit, "\xC3\xA9OkapXa") == 0,
            "un caractère accentué s'écrit en deux octets");
    Send (&S, Key (OkKeyRight, 0));
    Expect (W[WField].nCaret == 3, "et la flèche l'enjambe d'un seul coup");
    Send (&S, Key (OkKeyLeft, 0));
    Send (&S, Key (OkKeyBackspace, 0));
    Expect (strcmp (s_Edit, "OkapXa") == 0, "et le retour arrière l'efface entier");

    // Fills the buffer and then some: refusing is right, half a character is not.
    Send (&S, Key (OkKeyEnd, 0));
    for (unsigned i = 0; i < 40; i++)
    {
        Send (&S, Char ('a'));
    }
    Expect (strlen (s_Edit) == sizeof s_Edit - 1, "le champ plein refuse au lieu de déborder");

    // Tab still means Tab: a field takes the keys that are its own and no others.
    Send (&S, Key (OkKeyTab, 0));
    Expect (S.nFocus != WField, "Tab quitte le champ au lieu d'y être écrit");
}

static void CheckMenu (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);
    S.Bounds = Rect (0, 0, 640, 480);

    printf ("Le menu d'une déroulante\n");

    for (unsigned i = 0; i < WCount && S.nFocus != WPopup; i++)
    {
        Send (&S, Key (OkKeyTab, 0));
    }
    Expect (FocusIs (&S, WPopup), "le focus atteint la déroulante");
    Expect (!ScreenMenuOpen (&S), "et son menu est fermé");

    Send (&S, Key (OkKeySpace, 0));
    Expect (ScreenMenuOpen (&S), "Espace l'ouvre");
    Expect (S.Menu.nChoice == 0, "sur le choix courant");

    Send (&S, Key (OkKeyDown, 0));
    Expect (S.Menu.nChoice == 1, "Bas descend dedans");
    Send (&S, Key (OkKeyDown, 0));
    Expect (S.Menu.nChoice == 3, "et enjambe un choix hors d'atteinte");
    Expect (W[WPopup].nChoice == 0, "sans rien changer tant qu'on n'a pas validé");

    TScreenReply r = Send (&S, Key (OkKeyReturn, 0));
    Expect (r.Result == ScreenActivated && r.nIndex == WPopup, "Retour valide");
    Expect (W[WPopup].nChoice == 3, "et le choix est pris");
    Expect (!ScreenMenuOpen (&S), "le menu est refermé");
    Expect (S.bDirtyAll, "et tout l'écran est à redessiner, puisqu'il en couvrait");

    // Escape puts it back as it was, which is the whole point of a menu one can
    // open to look at.
    Send (&S, Key (OkKeySpace, 0));
    Send (&S, Key (OkKeyUp, 0));
    Send (&S, Key (OkKeyEscape, 0));
    Expect (!ScreenMenuOpen (&S) && W[WPopup].nChoice == 3, "Échap referme sans rien changer");

    // While it is open nothing underneath answers: that is what modal means.
    Send (&S, Key (OkKeySpace, 0));
    Send (&S, Key (OkKeyTab, 0));
    Expect (ScreenMenuOpen (&S) && S.nFocus == WPopup,
            "Tab ne traverse pas un menu ouvert");
    Send (&S, Mouse (EventMouseDown, 400, 60));
    Expect (!ScreenMenuOpen (&S), "un clic à côté le referme");
}

static void CheckMouse (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, &s_Theme, W, WCount);

    printf ("La souris\n");

    Send (&S, Mouse (EventMouseDown, 160, 50));         // Réglages
    Expect (Pressed (&S, WSettings), "enfoncé pendant le clic");
    Expect (FocusIs (&S, WSettings), "le clic prend le focus");

    TScreenReply r = Send (&S, Mouse (EventMouseUp, 160, 50));
    Expect (r.Result == ScreenActivated && r.nIndex == WSettings, "relâché dedans : actionné");
    Expect (!Pressed (&S, WSettings), "et plus enfoncé");

    Send (&S, Mouse (EventMouseDown, 160, 50));
    Send (&S, Mouse (EventMouseMove, 160, 200));
    Expect (!Pressed (&S, WSettings), "sortir du contrôle le relâche visuellement");
    Send (&S, Mouse (EventMouseMove, 160, 50));
    Expect (Pressed (&S, WSettings), "y revenir le reprend");
    Send (&S, Mouse (EventMouseMove, 160, 200));
    r = Send (&S, Mouse (EventMouseUp, 160, 200));
    Expect (r.Result != ScreenActivated, "relâché dehors : rien de fait");

    r = Send (&S, Mouse (EventMouseDown, 300, 50));     // le bouton inactif
    Expect (r.Result == ScreenIdle && !Pressed (&S, WOff), "un contrôle inactif ne prend rien");

    const int nRow = (int) s_Theme.M.nRowHeight;
    Send (&S, Mouse (EventMouseDown, 100, 121 + nRow + nRow / 2));      // la troisième
    Send (&S, Mouse (EventMouseUp,   100, 121 + nRow + nRow / 2));
    Expect (W[WList].nChoice == 1, "cliquer une ligne la sélectionne");
    Expect (FocusIs (&S, WList), "et donne le focus à la liste");

    r = Send (&S, Mouse (EventMouseDown, 5, 5));        // le fond
    Expect (r.Result == ScreenIdle && FocusIs (&S, WList),
            "cliquer à côté ne perd pas le focus");
}

int main (void)
{
    CheckTraversal ();
    CheckOperating ();
    CheckList ();
    CheckScroller ();
    CheckField ();
    CheckMenu ();
    CheckMouse ();

    printf ("\n%u écart(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
