/*
 * check_encoding.cpp — the shared folder's file names.
 *
 * Between a FAT card in UTF-8 and a Macintosh in MacRoman, the conversion is
 * exactly the kind of code that looks right and damages one name in fifty: a
 * 128-entry table, three UTF-8 forms, and two cases where doing nothing is
 * better than doing half. None of it needs a card, an emulator or a Macintosh,
 * so none of it has any reason to be discovered on a card.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "sysdeps.h"
#include "cpu_emulation.h"

// mac_encoding_circle.cpp reaches them through --wrap; here they are named.
extern "C" const char *__wrap__Z25host_encoding_to_macromanPKc (const char *);
extern "C" const char *__wrap__Z25macroman_to_host_encodingPKc (const char *);

#define ToMac  __wrap__Z25host_encoding_to_macromanPKc
#define ToHost __wrap__Z25macroman_to_host_encodingPKc

/*
 *  The Macintosh this test does not have
 *
 *  The Script Manager probe only runs when a 68000 is running. There is none
 *  here, which is also the case during a real startup: MacRoman is then the
 *  default bet, and that bet is what is measured.
 */
bool MacIsExecuting (void) { return false; }

void Execute68k (uint32, M68kRegisters *)      { }
void Execute68kTrap (uint16, M68kRegisters *)  { }

// Host2Mac_memcpy() is inline under DIRECT_ADDRESSING and reads this offset.
// Nothing calls it here — the probe does not run — but it has to exist for
// the linker to be satisfied.
uintptr MEMBaseDiff;

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s %s\n", bOK ? "ok  " : "FAIL ", pWhat);
    if (!bOK)
    {
        s_nFailures++;
    }
}

static void ExpectSame (const char *pGot, const char *pWant, const char *pWhat)
{
    bool bOK = strcmp (pGot, pWant) == 0;
    printf ("  %s %s\n", bOK ? "ok  " : "FAIL ", pWhat);
    if (!bOK)
    {
        printf ("        expected \"%s\", got \"%s\"\n", pWant, pGot);
        s_nFailures++;
    }
}

/*
 *  There and back
 */

static void CheckRoundTrip (void)
{
    printf ("round trip\n");

    // An accented name, the case that motivated the whole file. In MacRoman, é
    // is 0x8E and û is 0x9E.
    static const char Mac[]  = "R\x8Esum\x8E.txt";
    static const char Host[] = "R\xC3\xA9sum\xC3\xA9.txt";

    ExpectSame (ToMac (Host), Mac,  "accented UTF-8 to MacRoman");
    ExpectSame (ToHost (Mac), Host, "and MacRoman to UTF-8");

    ExpectSame (ToMac (ToHost (Mac)), Mac, "the round trip loses nothing");

    // ASCII goes through untouched, which is nearly every name.
    ExpectSame (ToMac ("System Folder"),  "System Folder", "ASCII passes as it is");
    ExpectSame (ToHost ("System Folder"), "System Folder", "both ways");

    ExpectSame (ToMac (""),  "", "so does the empty name");
    ExpectSame (ToHost (""), "", "both ways");
}

/*
 *  The characters MacRoman has and nobody expects
 */

static void CheckHighRange (void)
{
    printf ("the top of the table\n");

    // 0xA5 is the bullet, 0xD0 the en dash, 0xAA the trademark sign: three
    // code points outside Latin-1, so three bytes in UTF-8. They prove the
    // encoder does not stop at two.
    static const char Mac[]  = "\xA5\xD0\xAA";
    static const char Host[] = "\xE2\x80\xA2\xE2\x80\x93\xE2\x84\xA2";

    ExpectSame (ToHost (Mac), Host, "three bytes out when three are needed");
    ExpectSame (ToMac (Host), Mac,  "and three bytes in");

    // All 128 entries, one by one: a single transcription error would hide
    // behind any narrower test.
    bool bAll = true;
    for (int i = 0x80; i < 0x100; i++)
    {
        char Name[2] = { (char) i, 0 };
        char Copy[8];
        strcpy (Copy, ToHost (Name));
        bAll = bAll && strcmp (ToMac (Copy), Name) == 0;
    }
    Expect (bAll, "the 128 high entries survive the round trip");
}

/*
 *  What does not convert
 *
 *  Handing back the original name is a choice, not an oversight: a name the
 *  Mac cannot read is a nuisance, half a name is a loss.
 */

static void CheckRefusals (void)
{
    printf ("refusals\n");

    // A Chinese character: perfectly valid UTF-8, absent from MacRoman.
    static const char Chinese[] = "\xE6\x96\x87.txt";
    ExpectSame (ToMac (Chinese), Chinese,
                "a character outside MacRoman leaves the name whole");

    // Malformed UTF-8 — a lone continuation — must produce nothing.
    static const char Broken[] = "a\xC3.txt";
    ExpectSame (ToMac (Broken), Broken, "broken UTF-8 is not guessed at");

    // A supplementary plane (an emoji): four bytes, outside MacRoman.
    static const char Emoji[] = "\xF0\x9F\x8D\x8E.txt";
    ExpectSame (ToMac (Emoji), Emoji, "nor is a four-byte character");

    Expect (ToMac (0) == 0 && ToHost (0) == 0, "a null pointer comes back null");
}

/*
 *  Lengths
 */

static void CheckLengths (void)
{
    printf ("lengths\n");

    // 255 bytes, the limit of a FAT long name, all accented: 510 bytes of UTF-8
    // one way, 255 of MacRoman the other. The buffer must hold.
    char Long[256];
    for (int i = 0; i < 255; i++)
    {
        Long[i] = (char) 0x8E;      // é
    }
    Long[255] = 0;

    char Utf8[1024];
    strcpy (Utf8, ToHost (Long));
    Expect (strlen (Utf8) == 510, "255 accented letters make 510 bytes");
    ExpectSame (ToMac (Utf8), Long, "and come back whole");

    // Past the buffer, the name comes back intact rather than truncated. No FAT
    // card produces such a name; it is the guard being measured, not the case.
    static char Huge[600];
    for (int i = 0; i < 599; i++)
    {
        Huge[i] = (char) 0xA5;      // three bytes each in UTF-8
    }
    Huge[599] = 0;
    ExpectSame (ToHost (Huge), Huge, "a name too long comes back as it is");
}

int main (void)
{
    CheckRoundTrip ();
    CheckHighRange ();
    CheckRefusals ();
    CheckLengths ();

    if (s_nFailures > 0)
    {
        printf ("\n%u failure(s)\n", s_nFailures);
        return 1;
    }
    printf ("\nAll passed.\n");
    return 0;
}
