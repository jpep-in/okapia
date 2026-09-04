/*
 * hfs_volume_circle.h — what Okapia knows about the volumes on the card.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_HFS_VOLUME_H
#define OKAPIA_HFS_VOLUME_H

#include <stddef.h>

// One HFS volume found on the card. Everything but Path and bClean comes from
// libhfs's hfs_vstat(), so nothing here is a second parse of the same fields.
struct THfsVolumeInfo
{
    char          Path[64];       // as the emulator would open it, e.g. /machd76.image
    char          Name[28];       // HFS_MAX_VLEN + 1
    unsigned long TotalKB;
    unsigned long FreeKB;
    unsigned long NumFiles;
    unsigned long NumDirs;
    unsigned long Blessed;        // CNID of the System folder; non-zero means bootable
    bool          bClean;         // MDB says the volume was unmounted cleanly

    // drLsMod, the last time the Mac wrote to this volume, as Unix seconds.
    // libhfs converts it with a plain 2082844800 shift here (data.c:467): its
    // time-zone correction needs HAVE_MKTIME, which this build never defines,
    // so tzdiff stays 0. It is therefore *local* seconds, like the Mac's own
    // clock — not UTC.
    long          nLastModified;
};

// Read the MDB and say whether the volume was unmounted cleanly. Must run
// before any mount, because mounting is what repairs the answer away.
bool HfsInspect (const char *pPath);

// Describe a volume without touching it: libhfs writes nothing when it is
// mounted read-only, scavenging included (volume.c:1059).
bool HfsDescribe (const char *pPath, THfsVolumeInfo *pInfo);

// Every HFS volume in the root of the card, in directory order. Read-only.
unsigned HfsInventory (THfsVolumeInfo *pList, unsigned nMax);

// The version of the System installed on a volume, from the 'vers' resource of
// the System file in the blessed folder.
struct THfsSystemVersion
{
    unsigned nMajor;
    unsigned nMinor;
    unsigned nBugfix;
    char     Short[32];       // as the file states it, e.g. "7.6"
    char     File[32];        // the System file's name, which is localised

    // Does the System file carry PowerPC code? True when its resource map holds
    // a 'cfrg', the code fragment resource. Measured on the two volumes staged
    // here: 7.1 has none, 7.6 has ten — and that 7.6 was installed for a 68k
    // machine, which is the whole point. From System 7.5 on Apple shipped one
    // universal System, so this says "a PowerPC could run it too", never "only
    // a PowerPC can".
    bool     bNativeCode;
};

// Read it without touching the volume. False when there is no blessed folder,
// no System file in it, or no readable 'vers' resource.
bool HfsSystemVersion (const char *pPath, THfsSystemVersion *pVersion);

// Which emulator can boot this System — the question the boot menu has to
// answer before it can offer anything.
enum THfsSystemFlavour
{
    HfsFlavourUnreadable,   // no version to go on
    HfsFlavour68k,          // Basilisk II, and nothing to ask
    HfsFlavourPowerPC,      // SheepShaver, and nothing to ask
    HfsFlavourUniversal     // either will do, so somebody has to choose
};

// The rule, kept apart from any volume so it can be tested on its own.
//
// Two bounds, both read off the emulators' own patch lists, and one measurement
// in between:
//
//   - below 7.5.2, SheepShaver has no correctives at all
//     (SheepShaver/src/rsrc_patches.cpp names 7.5.2 as its earliest);
//   - from 8.5 up, Basilisk stops (BasiliskII/src/rsrc_patches.cpp names no
//     version above 8.1) — 8.5 is where Apple dropped the 68k machines;
//   - between the two, the System is universal in the literal sense, and
//     'cfrg' is what says the PowerPC half is actually installed.
//
// Note what this deliberately does not do: treat 'cfrg' alone as "PowerPC
// only". A native fragment proves the PowerPC half is installed and nothing
// more — Mac OS 8.6, which no 68k machine can boot, still carries some 1.5 MB
// of 68k code in its System file, 'gpch' alone being the largest resource type
// in it. So the fragment answers "could a PowerMac run this", never "must it".
THfsSystemFlavour HfsFlavourOf (const THfsSystemVersion *pVersion);

// Scavenge a volume an interrupted session left marked in use, and mark it
// clean again. This writes to the volume.
bool HfsRepair (const char *pPath);

#endif
