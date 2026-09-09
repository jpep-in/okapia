/*
 * render_chooser.cpp — draw the boot chooser on a development machine.
 *
 * The screen knows nothing about HFS, so a made-up card is all it takes to look
 * at it — and a made-up card can hold the awkward cases a real one rarely does:
 * a volume with no System, one left in use, one that is not mounted.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "okapia_chooser.h"

void ChooserSample (TChooser *p);
#include "okapia_screen.h"
#include "okapia_strings.h"

static bool WritePPM (const char *pPath, const unsigned *pPixels, unsigned w, unsigned h)
{
    FILE *f = fopen (pPath, "wb");
    if (f == 0) return false;
    fprintf (f, "P6\n%u %u\n255\n", w, h);
    for (unsigned i = 0; i < w * h; i++)
    {
        const unsigned c = pPixels[i];
        const unsigned char t[3] = { (unsigned char)(c>>16), (unsigned char)(c>>8),
                                     (unsigned char) c };
        fwrite (t, 1, 3, f);
    }
    fclose (f);
    return true;
}

int main (int argc, char **argv)
{
    const char *pOut = argc > 1 ? argv[1] : "chooser.ppm";
    const unsigned nScale = argc > 2 ? (unsigned) atoi (argv[2]) : 1;
    if (argc > 3)
    {
        StringsSetLanguage (StringsFromCode (argv[3]));
    }
    const unsigned w = 640 * nScale, h = 480 * nScale;
    unsigned *px = (unsigned *) calloc ((size_t) w * h, sizeof (unsigned));
    if (px == 0) return 1;
    TSurface s = { (unsigned char *) px, w, h, w * (unsigned) sizeof (unsigned) };

    TChooser Model;
    ChooserSample (&Model);
    // Which row to show selected. The emulator popup speaks about it, so a
    // picture of the screen with a 68k volume selected never shows the popup
    // live — and that is the half worth looking at.
    if (argc > 4)
    {
        Model.nStartup = atoi (argv[4]);
    }
    ChooserDraw (&s, &Model);

    // And the loop settles the focus, exactly as the firmware does before it
    // shows anything.
    TTheme T;
    ThemeMake (ThemeScaleFor (w, h), &T);
    TWidget *pW = 0;
    const unsigned n = ChooserWidgets (&pW);
    TScreen Screen;
    ScreenInit (&Screen, &T, pW, n);
    // Scrolled to the selection, as the loop does the moment anything is
    // operated: a picture of a chooser whose selected row is off the bottom is
    // a picture of the wrong thing.
    for (unsigned i = 0; i < n; i++)
    {
        if (pW[i].Type == WidgetList)
        {
            WidgetListReveal (&pW[i], &T);
        }
    }
    ChooserRepaint (&s);

    const bool bOK = WritePPM (pOut, px, w, h);
    if (bOK)
    {
        printf ("%s: %ux%u [%s], %u volumes\n", pOut, w, h,
                StringsCode (StringsLanguage ()), Model.nCount);
    }
    free (px);
    return bOK ? 0 : 1;
}
