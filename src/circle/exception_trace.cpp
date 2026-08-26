/*
 * exception_trace.cpp — report 68k exceptions while bringing the boot up.
 *
 * Exception() in newcpu.cpp is silent. A guest that stops right after loading
 * its System file is exactly the case where a bus or address error would explain
 * everything, so this counts them and prints the first few.
 *
 * Diagnostic scaffolding: remove once the boot completes.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "sysdeps.h"
#include "okapia_circle.h"

extern "C" void OkapiaReportException (int nr, unsigned long pc)
{
    static unsigned s_nCount[64];
    if (nr < 0 || nr >= 64)
    {
        return;
    }

    // 2 = bus error, 3 = address error, 4 = illegal instruction, 8 = privilege.
    // The Mac raises some of these routinely; what matters is a flood.
    if (++s_nCount[nr] <= 3 || (s_nCount[nr] % 1000) == 0)
    {
        CLogger::Get ()->Write ("okapia-68k", LogNotice,
                                "exception %d at PC 0x%08lX (%u so far)",
                                nr, pc, s_nCount[nr]);
    }
}
