//
// kernel_ppc.h — the SheepShaver kernel.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _kernel_ppc_h
#define _kernel_ppc_h

#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/types.h>

#include "hal_circle.h"
#include "okapia_boot.h"

class CKernelPPC
{
public:
    CKernelPPC (void);
    ~CKernelPPC (void);

    bool Initialize (void);
    TOkapiaExit Run (bool bSwitched);

private:
    // The same board the other kernel and the specimen bring up.
};

#endif
