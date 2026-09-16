/*
 * mac_encoding_circle.cpp — file names, between a FAT card and a Macintosh.
 *
 * The shared folder has a foot in each of two alphabets. FatFs gives long names
 * as UTF-8; a Macintosh names its files in MacRoman. Upstream's Unix layer
 * declines the problem — extfs_unix.cpp returns the pointer it was given — which
 * is right on a Linux desktop, where both sides are UTF-8, and wrong here: a
 * card written on this machine hands "Résumé.txt" to a French System 7.1 as
 * "ReÃÅ¡umeÃÅ¡.txt", and a file the Mac creates comes back mangled the other way.
 * It shows at the first accented name, which on the machines this project is
 * for is the first folder anyone opens.
 *
 * Hooked with --wrap rather than by patching external/ (docs/contributing/guide.md), so the two
 * functions below replace extfs_unix.cpp's outright. Neither allocates: the
 * caller copies the result immediately (extfs.cpp:229) and never keeps it.
 *
 * The table is generated from Python's mac_roman codec, which carries Apple's
 * ROMAN.TXT — see scripts/gen-macroman.py. Typing 128 code points by hand is
 * how one accented letter in one language goes wrong for years.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include <string.h>

#include "okapia_circle.h"
#include "cpu_emulation.h"
#include "emul_op.h"
#include "main.h"
#include "macos_util.h"

#include "macroman_table.h"

#define FROM "okapia-extfs"

// A FAT long name is 255 UTF-16 units, so at most 765 bytes of UTF-8 coming in
// and, the other way, 255 MacRoman bytes expanding to at most 765. A kilobyte
// covers both with room for the terminator. A name that would still not fit
// comes back unconverted rather than truncated: a name the Mac cannot read is a
// nuisance, half a name is a bug.
static const size_t NAME_MAX_BYTES = 1024;

static char s_ToMac[NAME_MAX_BYTES];
static char s_ToHost[NAME_MAX_BYTES];

/*
 *  Which alphabet is the Macintosh using?
 *
 *  Only MacRoman is converted. On a Japanese or Cyrillic System the same bytes
 *  mean something else, and converting them would not be a lesser service but a
 *  wrong one — so the answer there is to pass the name through untouched.
 *
 *  The Mac is asked, because nothing else knows: a stub of eighteen bytes calls
 *  ScriptUtil() for smMacSysScript. That means running 68k code, which is only
 *  possible once the processor is running — ExtFSInit() converts the volume
 *  name before that, and it is ASCII, so the probe waits. The technique is
 *  infinite-mac's (Unix/mac_encodings.cpp:316); the caching is not, and it
 *  matters: that port re-enters the emulator twice per file name.
 */

// cpu_ticks_circle.cpp sets it on the first visit, so it is true exactly when
// there is a 68k to ask.
extern bool MacIsExecuting (void);

enum TScript { ScriptUnknown, ScriptRoman, ScriptOther };
static TScript s_Script = ScriptUnknown;

static const uint16 smMacSysScript = 18;
static const int16  smRoman        = 0;

static void ProbeScript (void)
{
    if (s_Script != ScriptUnknown || !MacIsExecuting ())
    {
        return;
    }

    static const uint8 Proc[] =
    {
        0x59, 0x4F,                             // subq.w  #4,sp
        0x3F, 0x3C, 0x00, 0x00,                 // move.w  #selector,-(sp)
        0x2F, 0x3C, 0x84, 0x02, 0x00, 0x08,     // move.l  #-2080243704,-(sp)
        0xA8, 0xB5,                             // ScriptUtil()
        0x20, 0x1F,                             // move.l  (a7)+,d0
        (uint8) (M68K_RTS >> 8), (uint8) (M68K_RTS & 0xFF)
    };

    M68kRegisters r;
    r.d[0] = sizeof Proc;
    Execute68kTrap (0xA71E, &r);                // NewPtrSysClear()
    uint32 nProc = r.a[0];
    if (nProc == 0)
    {
        return;                                 // no memory: ask again later
    }

    Host2Mac_memcpy (nProc, (void *) Proc, sizeof Proc);
    // The selector goes in as data rather than as an assembled constant, so the
    // name above is the one thing that decides it.
    WriteMacInt16 (nProc + 4, smMacSysScript);
    Execute68k (nProc, &r);
    int32 nScript = (int32) r.d[0];

    r.a[0] = nProc;
    Execute68kTrap (0xA01F, &r);                // DisposePtr()

    s_Script = (nScript == smRoman) ? ScriptRoman : ScriptOther;
    CLogger::Get ()->Write (FROM, LogNotice,
                            "the Macintosh writes script %d: file names are %s",
                            (int) nScript,
                            s_Script == ScriptRoman ? "converted to MacRoman"
                                                    : "passed through unchanged");
}

