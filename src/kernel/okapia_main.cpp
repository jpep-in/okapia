//
// main.cpp — Okapia entry point.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "kernel.h"
#include <circle/startup.h>

int main (void)
{
    // No return: some destructors used by CKernel are not implemented.
    CKernel Kernel;
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
