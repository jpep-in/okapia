/*
 * hal_circle.h — the board, as a desktop plays it.
 *
 * Shadows src/hosts/circle/hal_circle.h (this directory comes first on the include
 * path): the same names, kept to what the shared platform files and kernel.cpp
 * call, with plain types. Defined in board_desktop.cpp. Nothing here is a
 * claim on hardware: the host owns its devices.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// The real header's guard as well, so that a file of src/hosts/circle that includes
// it by its quoted name (found beside it first) gets this one instead: the
// desktop's okapia_circle.h, force-included, includes this first.
#ifndef _hal_circle_h
#define _hal_circle_h
#define OKAPIA_HAL_CIRCLE_H

#include "okapia_circle.h"
#include <circle/devicenameservice.h>

class COkapiaBoard
{
public:
    bool Start (const char *pName);
    void StartConsole (void);
    bool StartCard (void);
    bool StartUSB (void);
    void SerialLog (bool) {}
};

COkapiaBoard &OkapiaBoard (void);

typedef void TBoardTickHandler (void);
bool BoardFineTick (unsigned nPeriodUsec, TBoardTickHandler *pHandler);
void BoardWatchClock (bool bReport);
void BoardLogFlush (void);
void BoardOutputShows (unsigned nX, unsigned nY, unsigned nWidth, unsigned nHeight,
                       unsigned nScale);
void BoardSerialLog (bool bOn);
void BoardSerialReplay (void);
class CSoundBaseDevice;
CSoundBaseDevice *BoardSoundClaim (const char *pWhere, unsigned nQueueMsecs);
// bPowerPC: the PowerPC engine is the one running (its restart), which a
// universal System with no engine remembered starts again on.
void BoardChimePlay (bool bPowerPC = false);
void BoardChimeFinish (void);
void BoardChimeStop (void);
void BoardPowerOff (void);

#endif
