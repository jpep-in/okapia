/*
 * check_blit.cpp — les convertisseurs indexés d'upstream, mesurés.
 *
 * video_blit.cpp est le seul code d'affichage qu'Okapia n'écrit pas : le
 * compositeur reçoit ce que ces routines lui donnent, et une palette ignorée
 * ressemble exactement à un bug de compositeur. Elle ne l'est pas — les deux
 * convertisseurs 1 bit écrivaient -(bit), c'est-à-dire du noir et blanc câblé,
 * là où tous leurs voisins lisent ExpandMap (patches/macemu/0003).
 *
 * Ce qui est vérifié ici est la propriété qui rend le bug visible : une palette
 * inversée doit sortir inversée. Un test qui n'emploierait que la palette
 * ordinaire noir-sur-blanc passerait sur le code fautif.
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
    printf ("  %s %s\n", bOK ? "ok  " : "ECHEC", pWhat);
    if (!bOK)
    {
        s_nFailures++;
    }
}

/*
 *  La palette, remplie comme les deux pilotes la remplissent
 *
 *  video_circle.cpp:306 et sheepshaver/video_circle.cpp:404 écrivent les 256
 *  entrées en répétant les couleurs quand le mode en a moins, parce que les
 *  convertisseurs lisent ExpandMap[c >> 6] sans masquer. Reproduire cela ici
 *  n'est pas une commodité : c'est la convention testée.
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
    Visual.fullscreen = true;   // sinon la profondeur 1 prend le chemin X11
    Visual.depth      = nOutputDepth;
    Visual.Rmask      = 0x000000FF;
    Visual.Gmask      = 0x0000FF00;
    Visual.Bmask      = 0x00FF0000;
    Screen_blitter_init (Visual, true, nMacDepth);
    return Screen_blit != 0;
}

/*
 *  1 bit vers 32 : la palette doit arriver
 */

static void CheckOneBitTo32 (void)
{
    printf ("1 bit vers 32\n");

    // Noir et blanc à l'endroit, puis la même chose inversée. C'est le second
    // cas qui distingue une palette lue d'une palette câblée.
    static const uint8 Normal[6]   = { 0x00, 0x00, 0x00,  0xFF, 0xFF, 0xFF };
    static const uint8 Inverted[6] = { 0xFF, 0xFF, 0xFF,  0x00, 0x00, 0x00 };

    Expect (SelectBlitter (1, 32), "un convertisseur est choisi");

    // 0xA5 = 1010 0101, le bit de poids fort d'abord.
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
    Expect (bOK, "chaque bit sort à la couleur que la palette lui donne");

    FillExpandMap (Inverted, 2);
    memset (Out, 0xCC, sizeof Out);
    Screen_blit ((uint8 *) Out, Source, sizeof Source);

    bOK = true;
    for (int i = 0; i < 8; i++)
    {
        bOK = bOK && Out[i] == ExpandMap[Bits[i]];
    }
    Expect (bOK, "et une palette inversée sort inversée");

    // La forme fautive écrivait 0 ou 0xFFFFFFFF : l'alpha du fond y était nul
    // pour la moitié des pixels, ce qui suffit à la reconnaître.
    Expect ((Out[0] & 0xFF000000u) == 0xFF000000u
         && (Out[1] & 0xFF000000u) == 0xFF000000u,
            "l'alpha vient de la palette et non du signe d'un entier");
}

/*
 *  1 bit vers 16, que l'amont n'a pas corrigé
 */

static void CheckOneBitTo16 (void)
{
    printf ("1 bit vers 16\n");

    static const uint8 Palette[6] = { 0xFF, 0xFF, 0xFF,  0x00, 0x00, 0x00 };
    Expect (SelectBlitter (1, 16), "un convertisseur est choisi");

    FillExpandMap (Palette, 2);
    static const uint8 Source[1] = { 0x80 };    // un seul bit, le premier
    uint16 Out[8];
    memset (Out, 0xCC, sizeof Out);
    Screen_blit ((uint8 *) Out, Source, sizeof Source);

    Expect (Out[0] == (uint16) ExpandMap[1] && Out[1] == (uint16) ExpandMap[0],
            "la palette arrive aussi sur seize bits");
}

/*
 *  2 bits vers 32, le voisin qui n'a jamais été faux
 *
 *  Il sert de témoin : si celui-ci échoue, c'est la table ou l'appel qui sont
 *  en cause, pas la correction.
 */

static void CheckTwoBitsTo32 (void)
{
    printf ("2 bits vers 32, témoin\n");

    static const uint8 Palette[12] = { 0x10, 0x11, 0x12,  0x20, 0x21, 0x22,
                                       0x30, 0x31, 0x32,  0x40, 0x41, 0x42 };
    Expect (SelectBlitter (2, 32), "un convertisseur est choisi");

    FillExpandMap (Palette, 4);
    static const uint8 Source[1] = { 0x1B };    // 00 01 10 11
    uint32 Out[4];
    memset (Out, 0xCC, sizeof Out);
    Screen_blit ((uint8 *) Out, Source, sizeof Source);

    Expect (Out[0] == ExpandMap[0] && Out[1] == ExpandMap[1]
         && Out[2] == ExpandMap[2] && Out[3] == ExpandMap[3],
            "les quatre entrées sortent dans l'ordre");
}

int main (void)
{
    CheckOneBitTo32 ();
    CheckOneBitTo16 ();
    CheckTwoBitsTo32 ();

    if (s_nFailures > 0)
    {
        printf ("\n%u échec(s)\n", s_nFailures);
        return 1;
    }
    printf ("\nTout passe.\n");
    return 0;
}
