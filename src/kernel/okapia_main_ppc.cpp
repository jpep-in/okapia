//
// okapia_main_ppc.cpp — Okapia entry point, SheepShaver engine.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "kernel_ppc.h"
#include <circle/startup.h>

int main (void)
{
    // No return: some destructors used by the kernel are not implemented.
    CKernelPPC Kernel;
    if (!Kernel.Initialize ())
    {
        halt ();
        return EXIT_HALT;
    }

    switch (Kernel.Run ())
    {
    case ShutdownReboot:
        reboot ();
        return EXIT_REBOOT;

    case ShutdownHalt:
    default:
        halt ();
        return EXIT_HALT;
    }
}
