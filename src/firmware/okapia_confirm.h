/*
 * okapia_confirm.h — an alert with a question and two answers.
 *
 * Shutting down and forgetting the parameter RAM both go through here, and the
 * repair dialogue of 16i will too. They differ by their words and by nothing
 * else, which is exactly the case for one screen and not three.
 *
 * The caution mark and the sentence do the interrupting. The frame does not:
 * the two rule weights are a constant of the design and an alert wears the same
 * ones a dialogue does, because a frame that is nearly the dialogue's reads as
 * a mistake rather than as emphasis.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_confirm_h
#define _okapia_confirm_h

#include "okapia_widgets.h"

// The three levels the Macintosh had, and they are worth keeping: they are a
// promise about consequences, not a decoration. Answering with the wrong one
// teaches people to click past all three.
enum TConfirmLevel
{
    ConfirmNote,                        // it happened; nothing is at stake
    ConfirmCaution,                     // this may cost you something
    ConfirmStop                         // it cannot go on
};

struct TConfirm
{
    TConfirmLevel Level;
    const char *pTitle;
    const char *pBody;
    const char *pYes;                   // the default button, on the right
    const char *pNo;                    // or 0 for a message with nothing to refuse
};

enum TConfirmAnswer
{
    ConfirmWaiting,
    ConfirmYes,
    ConfirmNo
};

void ConfirmDraw (TSurface *pSurface, const TConfirm *pConfirm);
void ConfirmRepaint (TSurface *pSurface);
unsigned ConfirmWidgets (TWidget **ppList);

// The frame the last layout drew, so a test can check that nothing strayed
// into its border. Every element spilling out was invisible in the numbers
// until the numbers were asked for.
TRect ConfirmDialog (void);
TConfirmAnswer ConfirmOperate (int nIndex);

#endif
