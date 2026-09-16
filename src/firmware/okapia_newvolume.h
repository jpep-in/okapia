/*
 * okapia_newvolume.h — the screen that makes a volume.
 *
 * Reached from the command row at the top of the chooser's list, because what
 * it makes lands in that list. It asks two things and nothing else: what the
 * Macintosh will call the volume, and how big it is.
 *
 * It does not ask for a file name. The Mac's volume name is written in MacRoman
 * and may hold characters no FAT directory entry will take, so the firmware
 * picks the file name itself and the user names the volume — which is the only
 * one of the two a Macintosh ever shows.
 *
 * Nothing here knows what a card is. The sizes it offers are handed to it, and
 * that is what enforces the one rule this screen must not get wrong: a volume
 * that does not fit cannot be created, and the way to make that impossible is
 * not to offer it. A size the caller left out cannot be picked, so there is no
 * refusal to write and no arithmetic to repeat on both sides.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_newvolume_h
#define _okapia_newvolume_h

#include "okapia_widgets.h"

// 27 characters and a terminator: HFS_MAX_VLEN, which is what hfs_format()
// refuses beyond (libhfs/hfs.h:28).
static const unsigned NEWVOLUME_NAME = 28;

// The rungs a card can offer. Eight is more than any card needs — the largest
// is 2 GB, where HFS standard runs out of allocation blocks.
static const unsigned NEWVOLUME_SIZES = 8;

struct TNewVolume
{
    char Name[NEWVOLUME_NAME];

    // What fits, smallest first, in megabytes. The caller works these out from
    // what the card has left and its own margin; an empty list means nothing
    // fits, and the screen then says so instead of offering a choice it would
    // have to take back.
    unsigned long SizeMB[NEWVOLUME_SIZES];
    unsigned      nSizes;
    unsigned      nPick;                // which of them, an index into SizeMB

    unsigned long nFreeMB;              // what is left on the card, for the eye
};

enum TNewVolumeAction
{
    NewVolumeNothing,                   // the model changed; repaint
    NewVolumeCreate,
    NewVolumeCancel
};

void NewVolumeDraw (TSurface *pSurface, TNewVolume *pNew);
void NewVolumeRepaint (TSurface *pSurface);
unsigned NewVolumeWidgets (TWidget **ppList);

// Brings the controls in step with the model: the button that cannot work says
// so before it is pressed, which is the same promise the chooser's Start makes.
void NewVolumeSync (TNewVolume *pNew);

TNewVolumeAction NewVolumeOperate (TNewVolume *pNew, int nIndex);

// Whether the last layout ran out of room, asked by the checks in every
// language — the same reason the chooser answers it.
bool NewVolumeOverflowed (void);

// The frame the last layout drew, so a check can say that nothing strayed into
// its border — every element spilling out was invisible in the numbers until
// the numbers were asked for.
TRect NewVolumeDialog (void);

// Whether the model is one that could be created: a name of at least one
// character, and a size to give it. Public because the caller must not have to
// re-derive the rule to know whether its own work is about to be wasted.
bool NewVolumeReady (const TNewVolume *pNew);

#endif
