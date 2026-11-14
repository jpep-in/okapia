/*
 * pages_sample.cpp — the settings, the information pane and a confirmation,
 * filled with values that do not exist.
 *
 * Shared by the renderer and the checks, for the same reason chooser_sample.cpp
 * is: a picture and a measurement of two different models prove less together
 * than either does alone. The values are the awkward ones on purpose — the
 * longest sentence, the fullest card, a model whose origin has to be explained.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "okapia_confirm.h"
#include "okapia_datetime.h"
#include "okapia_info.h"
#include "okapia_network.h"
#include "okapia_newvolume.h"
#include "okapia_settings.h"
#include "okapia_strings.h"
#include "okapia_clock.h"

void DateTimeSample (TDateTime *p);

void SettingsSample (TSettings *p)
{
    p->V.nMemoryMB = 256;
    p->V.nFrameSkip = 0;
    p->V.nSound = SoundJack;
    p->V.nLanguage = (unsigned) StringsLanguage ();
    p->V.bShared = true;
    // Everything but USB, which is what a Pi 3 offers — the awkward case, since
    // it is the one where an entry has to be there and unusable.
    p->nSoundAvailable = (1u << SoundOff) | (1u << SoundHDMI) | (1u << SoundJack);
    StrAppend (p->V.SharedPath, SETTINGS_PATH, 0, "/shared");
    StrAppend (p->V.SharedName, SETTINGS_NAME, 0, "Okapia");
    p->Opened = p->V;
    DateTimeSample (&p->Clock);
}

void InfoSample (TInfo *p)
{
    p->nCount = 0;
    InfoAdd (p, Str (StrInfoVersion), "2026-09-04");
    InfoAdd (p, Str (StrInfoMachine), "Raspberry Pi 3 Model B 1GB (AArch64)");
    InfoAdd (p, Str (StrInfoDisplay), "1280x960, 32 bpp");
    InfoAdd (p, Str (StrInfoRom), "/okapia.rom, 1024 KB, 32-bit clean");

    char Model[INFO_VALUE];
    unsigned n = StrAppend (Model, INFO_VALUE, 0, "Quadra 900 (14), ");
    StrAppend (Model, INFO_VALUE, n, Str (StrInfoModelAuto));
    InfoAdd (p, Str (StrInfoModel), Model);

    InfoAdd (p, Str (StrInfoCard), "1024 MB, 522 MB free");
    InfoAdd (p, Str (StrInfoStartup), "Mac HD 7.6 (/machd76.image)");

    p->nCredits = 0;
    InfoCredit (p, "Okapia",      "GPLv3+ · Jonathan Pepin");
    InfoCredit (p, "Basilisk II", "GPLv2+ · Christian Bauer et al., kanjitalk755");
    InfoCredit (p, "SheepShaver", "GPLv2+ · Christian Bauer et al., kanjitalk755");
    InfoCredit (p, "Circle",      "GPLv3+ · Rene Stange");
    InfoCredit (p, "libhfs",      "GPLv2+ · Robert Leslie");
    InfoCredit (p, "Helvetica",   "X11 · Adobe Systems, Digital Equipment");
}

void ConfirmSample (TConfirm *p)
{
    // The caution, because it is the one with something at stake: the note has
    // nothing to weigh and the stop has nothing to answer.
    p->Level  = ConfirmCaution;
    p->pTitle = Str (StrForgetPramTitle);
    p->pBody  = Str (StrForgetPramBody);
    p->pYes   = Str (StrForgetPram);
    p->pNo    = Str (StrCancel);
    p->pOther = 0;
    // With its row of choices, as the firmware asks it: the longest form of an
    // alert, so the one whose layout has the most to lose.
    p->pChoices[0] = Str (StrForgetPram68k);
    p->pChoices[1] = Str (StrForgetPramPowerpc);
    p->pChoices[2] = Str (StrForgetPramBoth);
    p->nChoices    = 3;
    p->nChoice     = 1;
}

// The shut down alert as the firmware asks it: a note, with Restart between
// Cancel and Shut down.
void ShutDownSample (TConfirm *p)
{
    memset (p, 0, sizeof *p);
    p->Level  = ConfirmNote;
    p->pTitle = Str (StrShutDownTitle);
    p->pBody  = Str (StrShutDownBody);
    p->pYes   = Str (StrShutDown);
    p->pNo    = Str (StrCancel);
    p->pOther = Str (StrRestart);
}

// A card with room for a few volumes but not for every rung: the sizes that do
// not fit are simply absent, which is what makes an impossible one impossible.
void NewVolumeSample (TNewVolume *p)
{
    memset (p, 0, sizeof *p);
    snprintf (p->Name, sizeof p->Name, "%s", "Macintosh HD");
    p->SizeMB[0] = 20;
    p->SizeMB[1] = 40;
    p->SizeMB[2] = 80;
    p->SizeMB[3] = 160;
    p->SizeMB[4] = 320;
    p->nSizes    = 5;
    p->nPick     = 2;
    p->nFreeMB   = 612;
}

// The Wi-Fi joined, a street's worth of networks around it, and the awkward
// ones among them: a name of 32 bytes, a name in accented letters, a hidden
// network that must not show, and the same network heard twice.
void NetworkSample (TNetwork *p)
{
    memset (p, 0, sizeof *p);
    p->V.nKind = NetworkWiFi;
    strcpy (p->V.SSID, "Maison-5G");
    strcpy (p->V.Password, "correct horse battery");
    strcpy (p->V.Country, "FR");
    p->Opened = p->V;
    p->nRunning = NetworkWiFi;
    static const struct { const char *pSSID; int nDbm; bool bSecured; } Heard[] =
    {
        { "Voisin du dessus",                 -71, true  },
        { "Maison-5G",                        -48, true  },
        { "",                                 -60, true  },
        { "Café de la Gare",                  -80, false },
        { "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345", -66, true  },
        { "Maison-5G",                        -58, true  },
        { "Freebox-3A21C9",                   -77, true  },
        { "Imprimante HP",                    -84, false }
    };
    unsigned n = 0;
    for (unsigned i = 0; i < sizeof Heard / sizeof Heard[0]; i++, n++)
    {
        snprintf (p->Seen[n].SSID, sizeof p->Seen[n].SSID, "%s", Heard[i].pSSID);
        p->Seen[n].nDbm     = Heard[i].nDbm;
        p->Seen[n].bSecured = Heard[i].bSecured;
    }
    p->nSeen = NetworkSortSeen (p->Seen, n);
    p->State = NetStateConnected;
    strcpy (p->Subject, "Maison-5G");
    p->nDbm = -48;
}

// Paris, the pool, a Wednesday in September — the longest day name and the
// longest month name in French, so the widest the date can be.
void DateTimeSample (TDateTime *p)
{
    memset (p, 0, sizeof *p);
    p->V.nZone = (unsigned) ClockZoneFind ("paris");
    p->V.bNetTime = true;
    StrAppend (p->V.Server, DATETIME_SERVER, 0, "pool.ntp.org");
    p->Opened = p->V;
    p->nLocal = ClockSecondsOf (2026, 9, 30) + 23 * 3600 + 5 * 60;
    unsigned n = StrAppend (p->Source, sizeof p->Source, 0, Str (StrClockFromNtp));
    StrAppend (p->Source, sizeof p->Source, n, " pool.ntp.org · UTC+2");
}
