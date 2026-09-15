/*
 * render_specimen.cpp — draw the firmware's specimen screen on a development
 * machine and write it out as an image.
 *
 * This is why the drawing primitives take a plain surface instead of a Circle
 * object: the whole interface can be looked at, and compared from one version
 * to the next, without building a kernel or booting anything. It also exercises
 * the presentation path, since a magnified canvas is what anyone will actually
 * see and it is where an off-by-one shows up first.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "okapia_gfx.h"
#include "okapia_screen.h"
#include "okapia_specimen.h"
#include "okapia_theme.h"

static bool WritePPM (const char *pPath, const unsigned *pPixels,
                      unsigned nWidth, unsigned nHeight)
{
    FILE *f = fopen (pPath, "wb");
    if (f == 0)
    {
        fprintf (stderr, "cannot write %s\n", pPath);
        return false;
    }
    fprintf (f, "P6\n%u %u\n255\n", nWidth, nHeight);
    for (unsigned i = 0; i < nWidth * nHeight; i++)
    {
        const unsigned nRGB = pPixels[i];
        const unsigned char Triple[3] =
        {
            (unsigned char) (nRGB >> 16), (unsigned char) (nRGB >> 8), (unsigned char) nRGB
        };
        fwrite (Triple, 1, 3, f);
    }
    fclose (f);
    return true;
}

// One page into a file. Named as the caller asked for the first, and with the
// page number appended for the rest — which is what specimen.sh compares its
// captures against.
static bool RenderPage (const char *pOut, unsigned nScale, unsigned nPage);

int main (int argc, char **argv)
{
    const char *pOut = argc > 1 ? argv[1] : "specimen.ppm";
    const unsigned nScale = argc > 2 ? (unsigned) atoi (argv[2]) : 1;
    if (nScale == 0 || nScale > 8)
    {
        fprintf (stderr, "scale must be 1..8\n");
        return 1;
    }

    // With a page named, that page. Without, every page there is — the count
    // being the flow's to decide, not this file's to know.
    if (argc > 3)
    {
        return RenderPage (pOut, nScale, (unsigned) atoi (argv[3])) ? 0 : 1;
    }

    unsigned nPixels[1];
    TSurface Probe = { (unsigned char *) nPixels, SPECIMEN_WIDTH * nScale,
                       SPECIMEN_HEIGHT * nScale, 0 };
    const unsigned nPages = SpecimenPageCount (&Probe);
    for (unsigned i = 0; i < nPages; i++)
    {
        char Path[256];
        if (i == 0)
        {
            snprintf (Path, sizeof Path, "%s", pOut);
        }
        else
        {
            // "specimen.ppm" becomes "specimen-2.ppm": the extension is put
            // back rather than assumed, so a caller may name the file anything.
            const char *pDot = strrchr (pOut, '.');
            const int nStem = pDot != 0 ? (int) (pDot - pOut) : (int) strlen (pOut);
            snprintf (Path, sizeof Path, "%.*s-%u%s", nStem, pOut, i + 1,
                      pDot != 0 ? pDot : "");
        }
        if (!RenderPage (Path, nScale, i))
        {
            return 1;
        }
    }
    return 0;
}

static bool RenderPage (const char *pOut, unsigned nScale, unsigned nPage)
{

    // The surface is the output itself: there is no canvas to magnify any more,
    // the interface is composed for whatever size it is handed.
    const unsigned nWidth  = SPECIMEN_WIDTH  * nScale;
    const unsigned nHeight = SPECIMEN_HEIGHT * nScale;
    unsigned *pPixels = (unsigned *) calloc ((size_t) nWidth * nHeight, sizeof (unsigned));
    if (pPixels == 0)
    {
        return false;
    }
    TSurface Surface = { (unsigned char *) pPixels, nWidth, nHeight,
                         nWidth * (unsigned) sizeof (unsigned) };

    SpecimenDraw (&Surface, nPage);

    // And then the loop settles the focus, exactly as the kernel does before it
    // shows anything. Without this the picture is the layout alone, and a page
    // that declares no focus of its own comes out with none — which is not what
    // anyone sees, and made the QEMU capture and this render disagree for a
    // reason that was neither one's fault.
    TTheme Theme;
    ThemeMake (ThemeScaleFor (nWidth, nHeight), &Theme);
    TWidget *pWidgets = 0;
    const unsigned nCount = SpecimenWidgets (&pWidgets);
    TScreen Screen;
    ScreenInit (&Screen, &Theme, pWidgets, nCount);
    SpecimenRepaint (&Surface);

    const bool bOK = WritePPM (pOut, pPixels, nWidth, nHeight);
    if (bOK)
    {
        printf ("%s: %ux%u, page %u, scale %u/16\n", pOut, nWidth, nHeight, nPage,
                ThemeScaleFor (nWidth, nHeight));
    }

    free (pPixels);
    return bOK;
}
