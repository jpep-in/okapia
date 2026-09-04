/*
 * pages_sample.cpp — the settings, the information pane and a confirmation,
 * filled with values that do not exist.
 *
 * Shared by the renderer and the checks, for the same reason chooser_sample.cpp
 * is: a picture and a measurement of two different models prove less together
 * than either does alone. The values are the awkward ones on purpose — the
 * longest sentence, the fullest card, a model whose origin has to be explained.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_confirm.h"
#include "okapia_info.h"
#include "okapia_settings.h"
#include "okapia_strings.h"

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
    InfoCredit (p, Str (StrInfoAuthor), "Jonathan Pepin");
    InfoCredit (p, "Okapia",      "GPLv3+");
    InfoCredit (p, "Basilisk II", "GPLv2+ · Christian Bauer et al.");
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
}
