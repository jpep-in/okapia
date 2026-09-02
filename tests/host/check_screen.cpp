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
//   8 list frame
//   9,10,11 its rows
enum { WLabel, WStart, WSettings, WOff, WCheck, WRadioA, WRadioB, WOther, WList,
       WRow0, WRow1, WRow2, WCount };

static void Build (TWidget *pW)
{
    for (unsigned i = 0; i < WCount; i++)
    {
        pW[i].Type   = WidgetLabel;
        pW[i].Rect   = Rect (0, 0, 0, 0);
        pW[i].pText  = 0;
        pW[i].nState = StateNormal;
        pW[i].nValue = 0;
        pW[i].nSpan  = 0;
        pW[i].pIcon  = 0;
        pW[i].Paint  = 0;
        pW[i].nGroup = 0;
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

    pW[WList]    .Type = WidgetListFrame;
    pW[WList]    .Rect = Rect (10, 120, 320, 60);
    for (unsigned i = 0; i < 3; i++)
    {
        pW[WRow0 + i].Type = WidgetListRow;
        pW[WRow0 + i].Rect = Rect (11, 121 + (int) (i * 19), 318, 19);
    }
}

static TEvent Key (unsigned nKey, unsigned nModifiers)
{
    TEvent e = { EventKeyDown, nKey, nModifiers, 0, 0 };
    return e;
}

static TEvent Mouse (TEventType Type, int nX, int nY)
{
    TEvent e = { Type, OkKeyNone, 0, nX, nY };
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
    ScreenInit (&S, W, WCount);

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
        ScreenInit (&T, V, WCount);
        Expect (T.nFocus == WCheck, "un écran qui déclare son focus est suivi");
        Send (&T, Key (OkKeyTab, 0));
        Expect (FocusIs (&T, WRadioA), "et Tab repart de là");
    }

    // All the way round: the wrap is where an off-by-one hides.
    for (unsigned i = 0; i < 6; i++)
    {
        Send (&S, Key (OkKeyTab, 0));
    }
    Expect (FocusIs (&S, WStart), "Tab fait le tour et revient au premier");
}

static void CheckOperating (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, W, WCount);

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
    ScreenInit (&S, W, WCount);

    printf ("Les flèches dans la liste\n");

    Send (&S, Key (OkKeyDown, 0));
    Expect (ScreenSelectedRow (&S, WList) < 0,
            "une flèche hors d'une liste ne fait rien");

    // Bounded, so that a loop that can no longer reach the list fails here
    // instead of hanging the build.
    for (unsigned i = 0; i < WCount && S.nFocus != WList; i++)
    {
        Send (&S, Key (OkKeyTab, 0));
    }
    Expect (FocusIs (&S, WList), "le focus se pose sur la liste, pas sur ses lignes");

    Send (&S, Key (OkKeyDown, 0));
    Expect (ScreenSelectedRow (&S, WList) == WRow0, "Bas choisit la première ligne");
    Send (&S, Key (OkKeyDown, 0));
    Expect (ScreenSelectedRow (&S, WList) == WRow1, "Bas descend");

    Send (&S, Key (OkKeyDown, 0));
    TScreenReply r = Send (&S, Key (OkKeyDown, 0));
    Expect (ScreenSelectedRow (&S, WList) == WRow2 && r.Result == ScreenIdle,
            "la liste s'arrête en bas au lieu de reboucler");

    Send (&S, Key (OkKeyUp, 0));
    Expect (ScreenSelectedRow (&S, WList) == WRow1, "Haut remonte");

    // One selection, and only one: two highlighted rows is the failure this
    // catches, and it looks like a redraw artefact rather than a state bug.
    unsigned nSelected = 0;
    for (unsigned i = 0; i < WCount; i++)
    {
        if (W[i].nState & StateSelected)
        {
            nSelected++;
        }
    }
    Expect (nSelected == 1, "une seule ligne sélectionnée à la fois");
}

static void CheckMouse (void)
{
    TWidget W[WCount];
    TScreen S;
    Build (W);
    ScreenInit (&S, W, WCount);

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

    Send (&S, Mouse (EventMouseDown, 100, 145));        // la deuxième ligne
    Send (&S, Mouse (EventMouseUp,   100, 145));
    Expect (ScreenSelectedRow (&S, WList) == WRow1, "cliquer une ligne la sélectionne");
    Expect (FocusIs (&S, WList), "et donne le focus à la liste, pas à la ligne");

    r = Send (&S, Mouse (EventMouseDown, 5, 5));        // le fond
    Expect (r.Result == ScreenIdle && FocusIs (&S, WList),
            "cliquer à côté ne perd pas le focus");
}

int main (void)
{
    CheckTraversal ();
    CheckOperating ();
    CheckList ();
    CheckMouse ();

    printf ("\n%u écart(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
