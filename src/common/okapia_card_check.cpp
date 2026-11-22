//
// okapia_card_check.cpp — what a volume is as an Okapia card, read only.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_card_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

TCardFs CardFatKind (unsigned long long nClusters)
{
    return nClusters < 4085 ? CardFsFat12 : nClusters < 65525 ? CardFsFat16 : CardFsFat32;
}

static bool IsHex (unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

bool CardFindVersion (const unsigned char *pData, unsigned long nBytes, char *pOut, unsigned nSize)
{
    // YYMMDD-sha, the sha seven hexadecimal digits or more, then +xxxx when the
    // tree had changes. Preceded and followed by something that is not part
    // of a word, so that a longer number is not taken for one.
    for (unsigned long i = 0; i + 14 <= nBytes; i++)
    {
        if (i > 0 && (IsHex (pData[i - 1]) || pData[i - 1] == '-'))
            continue;
        unsigned long j = i;
        while (j < i + 6 && pData[j] >= '0' && pData[j] <= '9')
            j++;
        if (j != i + 6 || pData[j] != '-')
            continue;
        unsigned long k = j + 1;
        while (k < nBytes && IsHex (pData[k]))
            k++;
        if (k - (j + 1) < 7)
            continue;
        if (k < nBytes && pData[k] == '+')
        {
            unsigned long m = k + 1;
            while (m < nBytes && IsHex (pData[m]))
                m++;
            if (m > k + 1)
                k = m;
        }
        const unsigned long nLength = k - i;
        if (nLength + 1 > nSize)
            return false;
        memcpy (pOut, pData + i, nLength);
        pOut[nLength] = '\0';
        return true;
    }
    return false;
}

static const char WRAP_MARKER[] = "--- okapia.log is full and starts again here";
static const char CLEAN[] = "Shut down cleanly";
// A Macintosh started: from here to CLEAN its volumes are in use.
static const char STARTED[] = "Initialising the emulator";

// The build a line's banner names, "Okapia <build> (…), built …", or false.
static bool BannerVersion (const char *pLine, const char *pEnd, char *pOut, unsigned nSize)
{
    const char *pAt = strstr (pLine, "Okapia ");
    if (pAt == 0 || pAt >= pEnd)
        return false;
    const char *pBuilt = strstr (pAt, ", built ");
    if (pBuilt == 0 || pBuilt >= pEnd)
        return false;
    const char *pWord = pAt + 7;
    const char *pStop = pWord;
    while (pStop < pEnd && *pStop != ' ')
        pStop++;
    char Version[32];
    if ((size_t) (pStop - pWord) + 1 > sizeof Version)
        return false;
    memcpy (Version, pWord, (size_t) (pStop - pWord));
    Version[pStop - pWord] = '\0';
    char Found[32];
    if (!CardFindVersion ((const unsigned char *) Version, strlen (Version), Found, sizeof Found)
        && strcmp (Version, "unknown") != 0)
        return false;
    snprintf (pOut, nSize, "%s", Version);
    return true;
}

// *pbClean: no Macintosh left running. A session that only showed the boot menu
// and was switched off ran none, and left nothing to repair.
static void Summarise (const char *pText, const char *pEnd, unsigned *pnSessions,
                       char *pVersion, unsigned nVersion, bool *pbClean)
{
    for (const char *p = pText; p < pEnd; )
    {
        const char *pLineEnd = (const char *) memchr (p, '\n', (size_t) (pEnd - p));
        if (pLineEnd == 0)
            pLineEnd = pEnd;
        char Version[32];
        if (BannerVersion (p, pLineEnd, Version, sizeof Version))
        {
            (*pnSessions)++;
            snprintf (pVersion, nVersion, "%s", Version);
            *pbClean = true;            // a new session: the old one has been repaired
        }
        else
        {
            const char *pClean = strstr (p, CLEAN);
            const char *pStarted = strstr (p, STARTED);
            if (pClean != 0 && pClean < pLineEnd)
                *pbClean = true;
            else if (pStarted != 0 && pStarted < pLineEnd)
                *pbClean = false;
        }
        p = pLineEnd + 1;
    }
}

void CardLogSummary (const char *pText, unsigned *pnSessions, char *pVersion, unsigned nVersion,
                     bool *pbClean)
{
    *pnSessions = 0;
    pVersion[0] = '\0';
    *pbClean = true;
    // The board writes a fixed file round and round: what follows the marker
    // is older than what precedes it.
    const char *pEnd = pText + strlen (pText);
    const char *pWrap = strstr (pText, WRAP_MARKER);
    if (pWrap != 0)
    {
        const char *pOlder = (const char *) memchr (pWrap, '\n', (size_t) (pEnd - pWrap));
        if (pOlder != 0)
            Summarise (pOlder + 1, pEnd, pnSessions, pVersion, nVersion, pbClean);
        Summarise (pText, pWrap, pnSessions, pVersion, nVersion, pbClean);
    }
    else
    {
        Summarise (pText, pEnd, pnSessions, pVersion, nVersion, pbClean);
    }
}

static bool FileThere (const char *pRoot, const char *pName)
{
    char Path[512];
    snprintf (Path, sizeof Path, "%s/%s", pRoot, pName);
    struct stat Info;
    return stat (Path, &Info) == 0 && S_ISREG (Info.st_mode);
}

static unsigned char *ReadWhole (const char *pRoot, const char *pName, unsigned long nMax,
                                 unsigned long *pnBytes)
{
    char Path[512];
    snprintf (Path, sizeof Path, "%s/%s", pRoot, pName);
    FILE *pFile = fopen (Path, "rb");
    if (pFile == 0)
        return 0;
    fseek (pFile, 0, SEEK_END);
    const long nSize = ftell (pFile);
    fseek (pFile, 0, SEEK_SET);
    if (nSize <= 0 || (unsigned long) nSize > nMax)
    {
        fclose (pFile);
        return 0;
    }
    unsigned char *pData = (unsigned char *) malloc ((size_t) nSize + 1);
    const bool bRead = pData != 0 && fread (pData, 1, (size_t) nSize, pFile) == (size_t) nSize;
    fclose (pFile);
    if (!bRead)
    {
        free (pData);
        return 0;
    }
    pData[nSize] = '\0';
    *pnBytes = (unsigned long) nSize;
    return pData;
}

void CardDiagnose (const char *pRoot, const TCardVolume *pVolume, TCardDiagnosis *pOut)
{
    memset (pOut, 0, sizeof *pOut);
    pOut->bFatOK = pVolume->Fs == CardFsFat32;
    pOut->bSmallClusters = pOut->bFatOK && pVolume->nCluster != 0 && pVolume->nCluster < 32768;

    static const char *const BOOT_FILES[] =
        { "start4.elf", "fixup4.dat", "bcm2711-rpi-4-b.dtb", "armstub8-rpi4.bin", "config.txt" };
    pOut->bKernel = FileThere (pRoot, CARD_KERNEL);
    unsigned n = 0;
    for (const char *pFile : BOOT_FILES)
    {
        if (!FileThere (pRoot, pFile))
        {
            n += (unsigned) snprintf (pOut->Missing + n, n < sizeof pOut->Missing ? sizeof pOut->Missing - n : 0,
                                      "%s%s", n != 0 ? ", " : "", pFile);
            if (n >= sizeof pOut->Missing)
                break;
        }
    }

    unsigned long nBytes = 0;
    if (pOut->bKernel)
    {
        unsigned char *pKernel = ReadWhole (pRoot, CARD_KERNEL, 16ul * 1024 * 1024, &nBytes);
        if (pKernel != 0)
        {
            CardFindVersion (pKernel, nBytes, pOut->Version, sizeof pOut->Version);
            free (pKernel);
        }
    }

    char *pLog = (char *) ReadWhole (pRoot, "okapia.log", 32ul * 1024 * 1024, &nBytes);
    if (pLog != 0)
    {
        // A log the board pads to the sector: the padding is not text.
        for (unsigned long i = 0; i < nBytes; i++)
            if (pLog[i] == '\0')
                pLog[i] = '\n';
        char Version[32];
        bool bClean = false;
        CardLogSummary (pLog, &pOut->nSessions, Version, sizeof Version, &bClean);
        pOut->bUncleanShutdown = pOut->nSessions != 0 && !bClean;
        if (pOut->Version[0] == '\0')
            snprintf (pOut->Version, sizeof pOut->Version, "%s", Version);
        free (pLog);
    }
}
