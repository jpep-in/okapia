//
// okapia_rom.cpp — what a ROM file is, and which disks it can start.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const unsigned long PPC_ROM_SIZE  = 4ul * 1024 * 1024;
static const unsigned long NANOKERNEL_ID = 0x30D064;
// The largest file read whole: a New World "Mac OS ROM" is under 4 MB.
static const unsigned long ROM_READ_MAX  = 8ul * 1024 * 1024;

static const unsigned QUADRA_CHECKSUM = 0xF1ACAD13u;

bool RomLooksLikeOne (const char *pName, unsigned long nBytes)
{
    const char *pDot = strrchr (pName, '.');
    const bool bNamed = pDot != 0 && strcasecmp (pDot, ".rom") == 0;
    // 64 KB (the first Macintosh) to 4 MB, and a New World file is under 4 MB.
    return bNamed || (nBytes >= 64ul * 1024 && nBytes <= PPC_ROM_SIZE
                      && (nBytes & (nBytes - 1)) == 0);
}

static unsigned Be16 (const unsigned char *p)
{
    return (unsigned) p[0] << 8 | p[1];
}

static unsigned Be32 (const unsigned char *p)
{
    return (unsigned) p[0] << 24 | (unsigned) p[1] << 16 | (unsigned) p[2] << 8 | p[3];
}

static void Say (char *pOut, unsigned nSize, const char *pText)
{
    snprintf (pOut, nSize, "%s", pText);
}

void RomIdentifyBytes (const unsigned char *pData, unsigned long nBytes, TRomFacts *pFacts)
{
    memset (pFacts, 0, sizeof *pFacts);
    pFacts->nBytes = (unsigned) nBytes;
    pFacts->Kind = RomUnusable;

    if (nBytes < 16)
    {
        pFacts->Verdict = RomTooSmall;
        Say (pFacts->Identity, sizeof pFacts->Identity, "not a ROM");
        Say (pFacts->Detail, sizeof pFacts->Detail, "too small");
        return;
    }

    // New World: a "Mac OS ROM" file. SheepShaver decodes it at start
    // (DecodeROM, rom_patches.cpp:148); every one starts Mac OS 8.1 to 9.0.4.
    if (memcmp (pData, "<CHRP-BOOT>", 11) == 0)
    {
        pFacts->Kind = RomPowerPCNewWorld;
        pFacts->Verdict = RomUsable;
        pFacts->bRecommended = true;
        pFacts->nFirstSystem = 810;
        pFacts->nLastSystem = ROM_LAST_SYSTEM;
        Say (pFacts->Identity, sizeof pFacts->Identity, "New World Mac OS ROM");
        Say (pFacts->Detail, sizeof pFacts->Detail, "NewWorld");
        return;
    }

    // Old World: the nanokernel's name decides (SheepShaver/src/rom_patches.cpp:682).
    if (nBytes == PPC_ROM_SIZE)
    {
        static const char *const Names[] =
            { "Boot TNT", "Boot Alchemy", "Boot Zanzibar", "Boot Gazelle", "Boot Gossamer", "NewWorld" };
        const char *pId = (const char *) pData + NANOKERNEL_ID;
        for (const char *pName : Names)
        {
            if (strncmp (pId, pName, strlen (pName)) == 0)
            {
                const char *pShort = strncmp (pName, "Boot ", 5) == 0 ? pName + 5 : pName;
                const bool bNew = strcmp (pShort, "NewWorld") == 0;
                pFacts->Kind = bNew ? RomPowerPCNewWorld : RomPowerPCOldWorld;
                pFacts->Verdict = RomUsable;
                pFacts->bRecommended = true;
                pFacts->nFirstSystem = bNew ? 810 : 752;
                pFacts->nLastSystem = ROM_LAST_SYSTEM;
                snprintf (pFacts->Identity, sizeof pFacts->Identity, "PowerPC ROM, %s nanokernel", pShort);
                Say (pFacts->Detail, sizeof pFacts->Detail, pShort);
                return;
            }
        }
        pFacts->Verdict = RomUnknownNanokernel;
        Say (pFacts->Identity, sizeof pFacts->Identity, "PowerPC ROM with an unknown nanokernel");
        Say (pFacts->Detail, sizeof pFacts->Detail, "4 MB, unknown nanokernel");
        return;
    }

    // 68k: the version word at offset 8 (BasiliskII/src/rom_patches.cpp:838),
    // and the checksum, the sum of every 16-bit word from offset 4.
    const unsigned nStored = Be32 (pData);
    unsigned nSum = 0;
    for (unsigned long i = 4; i + 1 < nBytes; i += 2)
    {
        nSum += Be16 (pData + i);
    }
    const unsigned nVersion = Be16 (pData + 8);
    pFacts->nChecksum = nStored;

    const char *pIdentity = "unknown ROM";
    switch (nVersion)
    {
    case 0x0000: pIdentity = "64K ROM, original Macintosh";  break;
    case 0x0075: pIdentity = "Mac Plus ROM";                 break;
    case 0x0276: pIdentity = "SE / Classic ROM";             break;
    case 0x0178: pIdentity = "Mac II ROM, not 32-bit clean"; break;
    case 0x067C: pIdentity = "32-bit clean ROM";             break;
    default:                                                 break;
    }
    // Only checksums read from real files are named; the rest is said by the
    // version word alone.
    if (nStored == QUADRA_CHECKSUM)     pIdentity = "Quadra/Centris 610, 650, 800 ROM (1993)";
    else if (nStored == 0x368CADFEu)    pIdentity = "Mac IIci ROM";
    else if (nStored == 0xB2E362A8u)    pIdentity = "Mac SE ROM";
    Say (pFacts->Identity, sizeof pFacts->Identity, pIdentity);

    if (nVersion != 0x067C)
    {
        pFacts->Verdict = nVersion == 0x0000 || nVersion == 0x0075 || nVersion == 0x0276
                          || nVersion == 0x0178 ? RomNot32BitClean : RomUnknown;
        Say (pFacts->Detail, sizeof pFacts->Detail,
             pFacts->Verdict == RomNot32BitClean ? "not 32-bit clean" : "not a ROM Okapia knows");
        return;
    }
    if (nSum != nStored)
    {
        pFacts->Verdict = RomChecksumMismatch;
        Say (pFacts->Detail, sizeof pFacts->Detail, "damaged (checksum)");
        return;
    }
    pFacts->Kind = Rom68k;
    pFacts->Verdict = RomUsable;
    pFacts->bRecommended = nStored == QUADRA_CHECKSUM;
    pFacts->nFirstSystem = 701;
    pFacts->nLastSystem = 810;
    Say (pFacts->Detail, sizeof pFacts->Detail, "32-bit clean");
}

