//
// specimen_main.cpp — entry point of the theme specimen kernel.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "specimen_kernel.h"
#include <circle/startup.h>

int main (void)
{
    CSpecimenKernel Kernel;
    if (!Kernel.Initialize ())
    {
        halt ();
        return EXIT_HALT;
    }

    Kernel.Run ();
    halt ();
    return EXIT_HALT;
}
