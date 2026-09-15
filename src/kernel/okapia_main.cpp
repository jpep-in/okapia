//
// okapia_main.cpp — entering the 68k Macintosh.
//
// No longer main(): one image carries both emulators and okapia_boot.cpp owns
// the entry point. This is the 68k engine's one exported name, and the build
// renames everything else the other engine defines so that the two cores — which
// export the same InitAll, ExitAll, PatchROM and Execute68k — can share an
// image (docs/project/architecture.md).
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "kernel.h"
#include "okapia_boot.h"

extern "C" TOkapiaExit OkapiaRun68k (int bSwitched)
{
    // Built with new and never deleted: some of the kernel's members have no
    // destructor worth running, and a Macintosh that has stopped has stopped.
    // Kept across calls, because this engine may be come back to — the other
    // one hands the board back the same way it took it — and a second CKernel
    // would re-run everything that is only allowed once.
    static CKernel *s_pKernel;

    if (s_pKernel == 0)
    {
        s_pKernel = new CKernel;
        if (s_pKernel == 0 || !s_pKernel->Initialize ())
        {
            return OkapiaHalt;
        }
    }

    return s_pKernel->Run (bSwitched != 0);
}
