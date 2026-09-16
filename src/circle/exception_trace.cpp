/*
 * exception_trace.cpp — report 68k exceptions, without patching external/.
 *
 * Exception() in newcpu.cpp is silent, and a guest that stalls is exactly the
 * case where a bus or address error would explain everything.
 *
 * We used to add a call to OkapiaReportException() inside newcpu.cpp, which
 * meant carrying a patch against upstream. The linker can do it instead: build
 * with --wrap=SYM and every call to SYM coming from another translation unit is
 * routed to __wrap_SYM, while __real_SYM still reaches the original. The cost is
 * that the hook must be spelled exactly __wrap_ + the mangled symbol, which is
 * unreadable — so that spelling buys nothing but the hook, and the actual work
 * stays in a function with a name a human chose.
 *
 * Calls made inside newcpu.cpp itself are not wrapped, so counts are a floor,
 * not a census. Guest faults come from the generated cpuemu*.o handlers, which
 * are wrapped.
 *
 * Off unless src/kernel/Makefile passes OKAPIA_TRACE=1.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "sysdeps.h"
#include "okapia_circle.h"

static void OkapiaReportException (int nr, unsigned long pc)
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
                                "exception %d from PC 0x%08lX (%u so far)",
                                nr, pc, s_nCount[nr]);
    }
}

// Exception(int nr, uaecptr oldpc), mangled. Nothing here but the adapter.
extern "C" void __real__Z9Exceptionij (int nr, unsigned int oldpc);

extern "C" void __wrap__Z9Exceptionij (int nr, unsigned int oldpc)
{
    OkapiaReportException (nr, (unsigned long) oldpc);
    __real__Z9Exceptionij (nr, oldpc);
}
