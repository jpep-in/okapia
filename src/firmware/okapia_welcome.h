/*
 * okapia_welcome.h — the welcome page: what Okapia is, and its Okapia Workspace.
 *
 * Shown on a desktop before the boot menu when there is no workspace yet, and
 * from the boot menu's footer to change one. Three steps read from top to
 * bottom, as the installer reads (okapia-installer): the workspace, a ROM, a
 * System; and Continue, which opens the boot menu. The page is handed values and
 * answers actions, as every page here: the host does the choosing of files and
 * the copying (src/common/okapia_menu.cpp, FirmwareWelcome).
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_welcome_h
#define _okapia_welcome_h

#include "okapia_widgets.h"

static const unsigned WELCOME_PATH  = 256;
static const unsigned WELCOME_LINE  = 112;
static const unsigned WELCOME_LINES = 12;      // per step; more are counted

// One engine's ROM, as the installer shows it: one line for each, 68K and
// PowerPC, whatever the workspace holds — the one the preferences name, else a
// usable one; and when there is none, which one to look for.
struct TWelcomeRom
{
    bool     bThere;
    char     Line[WELCOME_LINE];        // "Quadra/Centris 610, 650, 800 ROM (1993) · okapia.rom"
    char     Note[WELCOME_LINE];        // what it starts, or the recommendation
    bool     bChime;                    // Listen is offered
    char     CardPath[64];              // "/okapia.rom"
};

enum { WelcomeRom68k, WelcomeRomPowerPC, WelcomeRomCount };

// Whom the workspace is prepared for, as the installer's "For" menu: this
// computer (a folder Okapia runs from here), or one of the boards the release's
// manifest names (its SD card) — "Raspberry Pi 4" until a manifest is read.
static const unsigned WELCOME_TARGETS = 9;
enum { WelcomeForDesktop = 0, WelcomeForFirstBoard = 1 };

struct TWelcome
{
    unsigned    nTarget;                        // 0 this computer, then the boards
    unsigned    nTargets;
    char        TargetName[WELCOME_TARGETS][48];
    char        TargetId[WELCOME_TARGETS][16];  // the board's id, "pi4"
    // For a board: Prepare the Card, or Update the Card when an older Okapia
    // is on it; enabled when there is a release to put on it and work to do.
    bool        bPrepareEnabled;
    bool        bUpdate;
    char        Workspace[WELCOME_PATH];        // as shown, "" when none
    // What the workspace's volume is: "SD card · FAT32, 32 KB clusters · 7.1 GB
    // free · Okapia 261121-…", or what is wrong with it for a Pi.
    char        WorkspaceNote[WELCOME_LINE * 2];
    TWelcomeRom Rom[WelcomeRomCount];
    // "Mac HD 7.1 — System 7.1.2, 68K: starts with the 68K ROM"
    char        System[WELCOME_LINES][WELCOME_LINE];
    unsigned    nSystems;                       // all of them, shown or not
    bool        bNothingStarts;                 // disks, and none the ROMs start
    bool        bFill;                          // the page is the window (a desktop's)
    char        Status[WELCOME_LINE * 2];       // what the last action did
};

void WelcomeDraw (TSurface *pSurface, TWelcome *pWelcome);
// After WelcomeDraw: the height the page left unused (what it needs is the
// surface's less this), and where its title lies, the band that moves a
// borderless window.
unsigned WelcomeSpare (void);
// The rows above the steps (the title) and below them (the buttons): what stays
// in place while the steps scroll.
void WelcomeBands (unsigned *pnTop, unsigned *pnBottom, unsigned *pnRight);   // and the frame's width
void WelcomeRepaint (TSurface *pSurface);
unsigned WelcomeWidgets (TWidget **ppList);
void WelcomeSync (TWelcome *pWelcome);

enum TWelcomeAction
{
    WelcomeNothing,
    WelcomeTarget,                      // the "For" menu changed: nTarget
    WelcomeCard,                        // an SD card, from the list of those inserted
    WelcomeOpen,                        // an existing workspace
    WelcomeCreate,                      // a new one
    WelcomeAddRom,                      // either line's Choose: the file goes to its family
    WelcomeListen68k,
    WelcomeListenPowerPC,
    WelcomeAddSystem,
    WelcomePrepare,                     // put the release on the card
    WelcomeContinue,                    // to the boot menu
    WelcomeQuit
};

TWelcomeAction WelcomeOperate (TWelcome *pWelcome, int nIndex);

/*
 *  The SD cards inserted, to choose one as the workspace
 */
static const unsigned WELCOME_CARDS = 8;

struct TWelcomeCards
{
    char     Line[WELCOME_CARDS][WELCOME_LINE];   // "OKAPIA — FAT32, 31.9 GB"
    unsigned nCount;
};

void WelcomeCardsDraw (TSurface *pSurface, TWelcomeCards *pCards);
void WelcomeCardsRepaint (TSurface *pSurface);
unsigned WelcomeCardsWidgets (TWidget **ppList);
// After the list was read again: the rows, and Use greyed with none chosen.
void WelcomeCardsSync (TWelcomeCards *pCards);

enum TWelcomeCardsAction { WelcomeCardsNothing, WelcomeCardsUse, WelcomeCardsCancel };
// *pnChosen: the row chosen, when the answer is Use.
TWelcomeCardsAction WelcomeCardsOperate (TWelcomeCards *pCards, int nIndex, int *pnChosen);

#endif
