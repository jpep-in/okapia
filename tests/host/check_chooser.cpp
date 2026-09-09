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
enum { ColStartup = 1, ColMounted = 2 };

// One of the list's marks, the way the loop reports a click on it.
static TChooserAction Cell (unsigned nColumn)
{
    return ChooserOperate (&s_Model, FindList (), nColumn);
}

// The rows as the screen now holds them: each carries its own two answers,
// which is the change — they used to be three controls that spoke about
// whichever row happened to be selected.
// The list's first row is the command that makes a volume, so the rows and the
// volumes are one apart. Everything below counts in volumes, and the two
// accessors are where the one is added — the same discipline the chooser keeps
// with VolumeOf/RowOf, kept here rather than sprinkled through the checks.
static const TListItem *s_ActionRow (void)
{
    TWidget *pW = 0;
    ChooserWidgets (&pW);
    return &pW[FindList ()].pItems[0];
}

static const TListItem *s_Items (void)
{
    return s_ActionRow () + 1;
}

static void Select (int nVolume)
{
    TWidget *pW = 0;
    ChooserWidgets (&pW);
    pW[FindList ()].nChoice = nVolume + 1;
    ChooserSync (&s_Model);
}

// Putting the selection on the command row, which is not a volume.
static void SelectAction (void)
{
    TWidget *pW = 0;
    ChooserWidgets (&pW);
    pW[FindList ()].nChoice = 0;
    ChooserSync (&s_Model);
}

static unsigned Lines (char Out[][CHOOSER_LINE])
{
    return ChooserDiskLines (&s_Model, Out, CHOOSER_MAX);
}

static unsigned EngineLines (char Out[][CHOOSER_LINE])
{
    return ChooserEngineLines (&s_Model, Out, CHOOSER_MAX);
}

static unsigned CdromLines (char Out[][CHOOSER_LINE])
{
    return ChooserCdromLines (&s_Model, Out, CHOOSER_MAX);
}

// A popup, found the way the list is — by what it is and not by where it sits,
// so a component added above it does not move the test. There are two of them
// now, and they are told apart by how many answers they offer rather than by
// their order: an order is exactly the kind of thing a layout change moves.
static int FindPopupOf (unsigned nItems)
{
    TWidget *pW = 0;
    const unsigned n = ChooserWidgets (&pW);
    for (unsigned i = 0; i < n; i++)
    {
        if (pW[i].Type == WidgetPopup && pW[i].nItems == nItems)
        {
            return (int) i;
        }
    }
    return -1;
}

static int FindPopup (void)          { return FindPopupOf (2); }   // the emulator
static int FindMountPopup (void)     { return FindPopupOf (3); }   // how it mounts

static TWidget *Popup (void)
{
    TWidget *pW = 0;
    ChooserWidgets (&pW);
    return &pW[FindPopup ()];
}

static TWidget *MountPopup (void)
{
    TWidget *pW = 0;
    ChooserWidgets (&pW);
    return &pW[FindMountPopup ()];
}

