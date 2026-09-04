//
// kernel.h — Okapia: the Circle kernel that hosts the Macintosh.
//
// Initialisation order is not a matter of taste here:
//   1. serial, so every later failure can say so
//   2. the SD card, because the preferences live on it and they say how much
//      Mac RAM to allocate. Nothing else is initialised before it, and the free
//      heap it leaves behind is logged: measured at 935 MB on a 1 GB board,
//      the same figure as when this was read before the card came up
//   3. the Mac memory block, before any *other* driver fragments the heap
//   4. drivers, input
//   5. the emulator
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_kernel_h
#define _okapia_kernel_h

#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/types.h>

#include "hal_circle.h"

enum TShutdownMode
{
    ShutdownNone,
    ShutdownHalt,
    ShutdownReboot
};

class CKernel
{
public:
    CKernel (void);
    ~CKernel (void);

    bool Initialize (void);
    TShutdownMode Run (void);

private:
    void LoadPreferences (void);
    void ApplyTimeZone (void);
    void RefineClock (void);
    void ReportCardContents (void);
    bool PrepareVolumes (void);
    void ApplyModelId (void);
    void PrepareSharedFolder (void);
    void LoadKeycodes (void);
    bool StartMacintosh (void);

    // The Raspberry Pi itself, brought up once and in the right order, shared
    // with the specimen kernel and with whatever engine comes next.
    COkapiaBoard       m_Board;
};

#endif
