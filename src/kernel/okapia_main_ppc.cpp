//
// okapia_main_ppc.cpp — entering the PowerPC Macintosh.
//
// The SheepShaver engine's one exported name; see okapia_main.cpp for why there
// is exactly one.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "kernel_ppc.h"
#include "okapia_boot.h"

extern "C" TOkapiaExit OkapiaRunPowerPC (int bSwitched)
{
    static CKernelPPC *s_pKernel;

    if (s_pKernel == 0)
    {
        s_pKernel = new CKernelPPC;
        if (s_pKernel == 0 || !s_pKernel->Initialize ())
        {
            return OkapiaHalt;
        }
    }

    return s_pKernel->Run (bSwitched != 0);
}
