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

#include "okapia_gfx.h"
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

int main (int argc, char **argv)
{
    const char *pOut = argc > 1 ? argv[1] : "specimen.ppm";
    const unsigned nScale = argc > 2 ? (unsigned) atoi (argv[2]) : 1;
    const unsigned nPage = argc > 3 ? (unsigned) atoi (argv[3]) : 0;
    if (nScale == 0 || nScale > 8)
    {
        fprintf (stderr, "scale must be 1..8\n");
        return 1;
    }

    // The surface is the output itself: there is no canvas to magnify any more,
    // the interface is composed for whatever size it is handed.
    const unsigned nWidth  = SPECIMEN_WIDTH  * nScale;
    const unsigned nHeight = SPECIMEN_HEIGHT * nScale;
    unsigned *pPixels = (unsigned *) calloc ((size_t) nWidth * nHeight, sizeof (unsigned));
    if (pPixels == 0)
    {
        return 1;
    }
    TSurface Surface = { (unsigned char *) pPixels, nWidth, nHeight,
                         nWidth * (unsigned) sizeof (unsigned) };

    SpecimenDraw (&Surface, nPage);

    const bool bOK = WritePPM (pOut, pPixels, nWidth, nHeight);
    if (bOK)
    {
        printf ("%s: %ux%u, page %u, échelle %u/16\n", pOut, nWidth, nHeight, nPage,
                ThemeScaleFor (nWidth, nHeight));
    }

    free (pPixels);
    return bOK ? 0 : 1;
}