bool RomIdentify (const char *pHostPath, TRomFacts *pFacts)
{
    memset (pFacts, 0, sizeof *pFacts);
    FILE *pFile = fopen (pHostPath, "rb");
    if (pFile == 0)
    {
        return false;
    }
    fseek (pFile, 0, SEEK_END);
    const long nSize = ftell (pFile);
    fseek (pFile, 0, SEEK_SET);
    if (nSize <= 0 || (unsigned long) nSize > ROM_READ_MAX)
    {
        fclose (pFile);
        RomIdentifyBytes ((const unsigned char *) "", 0, pFacts);
        pFacts->nBytes = nSize > 0 ? (unsigned) nSize : 0;
        if (nSize > 0)
        {
            pFacts->Verdict = RomUnknown;
            Say (pFacts->Identity, sizeof pFacts->Identity, "too large to be a ROM");
            Say (pFacts->Detail, sizeof pFacts->Detail, "too large to be a ROM");
        }
        return true;
    }
    unsigned char *pData = (unsigned char *) malloc ((size_t) nSize);
    const bool bRead = pData != 0 && fread (pData, 1, (size_t) nSize, pFile) == (size_t) nSize;
    fclose (pFile);
    if (!bRead)
    {
        free (pData);
        return false;
    }
    RomIdentifyBytes (pData, (unsigned long) nSize, pFacts);
    free (pData);
    return true;
}

/*
 *  A disk, against the ROMs at hand
 */
TDiskJudgement DiskJudge (bool bHfs, bool bBootable, unsigned nSystem, TDiskFlavour Flavour,
                          const TRomFacts *p68k, const TRomFacts *pPowerPC)
{
    TDiskJudgement J;
    memset (&J, 0, sizeof J);
    if (!bHfs)
    {
        J.Verdict = DiskData;
        return J;
    }
    if (!bBootable)
    {
        J.Verdict = DiskNoSystem;
        return J;
    }
    if (Flavour == FlavourUnreadable || nSystem == 0)
    {
        J.Verdict = DiskUnreadable;
        return J;
    }
    if (nSystem > ROM_LAST_SYSTEM)
    {
        J.Verdict = DiskTooNew;
        return J;
    }
    const bool b68k = p68k != 0 && p68k->Verdict == RomUsable;
    const bool bPpc = pPowerPC != 0 && pPowerPC->Verdict == RomUsable;
    const bool bPpcStarts = bPpc && pPowerPC->nFirstSystem <= nSystem;

    switch (Flavour)
    {
    case Flavour68k:
        J.Verdict = b68k ? DiskStarts68k : DiskNeeds68k;
        J.bOtherPowerPCUseless = !b68k && bPpc;
        break;

    case FlavourPowerPC:
        if (bPpcStarts)
        {
            J.Verdict = DiskStartsPowerPC;
        }
        else if (bPpc)
        {
            J.Verdict = DiskPowerPCTooOld;
            J.nPowerPCFloor = pPowerPC->nFirstSystem;
        }
        else
        {
            J.Verdict = DiskNeedsPowerPC;
            J.bOther68kUseless = b68k;
        }
        break;

    default:                            // universal: either engine
        if (b68k && bPpcStarts)
            J.Verdict = DiskStartsBoth;
        else if (b68k)
            J.Verdict = DiskStarts68k;
        else if (bPpcStarts)
            J.Verdict = DiskStartsPowerPC;
        else if (bPpc)
        {
            J.Verdict = DiskPowerPCTooOld;
            J.nPowerPCFloor = pPowerPC->nFirstSystem;
        }
        else
            J.Verdict = DiskNeedsAny;
        break;
    }
    return J;
}

bool DiskJudgementStarts (const TDiskJudgement &J)
{
    return J.Verdict == DiskStarts68k || J.Verdict == DiskStartsPowerPC
        || J.Verdict == DiskStartsBoth;
}
