/*
 * check_blit.cpp — upstream's indexed converters, measured.
 *
 * video_blit.cpp is the only display code Okapia does not write: the
 * compositor receives what these routines hand it, and an ignored palette
 * looks exactly like a compositor bug. It is not one — the two 1-bit
 * converters wrote -(bit), that is hard-wired black and white, where all
 * their neighbours read ExpandMap (patches/macemu/0003).
 *
 * What is checked here is the property that makes the bug visible: an
 * inverted palette must come out inverted. A test using only the ordinary
 * black-on-white palette would pass on the faulty code.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "sysdeps.h"
#include "video.h"
#include "video_blit.h"

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s %s\n", bOK ? "ok  " : "FAIL ", pWhat);
    if (!bOK)
    {
        s_nFailures++;
    }
}

/*
 *  The palette, filled the way both drivers fill it
 *
 *  video_circle.cpp:306 and sheepshaver/video_circle.cpp:404 write all 256
 *  entries, repeating the colours when the mode has fewer, because the
 *  converters read ExpandMap[c >> 6] without masking. Reproducing that here is
 *  not a convenience: it is the convention under test.
 */

static void FillExpandMap (const uint8 *pPalette, int nColours)
{
    for (int i = 0; i < 256; i++)
    {
        int c = i & (nColours - 1);
        ExpandMap[i] = 0xFF000000u
                     |  (uint32) pPalette[c * 3 + 0]
                     | ((uint32) pPalette[c * 3 + 1] << 8)
                     | ((uint32) pPalette[c * 3 + 2] << 16);
    }
}

static bool SelectBlitter (int nMacDepth, int nOutputDepth)
{
    VisualFormat Visual;
    memset (&Visual, 0, sizeof Visual);
    Visual.fullscreen = true;   // otherwise depth 1 takes the X11 path
    Visual.depth      = nOutputDepth;
    Visual.Rmask      = 0x000000FF;
    Visual.Gmask      = 0x0000FF00;
    Visual.Bmask      = 0x00FF0000;
    Screen_blitter_init (Visual, true, nMacDepth);
    return Screen_blit != 0;
}

/*
 *  1 bit to 32: the palette must get through
 */

static void CheckOneBitTo32 (void)
{
    printf ("1 bit to 32\n");

    // Black and white the right way up, then the same inverted. The second
    // case is what tells a palette that is read from one hard-wired.
    static const uint8 Normal[6]   = { 0x00, 0x00, 0x00,  0xFF, 0xFF, 0xFF };
    static const uint8 Inverted[6] = { 0xFF, 0xFF, 0xFF,  0x00, 0x00, 0x00 };

    Expect (SelectBlitter (1, 32), "a converter is chosen");

    // 0xA5 = 1010 0101, most significant bit first.
    static const uint8 Source[1] = { 0xA5 };
    static const int   Bits[8]   = { 1, 0, 1, 0, 0, 1, 0, 1 };
    uint32 Out[8];

    FillExpandMap (Normal, 2);
    memset (Out, 0xCC, sizeof Out);
    Screen_blit ((uint8 *) Out, Source, sizeof Source);

    bool bOK = true;
    for (int i = 0; i < 8; i++)
    {
        bOK = bOK && Out[i] == ExpandMap[Bits[i]];
    }
    Expect (bOK, "each bit comes out in the colour the palette gives it");

    FillExpandMap (Inverted, 2);
    memset (Out, 0xCC, sizeof Out);
    Screen_blit ((uint8 *) Out, Source, sizeof Source);

    bOK = true;
    for (int i = 0; i < 8; i++)
    {
        bOK = bOK && Out[i] == ExpandMap[Bits[i]];
    }
    Expect (bOK, "and an inverted palette comes out inverted");

    // The faulty form wrote 0 or 0xFFFFFFFF: the alpha was zero for half the
    // pixels, which is enough to recognise it.
    Expect ((Out[0] & 0xFF000000u) == 0xFF000000u
         && (Out[1] & 0xFF000000u) == 0xFF000000u,
            "the alpha comes from the palette, not from the sign of an integer");
}

/*
 *  1 bit to 16, which upstream did not fix
 */

static void CheckOneBitTo16 (void)
{
    printf ("1 bit to 16\n");

    static const uint8 Palette[6] = { 0xFF, 0xFF, 0xFF,  0x00, 0x00, 0x00 };
    Expect (SelectBlitter (1, 16), "a converter is chosen");

    FillExpandMap (Palette, 2);
    static const uint8 Source[1] = { 0x80 };    // a single bit, the first
    uint16 Out[8];
    memset (Out, 0xCC, sizeof Out);
    Screen_blit ((uint8 *) Out, Source, sizeof Source);

    Expect (Out[0] == (uint16) ExpandMap[1] && Out[1] == (uint16) ExpandMap[0],
            "the palette gets through at sixteen bits too");
}

/*
 *  2 bits to 32, the neighbour that was never wrong
 *
 *  It is the control: if this one fails, the table or the call is at fault,
 *  not the fix.
 */

static void CheckTwoBitsTo32 (void)
{
    printf ("2 bits to 32, control\n");

    static const uint8 Palette[12] = { 0x10, 0x11, 0x12,  0x20, 0x21, 0x22,
                                       0x30, 0x31, 0x32,  0x40, 0x41, 0x42 };
    Expect (SelectBlitter (2, 32), "a converter is chosen");

    FillExpandMap (Palette, 4);
    static const uint8 Source[1] = { 0x1B };    // 00 01 10 11
    uint32 Out[4];
    memset (Out, 0xCC, sizeof Out);
    Screen_blit ((uint8 *) Out, Source, sizeof Source);

    Expect (Out[0] == ExpandMap[0] && Out[1] == ExpandMap[1]
         && Out[2] == ExpandMap[2] && Out[3] == ExpandMap[3],
            "the four entries come out in order");
}

int main (void)
{
    CheckOneBitTo32 ();
    CheckOneBitTo16 ();
    CheckTwoBitsTo32 ();

    if (s_nFailures > 0)
    {
        printf ("\n%u failure(s)\n", s_nFailures);
        return 1;
    }
    printf ("\nAll passed.\n");
    return 0;
}
