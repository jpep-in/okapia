/*
 * chooser_sample.cpp — a card that does not exist, holding every awkward case.
 *
 * Shared by the renderer and the checks, because a picture and a measurement of
 * two different cards prove less together than either does alone.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "okapia_chooser.h"

static void Volume (TChooser *p, const char *pPath, const char *pName,
                    const char *pSystem, TChooserCPU CPU, bool bClean, bool bMounted,
                    TChooserMount Mount, unsigned long nFreeKB,
                    unsigned long nTotalKB)
{
    TChooserVolume *v = &p->Volumes[p->nCount++];
    snprintf (v->Path, sizeof v->Path, "%s", pPath);
    snprintf (v->Name, sizeof v->Name, "%s", pName);
    snprintf (v->System, sizeof v->System, "%s", pSystem);
    v->CPU       = CPU;
    v->bBootable = pSystem[0] != '\0';
    v->bClean    = bClean;
    v->bMounted  = bMounted;
    v->Mount     = Mount;
    v->nFreeKB   = nFreeKB;
    v->nTotalKB  = nTotalKB;
}

void ChooserSample (TChooser *p)
{
    memset (p, 0, sizeof *p);
    // One of each processor, because "universal" is the longest label and the
    // row it lands on is also the one carrying a state — which is the widest
    // line the screen ever has to fit.
    Volume (p, "/boot71.img",  "Macintosh HD",  "7.1.2", CPU68k,       true,  true,  MountHD,         18 * 1024,  40 * 1024);
    Volume (p, "/machd76.image","Mac HD 7.6",   "7.6",   CPUUniversal, false, true,  MountHD,         240 * 1024, 500 * 1024);
    Volume (p, "/os81.img",    "Mac OS 8.1",    "8.1",   CPUUniversal, true,  true,  MountHDReadOnly, 95 * 1024,  200 * 1024);
    Volume (p, "/boot608.hda", "Système 6.0.8", "6.0.8", CPUUnknown,   true,  false, MountHD,         3 * 1024,   20 * 1024);
    Volume (p, "/travaux.img", "Travaux",       "",      CPUUnknown,   true,  true,  MountHD,         512 * 1024, 700 * 1024);
    // Last on purpose: the checks below address volumes by index, and a
    // newcomer in the middle would move them without saying so. The install CD
    // is the widest row the screen ever has to fit: a long name, a System, a
    // processor and a mount that is not the ordinary one.
    Volume (p, "/macos86.img", "Mac OS 8.6",    "8.6",   CPUPowerPC,   true,  true,  MountHD,         60 * 1024,  300 * 1024);
    Volume (p, "/installppc86fr.toast", "Mac OS 8.6 install", "8.6", CPUPowerPC, true, true, MountCD, 0, 600 * 1024);
    p->nStartup = 0;
    // Which emulator this image would carry. The screen is handed it rather
    // than compiled with it, so the same sample renders both ways.
    p->Built = CPU68k;
}

