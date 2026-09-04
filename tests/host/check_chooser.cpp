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
    return ChooserOperate (&s_Model, nIndex, 0);
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

// The columns, named as the chooser names them.
enum { ColStartup = 1, ColReadOnly = 2, ColMounted = 3 };

// One of the list's marks, the way the loop reports a click on it.
static TChooserAction Cell (unsigned nColumn)
{
    return ChooserOperate (&s_Model, FindList (), nColumn);
}

// The rows as the screen now holds them: each carries its own three answers,
// which is the change — they used to be three controls that spoke about
// whichever row happened to be selected.
static const TListItem *s_Items (void)
{
    TWidget *pW = 0;
    ChooserWidgets (&pW);
    return pW[FindList ()].pItems;
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

    // Les trois réponses sont dans la ligne, une colonne chacune : les lire sous
    // la liste demandait de retenir de quelle ligne elles parlaient.
    TWidget *pW = 0;
    ChooserWidgets (&pW);
    const TWidget &List = pW[FindList ()];
    Expect (List.nColumns == 3, "la liste porte trois colonnes");
    Expect (List.pColumns != 0
            && strcmp (List.pColumns[0].pHeader, Str (StrStartupDisk)) == 0
            && strcmp (List.pColumns[1].pHeader, Str (StrReadOnly)) == 0
            && strcmp (List.pColumns[2].pHeader, Str (StrMounted)) == 0,
            "et chacune son en-tête");
    Expect (List.pColumns[0].bRadio && !List.pColumns[1].bRadio,
            "le démarrage est un choix unique, la lecture seule une bascule");

    Select (1);
    Expect (Cell (ColStartup) == ChooserNothing, "cocher « disque de démarrage » ne quitte pas");
    Expect (s_Model.nStartup == 1, "et le volume de démarrage a changé");
    n = Lines (Out);
    Expect (strcmp (Out[0], "/machd76.image") == 0, "le nouveau vient en premier");
    Expect (strcmp (Out[1], "/boot71.img") == 0, "l'ancien garde sa place dans le reste");

    // Un volume sans Système ne peut pas démarrer, et sa case le dit — sur sa
    // propre ligne, donc sans qu'il faille l'avoir sélectionné pour le voir.
    Expect ((s_Items ()[4].nCell[ColStartup - 1] & StateDisabled) != 0,
            "un volume sans Système ne peut pas être le disque de démarrage");

    // Démonter en retire le volume des lignes écrites.
    Select (0);
    Cell (ColMounted);
    Expect (!s_Model.Volumes[0].bMounted, "on peut démonter un volume");
    n = Lines (Out);
    Expect (n == 3, "et il quitte les lignes écrites");

    // Le volume de démarrage, lui, ne peut pas être démonté sous ses propres pieds.
    Expect ((s_Items ()[1].nCell[ColMounted - 1] & StateDisabled) != 0,
            "le volume de démarrage ne peut pas être démonté");

    // Lecture seule : le geste de sûreté du projet, un caractère dans une ligne.
    Select (3);
    Cell (ColMounted);                  // il était démonté
    Cell (ColReadOnly);
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
    Cell (ColMounted);                  // démonte le volume de démarrage… refusé
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

    // Le clavier traverse les colonnes : sans cela, une liste de cases serait
    // une chose que seule la souris actionne, alors que les trois contrôles
    // qu'elle remplace étaient atteignables par Tab.
    Open ();
    {
        TWidget *pL = 0;
        ChooserWidgets (&pL);
        const int nList = FindList ();
        s_Screen.nFocus = nList;
        Expect (pL[nList].nCell == 0, "le clavier part du nom");
        Send (Key (OkKeyRight, 0));
        Expect (pL[nList].nCell == ColStartup, "droite entre dans la première colonne");
        Send (Key (OkKeyRight, 0));
        Send (Key (OkKeyRight, 0));
        Expect (pL[nList].nCell == ColMounted, "et va jusqu'à la dernière");
        Send (Key (OkKeyRight, 0));
        Expect (pL[nList].nCell == ColMounted, "sans sortir par la droite");
        Send (Key (OkKeyLeft, 0));
        Expect (pL[nList].nCell == ColReadOnly, "gauche revient");

        // Espace actionne la case où le clavier se trouve, et la réponse dit
        // laquelle — c'est ce que la boucle passe au sélecteur.
        pL[nList].nChoice = 2;
        pL[nList].nCell   = ColReadOnly;
        const TScreenReply R = Send (Key (OkKeySpace, 0));
        Expect (R.Result == ScreenActivated && R.nIndex == nList && R.nCell == ColReadOnly,
                "espace actionne la case sous le clavier");

        // Une case grisée ne s'actionne pas plus au clavier qu'à la souris.
        pL[nList].nChoice = 4;           // le volume sans Système
        pL[nList].nCell   = ColStartup;
        Expect (Send (Key (OkKeySpace, 0)).Result == ScreenIdle,
                "et une case inactive ne répond pas");
    }

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

    // The four glyph buttons of the footer, each answering for itself. The
    // information one lives here and not behind the settings: a machine that
    // will not start is not a machine whose owner wants to go two clicks deep
    // to find out why.
    {
        TWidget *pW = 0;
        const unsigned n = ChooserWidgets (&pW);
        unsigned nIcons = 0;
        unsigned nSeen = 0;
        for (unsigned i = 0; i < n; i++)
        {
            if (pW[i].Type != WidgetIconButton) continue;
            nIcons++;
            switch (Operate ((int) i))
            {
            case ChooserSettings:    nSeen |= 1; break;
            case ChooserInformation: nSeen |= 2; break;
            case ChooserForgetPram:  nSeen |= 4; break;
            case ChooserShutDown:    nSeen |= 8; break;
            default: break;
            }
        }
        Expect (nIcons == 4, "le pied de page porte quatre marques");
        Expect (nSeen == 15, "réglages, informations, PRAM et arrêt, chacune la sienne");
    }

    printf ("\n%u écart(s)\n", s_nFailures);
    free (s_pPixels);
    return s_nFailures == 0 ? 0 : 1;
}
