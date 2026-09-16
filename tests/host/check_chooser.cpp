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
 * Copyright (C) 2026  Jonathan Pepin
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
    printf ("  %s %s\n", bOK ? "ok  " : "FAIL ", pWhat);
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

    printf ("The chooser\n");
    Open ();
    Expect (s_Model.nCount == 7, "seven volumes on the card");
    Expect (ChooserSelected () == 0, "the selection starts on the startup volume");

    // The processor sits with the version, not among the dotted states: the
    // labels come from Str() so this measures the row and not one language.
    Expect (strstr (s_Items ()[0].pText, Str (StrCpu68k)) != 0,
            "a 7.1.2 volume states its processor");
    Expect (strstr (s_Items ()[1].pText, Str (StrCpuUniversal)) != 0,
            "a universal 7.6 says so, since there will be a choice");
    Expect (strstr (s_Items ()[5].pText, Str (StrCpuPowerpc)) != 0,
            "and an 8.6 states PowerPC");
    Expect (strstr (s_Items ()[4].pText, Str (StrCpu68k)) == 0
            && strstr (s_Items ()[4].pText, Str (StrCpuPowerpc)) == 0,
            "a volume with no readable System invents no processor");

    unsigned n = Lines (Out);
    Expect (n == 5, "five volumes of seven mounted as disks");
    Expect (strcmp (Out[0], "/boot71.img") == 0, "the startup volume comes first");
    Expect (strcmp (Out[2], "*/os81.img") == 0, "a read-only volume carries its star");

    // The two answers that are yes-or-no sit in the row, a column each: reading
    // them under the list meant remembering which row they spoke about. The
    // third is not one and is not there.
    TWidget *pW = 0;
    ChooserWidgets (&pW);
    const TWidget &List = pW[FindList ()];
    Expect (List.nColumns == 2, "the list carries two columns");
    Expect (List.pColumns != 0
            && strcmp (List.pColumns[0].pHeader, Str (StrStartupDisk)) == 0
            && strcmp (List.pColumns[1].pHeader, Str (StrMounted)) == 0,
            "each with its header");
    Expect (List.pColumns[0].bRadio && !List.pColumns[1].bRadio,
            "startup is a single choice, mounting a toggle");

    Select (1);
    Expect (Cell (ColStartup) == ChooserNothing, "ticking startup disk does not leave");
    Expect (s_Model.nStartup == 1, "and the startup volume has changed");
    n = Lines (Out);
    Expect (strcmp (Out[0], "/machd76.image") == 0, "the new one comes first");
    Expect (strcmp (Out[1], "/boot71.img") == 0, "the old one keeps its place among the rest");

    // A volume with no System cannot start, and its box says so — on its own
    // row, so without having to select it to see.
    Expect ((s_Items ()[4].nCell[ColStartup - 1] & StateDisabled) != 0,
            "a volume with no System cannot be the startup disk");

    // Unmounting takes the volume out of the lines written.
    Select (0);
    Cell (ColMounted);
    Expect (!s_Model.Volumes[0].bMounted, "a volume can be unmounted");
    n = Lines (Out);
    Expect (n == 4, "and it leaves the lines written");

    // The startup volume, though, cannot be unmounted from under itself.
    Expect ((s_Items ()[1].nCell[ColMounted - 1] & StateDisabled) != 0,
            "the startup volume cannot be unmounted");

    // Read-only: the project's safety gesture, one character in a line.
    Select (3);
    Cell (ColMounted);                  // it was unmounted
    MountPick (MountHDReadOnly);
    Expect (s_Model.Volumes[3].Mount == MountHDReadOnly,
            "a volume can be made read-only");
    n = Lines (Out);
    bool bStarred = false;
    for (unsigned i = 0; i < n; i++)
    {
        if (strcmp (Out[i], "*/boot608.hda") == 0)
        {
            bStarred = true;
        }
    }
    Expect (bStarred, "and its star goes onto the card");

    // With no startup volume, the button that cannot work says so beforehand.
    Select (1);
    Cell (ColMounted);                  // unmounts the startup volume… refused
    s_Model.nStartup = -1;
    ChooserSync (&s_Model);
    ChooserWidgets (&pW);
    const int nStart = Find (WidgetButton, Str (StrStart));
    Expect ((pW[nStart].nState & StateDisabled) != 0,
            "with nothing to start, the button is disabled");

    // The footer buttons answer what they promise.
    s_Model.nStartup = 1;
    ChooserSync (&s_Model);
    Expect (Operate (nStart) == ChooserStart, "Start starts");

    // The keyboard starts on a deliberate control, not on whatever lies first:
    // Return operates what the keyboard holds, and resting on the footer it
    // opened the settings from the main screen.
    Open ();
    Expect (s_Screen.nFocus == Find (WidgetButton, Str (StrStart)),
            "the keyboard starts on Start");

    // And it shows when it reaches Start. ChooserSync rewrote the button's whole
    // state, and the loop replays it at every change: the focus was erased
    // before it was drawn, so no key could make it appear on the main button.
    {
        TWidget *pW2 = 0;
        const unsigned nAll = ChooserWidgets (&pW2);
        const int nStart = Find (WidgetButton, Str (StrStart));
        for (unsigned i = 0; i < nAll && s_Screen.nFocus != nStart; i++)
        {
            Send (Key (OkKeyTab, 0));
        }
        Expect (s_Screen.nFocus == nStart, "the keyboard holds Start");
        Expect ((pW2[nStart].nState & StateFocused) != 0,
                "and it wears the focus ring");
        // What matters is that it survives: the replay is what erased it.
        ChooserSync (&s_Model);
        Expect ((pW2[nStart].nState & StateFocused) != 0,
                "replaying the model does not take it away");
        Expect ((pW2[nStart].nState & StateDefault) != 0,
                "and it stays the default button");
    }
    Open ();

    // The keyboard goes through the columns: otherwise a list of boxes would be
    // something only the mouse operates, while the three controls it replaces
    // were reachable with Tab.
    Open ();
    {
        TWidget *pL = 0;
        ChooserWidgets (&pL);
        const int nList = FindList ();
        s_Screen.nFocus = nList;
        Expect (pL[nList].nCell == 0, "the keyboard starts from the name");
        Send (Key (OkKeyRight, 0));
        Expect (pL[nList].nCell == ColStartup, "right enters the first column");
        Send (Key (OkKeyRight, 0));
        Expect (pL[nList].nCell == ColMounted, "and goes as far as the last");
        Send (Key (OkKeyRight, 0));
        Expect (pL[nList].nCell == ColMounted, "without leaving by the right");
        Send (Key (OkKeyLeft, 0));
        Expect (pL[nList].nCell == ColStartup, "left comes back");

        // Space operates the box the keyboard is on, and the reply says which —
        // that is what the loop hands the chooser.
        pL[nList].nChoice = 2;
        pL[nList].nCell   = ColMounted;
        const TScreenReply R = Send (Key (OkKeySpace, 0));
        Expect (R.Result == ScreenActivated && R.nIndex == nList && R.nCell == ColMounted,
                "space operates the box under the keyboard");

        // A greyed box no more operates from the keyboard than from the mouse.
        pL[nList].nChoice = 4;           // the volume with no System
        pL[nList].nCell   = ColStartup;
        Expect (Send (Key (OkKeySpace, 0)).Result == ScreenIdle,
                "and a disabled box does not answer");
    }

    // The keyboard reaches everything, and Escape destroys nothing.
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
    Expect (nFocusable >= 6, "everything operable is reachable by keyboard");
    const int nWas = s_Screen.nFocus;
    for (unsigned i = 0; i < nFocusable; i++)
    {
        Send (Key (OkKeyTab, 0));
    }
    Expect (s_Screen.nFocus == nWas, "Tab goes all the way round");

    // Which emulator starts a volume. The whole point is that the question is
    // only asked where there is one: a System built for one processor settles
    // it, and the popup then says which without offering a choice.
    printf ("\nthe emulation engine\n");
    Select (0);
    Expect ((Popup ()->nState & StateDisabled) != 0,
            "a 68k System does not ask the question");
    Expect (Popup ()->nChoice == 0, "and still says which will start");
    Select (5);
    Expect ((Popup ()->nState & StateDisabled) != 0,
            "nor does a PowerPC System");
    Expect (Popup ()->nChoice == 1, "and that one says PowerPC");
    Select (1);
    Expect ((Popup ()->nState & StateDisabled) == 0,
            "a universal System does: that is where there is a choice");

    n = EngineLines (Out);
    Expect (n == 2, "two universal volumes, two lines written");
    Expect (strcmp (Out[0], "/machd76.image 68k") == 0,
            "and nothing is written for those with no choice");

    Popup ()->nChoice = 1;
    Expect (ChooserOperate (&s_Model, FindPopup (), 0) == ChooserNothing,
            "changing the engine does not leave the screen");
    Expect (s_Model.Volumes[1].Engine == CPUPowerPC, "the choice is kept");
    n = EngineLines (Out);
    Expect (strcmp (Out[0], "/machd76.image powerpc") == 0,
            "and it goes onto the card");
    Expect (ChooserStartupEngine (&s_Model) == CPU68k,
            "the startup volume is another one, so nothing changes for the kernel");

    // And the case the loader exists for: a startup volume that asks for the
    // engine this image does not carry.
    s_Model.nStartup = 1;
    ChooserSync (&s_Model);
    Expect (ChooserStartupEngine (&s_Model) == CPUPowerPC,
            "the startup disk asks for the other engine");
    s_Model.nStartup = 0;

    // A choice made on a universal volume must not follow the selection onto a
    // volume that has no choice — the popup speaks about the selected row.
    Select (5);
    Expect (Popup ()->nChoice == 1 && (Popup ()->nState & StateDisabled) != 0,
            "and the next selection takes back what it shows");
    Select (0);
    Expect (Popup ()->nChoice == 0, "each volume keeps its own");

    // The row that makes a volume. It is in the list because what it makes
    // lands in the list, and first because that place never moves while the
    // volumes under it come and go.
    printf ("\nthe row that makes a volume\n");
    Open ();
    {
        TWidget *pW2 = 0;
        ChooserWidgets (&pW2);
        const TWidget &L = pW2[FindList ()];
        Expect (L.nItems == s_Model.nCount + 1,
                "the list carries one row more than there are volumes");
        Expect (s_ActionRow ()->bAction, "and the first is a command");
        Expect (strcmp (s_ActionRow ()->pText, Str (StrNewVolume)) == 0,
                "that says what it does");
        Expect (!s_Items ()[0].bAction, "the next ones are volumes");
    }

    SelectAction ();
    Expect (ChooserSelected () == -1, "selected, it is no volume");
    Expect ((MountPopup ()->nState & StateDisabled) != 0
            && (Popup ()->nState & StateDisabled) != 0,
            "and both popups have no volume left to describe");
    Expect (ChooserOperate (&s_Model, FindList (), 0) == ChooserNewVolume,
            "operating it asks for a new volume");

    // With the keyboard: space operates it, and the arrows do not enter a
    // column it does not have.
    {
        TWidget *pL = 0;
        ChooserWidgets (&pL);
        const int nList = FindList ();
        s_Screen.nFocus = nList;
        pL[nList].nChoice = 0;
        pL[nList].nCell   = 0;
        Send (Key (OkKeyRight, 0));
        Expect (pL[nList].nCell == 0, "the arrows do not enter its columns");
        const TScreenReply R = Send (Key (OkKeySpace, 0));
        Expect (R.Result == ScreenActivated && R.nCell == 0,
                "and space operates it like a button");
    }

    // An empty card has only that row, and the selection lands on it.
    {
        TChooser Empty;
        memset (&Empty, 0, sizeof Empty);
        Empty.nStartup = -1;
        Empty.Built    = CPU68k;
        ChooserDraw (&s_Surface, &Empty);
        TWidget *pW2 = 0;
        ChooserWidgets (&pW2);
        Expect (pW2[FindList ()].nItems == 1, "an empty card carries only the command");
        Expect (pW2[FindList ()].nChoice == 0, "and the selection lands on it");
        Expect (ChooserOperate (&Empty, FindList (), 0) == ChooserNewVolume,
                "so an empty card offers to make one");
    }
    Open ();

    // How a volume is handed to the Macintosh. Nothing in an image says what it
    // is — find_hfs_partition() reads a flat one and a partitioned one either
    // way — so this is a choice, and the three answers are the whole vocabulary
    // the preferences have for one volume.
    printf ("\nmount as\n");
    Open ();
    Select (6);
    Expect (MountPopup ()->nChoice == (int) MountCD,
            "the popup says what the selected volume is");
    Expect (strstr (s_Items ()[6].pText, Str (StrMountCd)) != 0,
            "and the row says so without being selected");

    n = Lines (Out);
    for (unsigned i = 0; i < n; i++)
    {
        Expect (strcmp (Out[i], "/installppc86fr.toast") != 0,
                "a CD is not a disk");
    }
    n = CdromLines (Out);
    Expect (n == 1 && strcmp (Out[0], "/installppc86fr.toast") == 0,
            "it goes on its own line, with no star");

    // A bootable CD can start the machine, and it is not the order of the lines
    // that says so: the ROM is handed a *driver* through the PRAM.
    Expect ((s_Items ()[6].nCell[ColStartup - 1] & StateDisabled) == 0,
            "a bootable CD can be the startup disk");
    Expect (ChooserBootDriver (&s_Model) == 0,
            "as long as it is not, no driver is imposed");
    Cell (ColStartup);
    Expect (s_Model.nStartup == 6, "ticking its mark chooses it");
    Expect (ChooserBootDriver (&s_Model) == CHOOSER_BOOT_CDROM,
            "and the machine gets CDROMRefNum");
    n = CdromLines (Out);
    Expect (n == 1 && strcmp (Out[0], "/installppc86fr.toast") == 0,
            "the startup disk heads its own list");
    n = Lines (Out);
    Expect (n == 5, "and it does not slip in among the disks");
    // Going back to a disk must reset it to 0: a card that once started from a
    // CD would otherwise ask for the CD driver for ever.
    Select (0);
    Cell (ColStartup);
    Expect (ChooserBootDriver (&s_Model) == 0, "going back to a disk lifts the driver");
    Select (6);
    Cell (ColStartup);

    // Moving from one list to the other, both ways, without the volume ending
    // up in both.
    Open ();
    Select (4);
    Expect (MountPick (MountCD) == ChooserNothing, "changing how it mounts does not leave the screen");
    n = CdromLines (Out);
    Expect (n == 2, "a volume made a CD joins the other list");
    n = Lines (Out);
    for (unsigned i = 0; i < n; i++)
    {
        Expect (strcmp (Out[i], "/travaux.img") != 0, "and leaves the first");
    }
    MountPick (MountHD);
    n = CdromLines (Out);
    Expect (n == 1, "and going back works too");

    // The row's picture follows the mount, both ways. It used to be set once and
    // for all at layout, which is not redone when a menu changes: a disk stayed
    // drawn as a folder, and a folder as a disk, until the next screen.
    Select (5);
    const TGlyphImage *pWasFolder = s_Items ()[5].pIcon;
    MountPick (MountCD);
    Expect (s_Items ()[5].pIcon != pWasFolder,
            "making a volume a CD changes its picture");
    Expect (s_Items ()[5].pIcon == s_Items ()[6].pIcon,
            "to the same one as the CD already there");
    MountPick (MountHD);
    Expect (s_Items ()[5].pIcon == pWasFolder, "going back to a disk restores it");

    // Making the startup volume a CD does not take that away: it stays the
    // startup volume, and it is the announced driver that changes.
    Select (0);
    MountPick (MountCD);
    Expect (s_Model.nStartup == 0, "the startup volume made a CD stays it");
    Expect (ChooserBootDriver (&s_Model) == CHOOSER_BOOT_CDROM,
            "and the machine will start through the CD driver");
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
            snprintf (What, sizeof What, "the page fits 640x480 in %s",
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
        Expect (nIcons == 4, "the footer carries four marks");
        Expect (nSeen == 15, "settings, information, PRAM and shut down, each its own");
    }

    printf ("\n%u failure(s)\n", s_nFailures);
    free (s_pPixels);
    return s_nFailures == 0 ? 0 : 1;
}
