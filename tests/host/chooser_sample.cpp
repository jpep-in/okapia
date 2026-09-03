/*
 * chooser_sample.cpp — a card that does not exist, holding every awkward case.
 *
 * Shared by the renderer and the checks, because a picture and a measurement of
 * two different cards prove less together than either does alone.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "okapia_chooser.h"

static void Volume (TChooser *p, const char *pPath, const char *pName,
                    const char *pSystem, bool bClean, bool bMounted, bool bReadOnly,
                    unsigned long nFreeKB)
{
    TChooserVolume *v = &p->Volumes[p->nCount++];
    snprintf (v->Path, sizeof v->Path, "%s", pPath);
    snprintf (v->Name, sizeof v->Name, "%s", pName);
    snprintf (v->System, sizeof v->System, "%s", pSystem);
    v->bBootable = pSystem[0] != '\0';
    v->bClean    = bClean;
    v->bMounted  = bMounted;
    v->bReadOnly = bReadOnly;
    v->nFreeKB   = nFreeKB;
}

void ChooserSample (TChooser *p)
{
    memset (p, 0, sizeof *p);
    Volume (p, "/boot71.img",  "Macintosh HD",  "7.1.2", true,  true,  false, 18 * 1024);
    Volume (p, "/machd76.image","Mac HD 7.6",   "7.6",   false, true,  false, 240 * 1024);
    Volume (p, "/os81.img",    "Mac OS 8.1",    "8.1",   true,  true,  true,  95 * 1024);
    Volume (p, "/boot608.hda", "Système 6.0.8", "6.0.8", true,  false, false, 3 * 1024);
    Volume (p, "/travaux.img", "Travaux",       "",      true,  true,  false, 512 * 1024);
    p->nStartup = 0;
}