// Picking an answer in the mount popup, the way the loop reports it.
static TChooserAction MountPick (TChooserMount Mount)
{
    MountPopup ()->nChoice = (int) Mount;
    return ChooserOperate (&s_Model, FindMountPopup (), 0);
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
    Expect (s_Model.nCount == 7, "sept volumes sur la carte");
    Expect (ChooserSelected () == 0, "la sélection part sur le volume de démarrage");

    // The processor sits with the version, not among the dotted states: the
    // labels come from Str() so this measures the row and not one language.
    Expect (strstr (s_Items ()[0].pText, Str (StrCpu68k)) != 0,
            "un volume 7.1.2 annonce son processeur");
    Expect (strstr (s_Items ()[1].pText, Str (StrCpuUniversal)) != 0,
            "un 7.6 universel le dit, puisque là il y aura un choix");
    Expect (strstr (s_Items ()[5].pText, Str (StrCpuPowerpc)) != 0,
            "et un 8.6 annonce PowerPC");
    Expect (strstr (s_Items ()[4].pText, Str (StrCpu68k)) == 0
            && strstr (s_Items ()[4].pText, Str (StrCpuPowerpc)) == 0,
            "un volume sans Système lisible n'invente pas de processeur");

    unsigned n = Lines (Out);
    Expect (n == 5, "cinq volumes montés en disque sur sept");
    Expect (strcmp (Out[0], "/boot71.img") == 0, "le volume de démarrage vient en premier");
    Expect (strcmp (Out[2], "*/os81.img") == 0, "un volume en lecture seule porte son étoile");

    // Les deux réponses qui sont des oui-ou-non sont dans la ligne, une colonne
    // chacune : les lire sous la liste demandait de retenir de quelle ligne
    // elles parlaient. La troisième n'en est pas une et n'est pas là.
    TWidget *pW = 0;
    ChooserWidgets (&pW);
    const TWidget &List = pW[FindList ()];
    Expect (List.nColumns == 2, "la liste porte deux colonnes");
    Expect (List.pColumns != 0
            && strcmp (List.pColumns[0].pHeader, Str (StrStartupDisk)) == 0
            && strcmp (List.pColumns[1].pHeader, Str (StrMounted)) == 0,
            "et chacune son en-tête");
    Expect (List.pColumns[0].bRadio && !List.pColumns[1].bRadio,
            "le démarrage est un choix unique, le montage une bascule");

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
    Expect (n == 4, "et il quitte les lignes écrites");

    // Le volume de démarrage, lui, ne peut pas être démonté sous ses propres pieds.
    Expect ((s_Items ()[1].nCell[ColMounted - 1] & StateDisabled) != 0,
            "le volume de démarrage ne peut pas être démonté");

    // Lecture seule : le geste de sûreté du projet, un caractère dans une ligne.
    Select (3);
    Cell (ColMounted);                  // il était démonté
    MountPick (MountHDReadOnly);
    Expect (s_Model.Volumes[3].Mount == MountHDReadOnly,
            "on peut passer un volume en lecture seule");
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

    // Le clavier part sur la liste, pas sur la première chose qui traîne : Retour
    // actionne ce que le clavier tient, et posé sur le pied de page il ouvrait
    // les réglages depuis l'écran principal.
    Open ();
    Expect (s_Screen.nFocus == Find (WidgetButton, Str (StrStart)),
            "le clavier part sur Démarrer");

    // Et il se voit quand il arrive sur Démarrer. ChooserSync réécrivait l'état
    // entier du bouton, et la boucle le fait rejouer à chaque changement : le
    // focus était effacé avant d'avoir été dessiné, si bien qu'aucune touche ne
    // pouvait le faire apparaître sur le bouton principal.
    {
        TWidget *pW2 = 0;
        const unsigned nAll = ChooserWidgets (&pW2);
        const int nStart = Find (WidgetButton, Str (StrStart));
        for (unsigned i = 0; i < nAll && s_Screen.nFocus != nStart; i++)
        {
            Send (Key (OkKeyTab, 0));
        }
        Expect (s_Screen.nFocus == nStart, "le clavier tient Démarrer");
        Expect ((pW2[nStart].nState & StateFocused) != 0,
                "et il porte l'anneau de focus");
        // Ce qui compte est qu'il y survive : c'est le rejeu qui l'effaçait.
        ChooserSync (&s_Model);
        Expect ((pW2[nStart].nState & StateFocused) != 0,
                "que le modèle soit rejoué ne le lui reprend pas");
        Expect ((pW2[nStart].nState & StateDefault) != 0,
                "et il reste le bouton par défaut");
    }
    Open ();

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
        Expect (pL[nList].nCell == ColMounted, "et va jusqu'à la dernière");
        Send (Key (OkKeyRight, 0));
        Expect (pL[nList].nCell == ColMounted, "sans sortir par la droite");
        Send (Key (OkKeyLeft, 0));
        Expect (pL[nList].nCell == ColStartup, "gauche revient");

        // Espace actionne la case où le clavier se trouve, et la réponse dit
        // laquelle — c'est ce que la boucle passe au sélecteur.
        pL[nList].nChoice = 2;
        pL[nList].nCell   = ColMounted;
        const TScreenReply R = Send (Key (OkKeySpace, 0));
        Expect (R.Result == ScreenActivated && R.nIndex == nList && R.nCell == ColMounted,
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

    // Which emulator starts a volume. The whole point is that the question is
    // only asked where there is one: a System built for one processor settles
    // it, and the popup then says which without offering a choice.
    printf ("\nle moteur d'émulation\n");
    Select (0);
    Expect ((Popup ()->nState & StateDisabled) != 0,
            "un Système 68k ne pose pas la question");
    Expect (Popup ()->nChoice == 0, "et il annonce quand même lequel démarrera");
    Select (5);
    Expect ((Popup ()->nState & StateDisabled) != 0,
            "un Système PowerPC non plus");
    Expect (Popup ()->nChoice == 1, "et celui-là annonce PowerPC");
    Select (1);
    Expect ((Popup ()->nState & StateDisabled) == 0,
            "un Système universel, si : c'est là qu'il y a un choix");

    n = EngineLines (Out);
    Expect (n == 2, "deux volumes universels, deux lignes écrites");
    Expect (strcmp (Out[0], "/machd76.image 68k") == 0,
            "et rien n'est écrit pour ceux qui n'ont pas le choix");

    Popup ()->nChoice = 1;
    Expect (ChooserOperate (&s_Model, FindPopup (), 0) == ChooserNothing,
            "changer de moteur ne quitte pas l'écran");
    Expect (s_Model.Volumes[1].Engine == CPUPowerPC, "le choix est retenu");
    n = EngineLines (Out);
    Expect (strcmp (Out[0], "/machd76.image powerpc") == 0,
            "et il part sur la carte");
    Expect (ChooserStartupEngine (&s_Model) == CPU68k,
            "le volume de démarrage est un autre, donc rien ne change pour le noyau");

    // And the case the loader exists for: a startup volume that asks for the
    // engine this image does not carry.
    s_Model.nStartup = 1;
    ChooserSync (&s_Model);
    Expect (ChooserStartupEngine (&s_Model) == CPUPowerPC,
            "le disque de démarrage réclame l'autre moteur");
    s_Model.nStartup = 0;

    // A choice made on a universal volume must not follow the selection onto a
    // volume that has no choice — the popup speaks about the selected row.
    Select (5);
    Expect (Popup ()->nChoice == 1 && (Popup ()->nState & StateDisabled) != 0,
            "et la sélection suivante reprend la main sur ce qu'il affiche");
    Select (0);
    Expect (Popup ()->nChoice == 0, "chaque volume garde le sien");

    // The row that makes a volume. It is in the list because what it makes
    // lands in the list, and first because that place never moves while the
    // volumes under it come and go.
    printf ("\nla ligne qui crée un volume\n");
    Open ();
    {
        TWidget *pW2 = 0;
        ChooserWidgets (&pW2);
        const TWidget &L = pW2[FindList ()];
        Expect (L.nItems == s_Model.nCount + 1,
                "la liste porte une ligne de plus qu'il n'y a de volumes");
        Expect (s_ActionRow ()->bAction, "et la première est une commande");
        Expect (strcmp (s_ActionRow ()->pText, Str (StrNewVolume)) == 0,
                "qui dit ce qu'elle fait");
        Expect (!s_Items ()[0].bAction, "les suivantes sont des volumes");
    }

    SelectAction ();
    Expect (ChooserSelected () == -1, "sélectionnée, elle n'est aucun volume");
    Expect ((MountPopup ()->nState & StateDisabled) != 0
            && (Popup ()->nState & StateDisabled) != 0,
            "et les deux popups n'ont plus de volume à décrire");
    Expect (ChooserOperate (&s_Model, FindList (), 0) == ChooserNewVolume,
            "l'actionner demande un nouveau volume");

    // Au clavier : espace l'actionne, et les flèches n'entrent pas dans une
    // colonne qu'elle n'a pas.
    {
        TWidget *pL = 0;
        ChooserWidgets (&pL);
        const int nList = FindList ();
        s_Screen.nFocus = nList;
        pL[nList].nChoice = 0;
        pL[nList].nCell   = 0;
        Send (Key (OkKeyRight, 0));
        Expect (pL[nList].nCell == 0, "les flèches n'entrent pas dans ses colonnes");
        const TScreenReply R = Send (Key (OkKeySpace, 0));
        Expect (R.Result == ScreenActivated && R.nCell == 0,
                "et espace l'actionne comme un bouton");
    }

    // Une carte vide n'a que cette ligne, et la sélection s'y pose.
    {
        TChooser Empty;
        memset (&Empty, 0, sizeof Empty);
        Empty.nStartup = -1;
        Empty.Built    = CPU68k;
        ChooserDraw (&s_Surface, &Empty);
        TWidget *pW2 = 0;
        ChooserWidgets (&pW2);
        Expect (pW2[FindList ()].nItems == 1, "une carte vide ne porte que la commande");
        Expect (pW2[FindList ()].nChoice == 0, "et la sélection s'y pose");
        Expect (ChooserOperate (&Empty, FindList (), 0) == ChooserNewVolume,
                "donc une carte vide propose d'en faire un");
    }
    Open ();

    // How a volume is handed to the Macintosh. Nothing in an image says what it
    // is — find_hfs_partition() reads a flat one and a partitioned one either
    // way — so this is a choice, and the three answers are the whole vocabulary
    // the preferences have for one volume.
    printf ("\nmonter comme\n");
    Open ();
    Select (6);
    Expect (MountPopup ()->nChoice == (int) MountCD,
            "le popup annonce ce que le volume sélectionné est");
    Expect (strstr (s_Items ()[6].pText, Str (StrMountCd)) != 0,
            "et la ligne le dit sans qu'on la sélectionne");

    n = Lines (Out);
    for (unsigned i = 0; i < n; i++)
    {
        Expect (strcmp (Out[i], "/installppc86fr.toast") != 0,
                "un CD n'est pas un disque");
    }
    n = CdromLines (Out);
    Expect (n == 1 && strcmp (Out[0], "/installppc86fr.toast") == 0,
            "il part sur sa propre ligne, sans étoile");

    // Un CD amorçable peut démarrer la machine, et ce n'est pas l'ordre des
    // lignes qui le dit : la ROM reçoit un *pilote* par la PRAM.
    Expect ((s_Items ()[6].nCell[ColStartup - 1] & StateDisabled) == 0,
            "un CD amorçable peut être le disque de démarrage");
    Expect (ChooserBootDriver (&s_Model) == 0,
            "tant qu'il ne l'est pas, aucun pilote n'est imposé");
    Cell (ColStartup);
    Expect (s_Model.nStartup == 6, "cocher sa marque le choisit");
    Expect (ChooserBootDriver (&s_Model) == CHOOSER_BOOT_CDROM,
            "et la machine reçoit CDROMRefNum");
    n = CdromLines (Out);
    Expect (n == 1 && strcmp (Out[0], "/installppc86fr.toast") == 0,
            "le disque de démarrage vient en tête de sa propre liste");
    n = Lines (Out);
    Expect (n == 5, "et il ne s'invite pas dans les disques");
    // Repasser sur un disque doit remettre 0 : une carte qui a démarré d'un
    // disque une fois réclamerait le pilote CD pour toujours.
    Select (0);
    Cell (ColStartup);
    Expect (ChooserBootDriver (&s_Model) == 0, "revenir à un disque lève le pilote");
    Select (6);
    Cell (ColStartup);

    // Le passage d'une liste à l'autre, dans les deux sens, sans que le volume
    // se retrouve dans les deux.
    Open ();
    Select (4);
    Expect (MountPick (MountCD) == ChooserNothing, "changer de montage ne quitte pas l'écran");
    n = CdromLines (Out);
    Expect (n == 2, "un volume passé en CD rejoint l'autre liste");
    n = Lines (Out);
    for (unsigned i = 0; i < n; i++)
    {
        Expect (strcmp (Out[i], "/travaux.img") != 0, "et quitte la première");
    }
    MountPick (MountHD);
    n = CdromLines (Out);
    Expect (n == 1, "et le retour marche aussi");

    // L'image de la ligne suit le montage, dans les deux sens. Elle était posée
    // une fois pour toutes à la mise en page, qui ne se refait pas quand on
    // change un menu : un disque restait dessiné en dossier, et un dossier en
    // disque, jusqu'au prochain écran.
    Select (5);
    const TGlyphImage *pWasFolder = s_Items ()[5].pIcon;
    MountPick (MountCD);
    Expect (s_Items ()[5].pIcon != pWasFolder,
            "passer un volume en CD change son image");
    Expect (s_Items ()[5].pIcon == s_Items ()[6].pIcon,
            "et c'est la même que celle du CD déjà là");
    MountPick (MountHD);
    Expect (s_Items ()[5].pIcon == pWasFolder, "revenir en disque la rend");

    // Faire un CD du volume de démarrage ne le lui retire pas : il reste le
    // volume de démarrage, et c'est le pilote annoncé qui change.
    Select (0);
    MountPick (MountCD);
    Expect (s_Model.nStartup == 0, "le volume de démarrage devenu CD le reste");
    Expect (ChooserBootDriver (&s_Model) == CHOOSER_BOOT_CDROM,
            "et la machine démarrera par le pilote CD");
    Open ();

    // The page has to fit in every language it speaks, at the smallest surface
    // it is asked to draw on. The emulator row was added against English and
    // French is longer nearly everywhere; measuring both is the whole reason
    // the translations came before the screens.
    {
        const TLanguage Was = StringsLanguage ();
        for (unsigned i = 0; i < LanguageCount; i++)
        {
            StringsSetLanguage ((TLanguage) i);
            ChooserDraw (&s_Surface, &s_Model);
            char What[64];
            snprintf (What, sizeof What, "la page tient en 640x480 en %s",
                      StringsCode ((TLanguage) i));
            Expect (!ChooserOverflowed (), What);
        }
        StringsSetLanguage (Was);
        Open ();
    }

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
