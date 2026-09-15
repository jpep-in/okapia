/*
 * render_pages.cpp — draw the settings, the information pane and a confirmation.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>

#include "okapia_confirm.h"
#include "okapia_info.h"
#include "okapia_newvolume.h"
#include "okapia_screen.h"
#include "okapia_settings.h"
#include "okapia_strings.h"

void SettingsSample (TSettings *p);
void InfoSample (TInfo *p);
void ConfirmSample (TConfirm *p);
void NewVolumeSample (TNewVolume *p);

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

// The focus settles exactly as the firmware settles it before showing anything,
// so a picture taken here is the picture the machine draws.
static void Settle (TSurface *s, unsigned (*Widgets) (TWidget **))
{
    TTheme T;
    ThemeMake (ThemeScaleFor (s->nWidth, s->nHeight), &T);
    TWidget *pW = 0;
    const unsigned n = Widgets (&pW);
    TScreen Screen;
    ScreenInit (&Screen, &T, pW, n);
}

int main (int argc, char **argv)
{
    const char *pStem = argc > 1 ? argv[1] : "page";
    const unsigned nScale = argc > 2 ? (unsigned) atoi (argv[2]) : 1;
    if (argc > 3)
    {
        StringsSetLanguage (StringsFromCode (argv[3]));
    }
    const unsigned w = 640 * nScale, h = 480 * nScale;
    unsigned *px = (unsigned *) calloc ((size_t) w * h, sizeof (unsigned));
    if (px == 0) return 1;
    TSurface s = { (unsigned char *) px, w, h, w * (unsigned) sizeof (unsigned) };

    char Path[256];
    unsigned nAt;

    TSettings Settings;
    SettingsSample (&Settings);
    SettingsDraw (&s, &Settings);
    Settle (&s, SettingsWidgets);
    SettingsRepaint (&s);
    nAt = StrAppend (Path, sizeof Path, 0, pStem);
    StrAppend (Path, sizeof Path, nAt, "-settings.ppm");
    if (!WritePPM (Path, px, w, h)) return 1;
    printf ("%s\n", Path);

    TInfo Info;
    InfoSample (&Info);
    InfoDraw (&s, &Info);
    Settle (&s, InfoWidgets);
    InfoRepaint (&s);
    nAt = StrAppend (Path, sizeof Path, 0, pStem);
    StrAppend (Path, sizeof Path, nAt, "-info.ppm");
    if (!WritePPM (Path, px, w, h)) return 1;
    printf ("%s\n", Path);

    // One of each level. They are a promise about consequences, and the only
    // way to see whether three marks read as three different promises is to put
    // them side by side.
    static const struct { TConfirmLevel Level; const char *pSuffix; } Levels[] =
    {
        { ConfirmNote,    "-confirm-note.ppm"    },
        { ConfirmCaution, "-confirm.ppm"         },
        { ConfirmStop,    "-confirm-stop.ppm"    }
    };
    for (unsigned i = 0; i < sizeof Levels / sizeof Levels[0]; i++)
    {
        TConfirm Confirm;
        ConfirmSample (&Confirm);
        Confirm.Level = Levels[i].Level;
        // The choices on the caution only, which is the alert that has them;
        // the other two show the plain form.
        if (Confirm.Level != ConfirmCaution)
        {
            Confirm.nChoices = 0;
        }
        ConfirmDraw (&s, &Confirm);
        Settle (&s, ConfirmWidgets);
        ConfirmRepaint (&s);
        nAt = StrAppend (Path, sizeof Path, 0, pStem);
        StrAppend (Path, sizeof Path, nAt, Levels[i].pSuffix);
        if (!WritePPM (Path, px, w, h)) return 1;
        printf ("%s\n", Path);
    }

    TNewVolume New;
    NewVolumeSample (&New);
    NewVolumeDraw (&s, &New);
    Settle (&s, NewVolumeWidgets);
    NewVolumeRepaint (&s);
    nAt = StrAppend (Path, sizeof Path, 0, pStem);
    StrAppend (Path, sizeof Path, nAt, "-newvolume.ppm");
    if (!WritePPM (Path, px, w, h)) return 1;
    printf ("%s\n", Path);

    free (px);
    return 0;
}