static bool Converting (void)
{
    ProbeScript ();
    return s_Script != ScriptOther;             // unknown yet: MacRoman is the bet
}

/*
 *  UTF-8, decoded and encoded by hand
 *
 *  Circle has no iconv and nothing here may allocate, so this is the whole of
 *  it: three forms in, three forms out, and anything malformed refuses rather
 *  than guesses.
 */

static const uint32 BAD = 0xFFFFFFFF;

static uint32 Utf8Next (const char *p, size_t nLeft, size_t *pnUsed)
{
    const uint8 *q = (const uint8 *) p;
    uint8 c = q[0];

    if (c < 0x80)                     { *pnUsed = 1; return c; }
    if ((c & 0xE0) == 0xC0 && nLeft >= 2 && (q[1] & 0xC0) == 0x80)
    {
        *pnUsed = 2;
        return ((uint32) (c & 0x1F) << 6) | (q[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && nLeft >= 3 && (q[1] & 0xC0) == 0x80
                                         && (q[2] & 0xC0) == 0x80)
    {
        *pnUsed = 3;
        return ((uint32) (c & 0x0F) << 12) | ((uint32) (q[1] & 0x3F) << 6)
             | (q[2] & 0x3F);
    }
    // Four-byte forms are outside the Basic Multilingual Plane and so outside
    // MacRoman by definition; they are consumed and refused, not misread.
    if ((c & 0xF8) == 0xF0 && nLeft >= 4)       { *pnUsed = 4; return BAD; }

    *pnUsed = 1;
    return BAD;
}

static size_t Utf8Put (char *p, uint32 nCode)
{
    if (nCode < 0x80)   { p[0] = (char) nCode; return 1; }
    if (nCode < 0x800)
    {
        p[0] = (char) (0xC0 | (nCode >> 6));
        p[1] = (char) (0x80 | (nCode & 0x3F));
        return 2;
    }
    p[0] = (char) (0xE0 | (nCode >> 12));
    p[1] = (char) (0x80 | ((nCode >> 6) & 0x3F));
    p[2] = (char) (0x80 | (nCode & 0x3F));
    return 3;
}

// 128 comparisons for one accented letter, a handful of times per name. A
// reverse table would be 64 KB to save nothing anybody can measure.
static int MacRomanOf (uint32 nCode)
{
    for (int i = 0; i < 128; i++)
    {
        if (MacRomanHigh[i] == nCode)
        {
            return 0x80 + i;
        }
    }
    return -1;
}

/*
 *  The two functions extfs.cpp calls
 */

extern "C" const char *__wrap__Z25host_encoding_to_macromanPKc (const char *pName)
{
    if (pName == 0 || !Converting ())
    {
        return pName;
    }

    size_t nLeft = strlen (pName);
    size_t nOut  = 0;
    const char *p = pName;

    while (nLeft > 0)
    {
        size_t nUsed = 0;
        uint32 nCode = Utf8Next (p, nLeft, &nUsed);

        int nByte;
        if (nCode == BAD)
        {
            return pName;               // not UTF-8: leave it exactly as it is
        }
        else if (nCode < 0x80)
        {
            nByte = (int) nCode;
        }
        else if ((nByte = MacRomanOf (nCode)) < 0)
        {
            return pName;               // no MacRoman for it: better whole
        }

        if (nOut + 1 >= NAME_MAX_BYTES)
        {
            return pName;
        }
        s_ToMac[nOut++] = (char) nByte;

        p     += nUsed;
        nLeft -= nUsed;
    }

    s_ToMac[nOut] = '\0';
    return s_ToMac;
}

extern "C" const char *__wrap__Z25macroman_to_host_encodingPKc (const char *pName)
{
    if (pName == 0 || !Converting ())
    {
        return pName;
    }

    size_t nOut = 0;
    for (const unsigned char *p = (const unsigned char *) pName; *p != 0; p++)
    {
        uint32 nCode = (*p < 0x80) ? *p : MacRomanHigh[*p - 0x80];

        if (nOut + 4 >= NAME_MAX_BYTES)
        {
            return pName;
        }
        nOut += Utf8Put (s_ToHost + nOut, nCode);
    }

    s_ToHost[nOut] = '\0';
    return s_ToHost;
}
