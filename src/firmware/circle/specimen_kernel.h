//
// specimen_kernel.h — a kernel that does nothing but show the interface.
//
// Separate from the Okapia kernel on purpose. It links Circle and the firmware's
// drawing code and nothing else: no Basilisk core, no SD card, no emulator. That
// makes it build in seconds instead of minutes, which is what a design one is
// still deciding needs, and it means looking at the theme costs neither a card
// nor a risk to one.
//
// It carries a USB host, because a specimen one cannot tab through is a picture
// of an interface rather than an interface: the focus ring, the pressed state
// and the pointer only mean anything under someone's hands.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_specimen_kernel_h
#define _okapia_specimen_kernel_h

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

class CSpecimenKernel
{
public:
    CSpecimenKernel (void);

    bool Initialize (void);
    TShutdownMode Run (void);

private:
    // The same board the Macintosh kernel brings up, and the reason this one
    // exists: the firmware has to be shown to run with no emulator behind it.
    COkapiaBoard       m_Board;
};

#endif
