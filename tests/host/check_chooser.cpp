/*
 * check_chooser.cpp — drive the boot chooser with no card and no screen.
 *
 * The chooser is handed a list of volumes and answers what the user asked for,
 * so a made-up card is enough to check it — and a made-up one can hold the
 * awkward cases a real card rarely does all at once: a volume with no System,
 * one left in use, one that is not mounted.
 *
 * What is checked here is the part that will be written to the user's card: the
 * order of the disk lines and the star in front of a read-only one. Getting
 * that wrong is a Macintosh that starts from the wrong System, or a volume
 * mounted for writing that was meant to be safe.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "okapia_chooser.h"
#include "okapia_screen.h"
#include "okapia_strings.h"

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s %s\n", bOK ? "ok  " : "ECHEC", pWhat);
    if (!bOK)
    {
        s_nFailures++;
    }
}

void ChooserSample (TChooser *p);        // from render_chooser.cpp

static TTheme  s_Theme;
static unsigned *s_pPixels;
static TSurface  s_Surface;
static TScreen   s_Screen;
static TChooser  s_Model;

// The screen laid out as the firmware lays it out, so the components are the
// real ones and the indices below are whatever the layout decided.
static void Open (void)
{
    ChooserSample (&s_Model);
    ChooserDraw (&s_Surface, &s_Model);
    TWidget *pW = 0;
    const unsigned n = ChooserWidgets (&pW);
    ScreenInit (&s_Screen, &s_Theme, pW, n);
}

static TEvent Key (unsigned nKey, unsigned nModifiers)
{
    TEvent e = { EventKeyDown, nKey, 0, nModifiers, 0, 0 };
    return e;
}

static TScreenReply Send (const TEvent &rEvent)
{
    return ScreenEvent (&s_Screen, &rEvent);
}

// Operating a control the way the firmware does: the loop reports it, the
// chooser is asked what it meant.
static TChooserAction Operate (int nIndex)
{
    return ChooserOperate (&s_Model, nIndex);
}

// The component of this type carrying this label, or -1.
static int Find (TWidgetType Type, const char *pText)
{
    TWidget *pW = 0;
    const unsigned n = ChooserWidgets (&pW);
    for (unsigned i = 0; i < n; i++)
    {
        if (pW[i].Type == Type && pW[i].pText != 0 && strcmp (pW[i].pText, pText) == 0)
        {
            return (int) i;
        }
    }
    return -1;
}

static int FindList (void)
{
    TWidget *pW = 0;
    const unsigned n = ChooserWidgets (&pW);
    for (unsigned i = 0; i < n; i++)
    {
        if (pW[i].Type == WidgetList)
        {
            return (int) i;
        }
    }
    return -1;
}

static void Select (int nItem)
{
    TWidget *pW = 0;
    ChooserWidgets (&pW);
    pW[FindList ()].nChoice = nItem;
    ChooserSync (&s_Model);
}

static unsigned Lines (char Out[][CHOOSER_LINE])
{
    return ChooserDiskLines (&s_Model, Out, CHOOSER_MAX);
}

int main (void)
{
    ThemeMake (16, &s_Theme);
    s_pPixels = (unsigned *) calloc (640 * 480, sizeof (unsigned));
    if (s_pPixels == 0)
    {
        return 1;
    }
    s_Surface.pPixels = (unsigned char *) s_pPixels;
    s_Surface.nWidth  = 640;
    s_Surface.nHeight = 480;
    s_Surface.nPitch  = 640 * (unsigned) sizeof (unsigned);

    char Out[CHOOSER_MAX][CHOOSER_LINE];

    printf ("Le sélecteur\n");
    Open ();
    Expect (s_Model.nCount == 5, "cinq volumes sur la carte");
    Expect (ChooserSelected () == 0, "la sélection part sur le volume de démarrage");

    unsigned n = Lines (Out);
    Expect (n == 4, "quatre volumes montés sur cinq");
    Expect (strcmp (Out[0], "/boot71.img") == 0, "le volume de démarrage vient en premier");
    Expect (strcmp (Out[2], "*/os81.img") == 0, "un volume en lecture seule porte son étoile");

    // Changer de volume de démarrage.
    const int nStartup = Find (WidgetCheckbox, Str (StrStartupDisk));
    const int nReadOnly = Find (WidgetCheckbox, Str (StrReadOnly));
    const int nMounted = Find (WidgetCheckbox, Str (StrMounted));
    Expect (nStartup >= 0 && nReadOnly >= 0 && nMounted >= 0,
            "les trois cases sont sur l'écran");

    Select (1);
    Expect (Operate (nStartup) == ChooserNothing, "cocher « disque de démarrage » ne quitte pas");
    Expect (s_Model.nStartup == 1, "et le volume de démarrage a changé");
    n = Lines (Out);
    Expect (strcmp (Out[0], "/machd76.image") == 0, "le nouveau vient en premier");
    Expect (strcmp (Out[1], "/boot71.img") == 0, "l'ancien garde sa place dans le reste");

    // Un volume sans Système ne peut pas démarrer, et la case le dit.
    Select (4);
    TWidget *pW = 0;
    ChooserWidgets (&pW);
    Expect ((pW[nStartup].nState & StateDisabled) != 0,
            "un volume sans Système ne peut pas être le disque de démarrage");

    // Démonter en retire le volume des lignes écrites.
    Select (0);
    Operate (nMounted);
    Expect (!s_Model.Volumes[0].bMounted, "on peut démonter un volume");
    n = Lines (Out);
    Expect (n == 3, "et il quitte les lignes écrites");

    // Le volume de démarrage, lui, ne peut pas être démonté sous ses propres pieds.
    Select (1);
    ChooserWidgets (&pW);
    Expect ((pW[nMounted].nState & StateDisabled) != 0,
            "le volume de démarrage ne peut pas être démonté");

    // Lecture seule : le geste de sûreté du projet, un caractère dans une ligne.
    Select (3);
    Operate (nMounted);                 // il était démonté
    Operate (nReadOnly);
    Expect (s_Model.Volumes[3].bReadOnly, "on peut passer un volume en lecture seule");
    n = Lines (Out);
    bool bStarred = false;
    for (unsigned i = 0; i < n; i++)
    {
        if (strcmp (Out[i], "*/boot608.hda") == 0)
        {
            bStarred = true;
        }
    }
    Expect (bStarred, "et son étoile part sur la carte");

    // Sans volume de démarrage, le bouton qui ne peut pas marcher le dit avant.
    Select (1);
    Operate (nMounted);                 // démonte le volume de démarrage… refusé
    s_Model.nStartup = -1;
    ChooserSync (&s_Model);
    ChooserWidgets (&pW);
    const int nStart = Find (WidgetButton, Str (StrStart));
    Expect ((pW[nStart].nState & StateDisabled) != 0,
            "sans rien pour démarrer, le bouton est inactif");

    // Les boutons du pied répondent ce qu'ils promettent.
    s_Model.nStartup = 1;
    ChooserSync (&s_Model);
    Expect (Operate (nStart) == ChooserStart, "Démarrer démarre");

    // Le clavier atteint tout, et Échap ne détruit rien.
    Open ();
    unsigned nFocusable = 0;
    ChooserWidgets (&pW);
    const unsigned nCount = ChooserWidgets (&pW);
    for (unsigned i = 0; i < nCount; i++)
    {
        if (WidgetFocusable (&pW[i]))
        {
            nFocusable++;
        }
    }
    Expect (nFocusable >= 6, "tout ce qui s'actionne est atteignable au clavier");
    const int nWas = s_Screen.nFocus;
    for (unsigned i = 0; i < nFocusable; i++)
    {
        Send (Key (OkKeyTab, 0));
    }
    Expect (s_Screen.nFocus == nWas, "Tab fait le tour");

    printf ("\n%u écart(s)\n", s_nFailures);
    free (s_pPixels);
    return s_nFailures == 0 ? 0 : 1;
}
