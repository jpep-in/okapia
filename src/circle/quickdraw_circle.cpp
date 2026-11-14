/*
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "okapia_circle.h"
#include "prefs.h"
#include "quickdraw_blit.h"
#include "quickdraw_circle.h"
#include "quickdraw_worker.h"
#ifndef OKAPIA_JIT
#include "m68k.h"
#include "readcpu.h"
#include "newcpu.h"
#endif

static const char FROM[] = "quickdraw";
static constexpr uint32 BITBLT = 0xab00;
static constexpr uint32 BITBLT_ROM = 0x2c870;
static constexpr uint32 BITBLT_RETURN = 0x36e40;
static constexpr uint32 BITBLT_CALL = BITBLT_RETURN - 6;
static constexpr uint32 RGNBLT = 0xab1f;
static constexpr uint32 RGNBLT_ROM = 0x2a6e0;
static constexpr uint32 RGNBLT_RETURN = 0x36e62;
static constexpr uint32 STRETCHBITS = 0xab27;
static constexpr uint32 DRAWSLAB = 0xab0b;
static constexpr uint32 DRAWSLAB_ROM = 0x2f160;
static constexpr uint32 SCALEBLT = 0xab96;
static constexpr uint32 SCALEBLT_ROM = 0x39dd0;
static constexpr uint32 SCALEBLT_RETURN = 0x36e0e;
static constexpr uint32 ST_SCAN_LOOP = 0xab99;
static constexpr uint32 SCAN_ROM = 0x374d0;
static constexpr uint32 STRETCH_FINISH = 0x37934;
static constexpr unsigned SYSTEM_RETURN_OFFSET = 0x6de;
static constexpr unsigned SYSTEM_REGION_RETURN_OFFSET = 0x712;
static constexpr unsigned SYSTEM_SCALE_RETURN_OFFSET = 0x680;
static constexpr unsigned SYSTEM_SCAN_OFFSET = 0x1084;
static constexpr unsigned SYSTEM_FINISH_OFFSET = 0x140a;
static constexpr uint32 THUNK_OPCODE_OFFSET = 4;
static constexpr unsigned THUNK_SAVED_BYTES = 6;

static QDMemory s_memory;
static uint32 s_thunk;
static uint32 s_regionThunk;
static uint32 s_slabThunk;
static uint32 s_scaleThunk, s_scanThunk, s_scanCaller;
static bool s_installAttempted;
static bool s_parallel;
static bool s_report;
static bool s_probe;
static uint32 s_systemReturn;
static uint32 s_systemRegionReturn;
static uint32 s_systemScaleReturn;
static unsigned s_regionCalls, s_regionNative;
static unsigned s_slabCalls, s_slabNative;
static unsigned s_scaleCalls, s_scaleNative, s_scanCalls, s_scanNative;
static unsigned s_calls, s_romCalls, s_eligible, s_completed, s_split, s_usecs, s_lastReport;
static unsigned s_copies, s_fills, s_overlaps;
static uint64_t s_bytes;

struct RejectedBlits
{
    unsigned count;
    unsigned sourceDepth, destinationDepth;
    int mode;
    unsigned colorize;
};

static RejectedBlits s_rejected[unsigned (QDBlitReject::Count)];
static const char *const s_rejectNames[] = {
    "caller", "frame", "depth", "mode", "colorize", "alpha",
    "pixmap", "rectangle", "pixel bounds", "pattern", "overlap"
};
static_assert (sizeof s_rejectNames / sizeof *s_rejectNames == unsigned (QDBlitReject::Count),
               "BitBlt rejection names must match QDBlitReject");

#ifndef OKAPIA_JIT
static void RecordRejection (const M68kRegisters &regs, const QDBlitDiagnostic &diagnostic)
{
    auto &sample = s_rejected[unsigned (diagnostic.reason)];
    if (sample.count++ == 0)
    {
        const unsigned src = uint16_t (regs.d[3]);
        const unsigned dst = uint16_t (regs.d[4]);
        sample.sourceDepth = src <= 5 ? 1u << src : 0;
        sample.destinationDepth = dst <= 5 ? 1u << dst : 0;
        sample.mode = diagnostic.mode;
        sample.colorize = diagnostic.colorize;
    }
}
#endif

void QDSetScreen (unsigned char *pixels, unsigned size)
{
    s_memory.screen = {pixels ? Host2MacAddr (pixels) : 0, size, pixels};
}

void QDReset ()
{
    s_thunk = 0;
    s_regionThunk = 0;
    s_slabThunk = 0;
    s_scaleThunk = s_scanThunk = s_scanCaller = 0;
    s_installAttempted = false;
    s_report = s_probe = s_parallel = false;
    s_systemReturn = 0;
    s_systemRegionReturn = 0;
    s_systemScaleReturn = 0;
    s_regionCalls = s_regionNative = 0;
    s_slabCalls = s_slabNative = 0;
    s_scaleCalls = s_scaleNative = s_scanCalls = s_scanNative = 0;
    s_calls = s_romCalls = s_eligible = s_completed = s_split = s_usecs = 0;
    s_copies = s_fills = s_overlaps = 0;
    s_lastReport = 0;
    s_bytes = 0;
    memset (s_rejected, 0, sizeof s_rejected);
}

static bool MatchesCode (uint32 address, unsigned length, uint32 expected)
{
    const auto *code = !(address & 1) ? s_memory.ram.Map (address, length) : nullptr;
    if (!code)
        return false;
    uint32 signature = 2166136261u;
    for (unsigned i = 0; i < length; ++i)
        signature = (signature ^ code[i]) * 16777619u;
    return signature == expected;
}

static uint32 AllocateThunk (const uint16 *code, unsigned words)
{
    M68kRegisters regs = {};
    regs.d[0] = words * 2;
    Execute68kTrap (0xa71e, &regs);  // NewPtrSysClear
    const uint32 addr = regs.a[0];
    if (!addr || !s_memory.ram.Map (addr, words * 2))
        return 0;
    for (unsigned i = 0; i < words; ++i)
        WriteMacInt16 (addr + 2 * i, code[i]);
    FlushCodeCache (Mac2HostAddr (addr), words * 2);
    return addr;
}

static uint32 BuildThunk (uint32 original, uint32 finish, bool returnSuccess)
{
    // Preserve the inputs on fallback. ScaleBlt returns success in D0,
    // and stScanLoop transfers to the guest cleanup to restore its stack.
    if (finish)
    {
        const uint16 code[] = {
            0x42e7, 0x2f00, QD_EMUL_OP, 0x4a00, 0x670a,
            0x201f, 0x44df, 0x4ef9, uint16 (finish >> 16), uint16 (finish),
            0x201f, 0x44df, 0x4ef9, uint16 (original >> 16), uint16 (original)
        };
        return AllocateThunk (code, sizeof code / sizeof *code);
    }
    const uint16 code[] = {
        0x42e7, 0x2f00, QD_EMUL_OP, 0x4a00, 0x6706,
        uint16 (returnSuccess ? 0x588f : 0x201f), 0x44df, 0x4e75,
        0x201f, 0x44df, 0x4ef9, uint16 (original >> 16), uint16 (original)
    };
    return AllocateThunk (code, sizeof code / sizeof *code);
}

static uint32 InstallThunk (uint32 trap, uint32 original, uint32 finish = 0, bool returnSuccess = false)
{
    const uint32 addr = BuildThunk (original, finish, returnSuccess);
    if (!addr)
        return 0;
    M68kRegisters regs = {};
    regs.d[0] = trap;
    regs.a[0] = addr;
    Execute68kTrap (0xa647, &regs);  // SetToolTrapAddress
    return addr;
}

void QDInstall ()
{
    if (s_installAttempted)
        return;
    s_installAttempted = true;
#ifdef OKAPIA_JIT
    return;
#endif
    const char *mode = PrefsFindString ("qdaccel");
    if (!mode)
        return;
    s_parallel = strcmp (mode, "parallel") == 0;
    s_probe = strcmp (mode, "probe") == 0;
    if (!s_parallel && !s_probe && strcmp (mode, "sync") != 0)
        return;
    s_report = s_probe || PrefsFindBool ("perfreport");

    if (TwentyFourBitAddressing || ROMSize != 0x100000 ||
        ReadMacInt32 (ROMBaseMac) != 0xf1acad13 ||
        ReadMacInt32 (ROMBaseMac + BITBLT_ROM) != 0x41eefd98 ||
        ReadMacInt32 (ROMBaseMac + BITBLT_CALL) != 0x4eb081e1 ||
        ReadMacInt16 (ROMBaseMac + BITBLT_CALL + 4) != 0x1a00)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "BitBlt disabled: unsupported ROM or addressing");
        return;
    }

    M68kRegisters regs = {};
    regs.d[0] = BITBLT;
    Execute68kTrap (0xa746, &regs);  // GetToolTrapAddress
    const uint32 original = regs.a[0];
    if (original != ROMBaseMac + BITBLT_ROM)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "BitBlt disabled: existing handler %08x", original);
        return;
    }

    s_memory.ram = {RAMBaseMac, RAMSize, RAMBaseHost};
    // System 7.1 and 7.6.1 install the same StretchBits patch at different
    // heap addresses. Fingerprint the entry through the BitBlt return branch,
    // including the frame setup. Guest code with another ABI falls back.
    regs.d[0] = STRETCHBITS;
    Execute68kTrap (0xa746, &regs);
    const uint32 stretch = regs.a[0];
    if (MatchesCode (stretch, SYSTEM_RETURN_OFFSET + 4, 0xf86789bb))
        s_systemReturn = stretch + SYSTEM_RETURN_OFFSET;
    if (MatchesCode (stretch, SYSTEM_REGION_RETURN_OFFSET + 4, 0x69fc08f9))
        s_systemRegionReturn = stretch + SYSTEM_REGION_RETURN_OFFSET;
    if (MatchesCode (stretch, SYSTEM_SCALE_RETURN_OFFSET + 4, 0xa145471c))
        s_systemScaleReturn = stretch + SYSTEM_SCALE_RETURN_OFFSET;

    s_thunk = InstallThunk (BITBLT, original);
    if (!s_thunk)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "BitBlt disabled: no system heap for the trampoline");
        return;
    }
    CLogger::Get ()->Write (FROM, LogNotice,
                            "BitBlt installed (%s, StretchBits %08x, verified RAM return %08x)",
                            mode, stretch, s_systemReturn);

    regs.d[0] = RGNBLT;
    Execute68kTrap (0xa746, &regs);
    const uint32 region = regs.a[0];
    if (region == ROMBaseMac + RGNBLT_ROM || MatchesCode (region, 4096, 0xde7cb520))
        s_regionThunk = InstallThunk (RGNBLT, region);
    if (s_regionThunk)
        CLogger::Get ()->Write (FROM, LogNotice, "RgnBlt installed (%s, handler %08x)", mode, region);

    regs.d[0] = DRAWSLAB;
    Execute68kTrap (0xa746, &regs);
    if (regs.a[0] == ROMBaseMac + DRAWSLAB_ROM)
        s_slabThunk = InstallThunk (DRAWSLAB, regs.a[0]);

    regs.d[0] = SCALEBLT;
    Execute68kTrap (0xa746, &regs);
    if (regs.a[0] == ROMBaseMac + SCALEBLT_ROM)
        s_scaleThunk = InstallThunk (SCALEBLT, regs.a[0], 0, true);
    if (s_scaleThunk)
        CLogger::Get ()->Write (FROM, LogNotice, "ScaleBlt installed (%s)", mode);

    regs.d[0] = ST_SCAN_LOOP;
    Execute68kTrap (0xa746, &regs);
    const uint32 scan = regs.a[0];
    if (scan == ROMBaseMac + SCAN_ROM &&
        ReadMacInt32 (scan - 6) == 0x4eb081e1 && ReadMacInt16 (scan - 2) == 0x1c64 &&
        ReadMacInt32 (ROMBaseMac + STRETCH_FINISH) == 0x42780d6e)
        s_scanThunk = InstallThunk (ST_SCAN_LOOP, scan, ROMBaseMac + STRETCH_FINISH);
    else if (scan == stretch + SYSTEM_SCAN_OFFSET &&
             MatchesCode (stretch, SYSTEM_SCAN_OFFSET + 4, 0x93858458) &&
             MatchesCode (stretch + SYSTEM_FINISH_OFFSET, 0x50, 0x5f2221f7))
        s_scanThunk = InstallThunk (ST_SCAN_LOOP, scan, stretch + SYSTEM_FINISH_OFFSET);
    if (s_scanThunk)
    {
        s_scanCaller = scan;
        CLogger::Get ()->Write (FROM, LogNotice, "stScanLoop installed (%s)", mode);
    }
}

void QDEmulOp (M68kRegisters &regs)
{
#ifndef OKAPIA_JIT
    const uint32 sp = regs.a[7];
    const unsigned shift = uint16_t (regs.d[0]);
    regs.d[0] = 0;
    const uint32 pc = m68k_getpc ();
    const bool region = s_regionThunk && pc == s_regionThunk + THUNK_OPCODE_OFFSET;
    const bool slab = s_slabThunk && pc == s_slabThunk + THUNK_OPCODE_OFFSET;
    const bool scale = s_scaleThunk && pc == s_scaleThunk + THUNK_OPCODE_OFFSET;
    const bool scan = s_scanThunk && pc == s_scanThunk + THUNK_OPCODE_OFFSET;
    if ((!region && !slab && !scale && !scan &&
         (!s_thunk || pc != s_thunk + THUNK_OPCODE_OFFSET)) ||
        !s_memory.ram.Map (sp, THUNK_SAVED_BYTES + 4))
        return;
    const uint32 caller = ReadMacInt32 (sp + THUNK_SAVED_BYTES);
    if (scale || scan)
    {
        unsigned &calls = scale ? s_scaleCalls : s_scanCalls;
        unsigned &native = scale ? s_scaleNative : s_scanNative;
        ++calls;
        if (scale)
        {
            if (caller != ROMBaseMac + SCALEBLT_RETURN &&
                (!s_systemScaleReturn || caller != s_systemScaleReturn))
                return;
            if (uint16 (regs.d[3]) != 5 || uint16 (regs.d[4]) != 3 || regs.d[7])
                return;
        }
        else if (caller != s_scanCaller)
            return;
        QDBlit blit;
        QDRegion regions[3];
        int top, left;
        const bool systemScan = scan && s_scanCaller != ROMBaseMac + SCAN_ROM;
        if (!QDPrepareScanBlit (s_memory, regs.a[6], scale, systemScan, blit, regions, top, left) ||
            s_probe)
            return;
        QDRunRegionBlit (blit, regions, top, left);
        ++native;
        regs.d[0] = 1;
        return;
    }
    if (slab)
    {
        ++s_slabCalls;
        // The slow, mask-buffer paths of DrawLine, DrawArc and QuickPolys.
        // The ROM has separate tables for run lists and expanded patterns.
        const uint32 offset = caller - ROMBaseMac;
        if (shift != 3 || (offset != 0x29e3c && offset != 0x34322 && offset != 0x34ce2))
            return;
        constexpr uint32 maskModes[] = {0x2f1c4, 0x2f1e6, 0x2f1fc, 0x2f226};
        for (unsigned op = 0; op < sizeof maskModes / sizeof *maskModes; ++op)
            if (regs.a[4] == ROMBaseMac + maskModes[op])
            {
                if (QDSlab8 (s_memory, regs.a, regs.d, op, !s_probe) && !s_probe)
                {
                    ++s_slabNative;
                    regs.d[0] = 1;
                }
                break;
            }
        return;
    }
    if (region)
    {
        ++s_regionCalls;
        QDBlit blit;
        QDRegion regions[3];
        int top, left;
        if ((caller == ROMBaseMac + RGNBLT_RETURN ||
             (s_systemRegionReturn && caller == s_systemRegionReturn)) &&
            QDPrepareRegionBlit (s_memory, regs.a, regs.d, blit, regions, top, left) && !s_probe)
        {
            QDRunRegionBlit (blit, regions, top, left);
            ++s_regionNative;
            regs.d[0] = 1;
        }
        return;
    }
    ++s_calls;
    if (s_probe && s_calls <= 3)
        CLogger::Get ()->Write (FROM, LogNotice, "caller %08x, a4/a5/a6 %08x/%08x/%08x, d3/d4 %08x/%08x",
                                caller,
                                regs.a[4], regs.a[5], regs.a[6], regs.d[3], regs.d[4]);
    if (caller != ROMBaseMac + BITBLT_RETURN &&
        (!s_systemReturn || caller != s_systemReturn))
    {
        if (s_report)
            RecordRejection (regs, {QDBlitReject::Caller});
        return;
    }
    ++s_romCalls;

    QDBlit blit;
    QDBlitDiagnostic diagnostic = {};
    if (!QDPrepareBlit (s_memory, regs.a, regs.d, blit, s_report ? &diagnostic : nullptr))
    {
        if (s_report)
            RecordRejection (regs, diagnostic);
        return;
    }
    ++s_eligible;
    if (s_probe)
        return;
    const unsigned start = s_report ? CTimer::GetClockTicks () : 0;
    if (s_parallel && QDParallelBlit (blit))
        ++s_split;
    else
        QDRunBlit (blit, 0, blit.height);
    if (s_report)
    {
        s_usecs += CTimer::GetClockTicks () - start;
        s_bytes += uint64_t (blit.rowBytes) * blit.height;
        s_copies += blit.source != nullptr;
        s_fills += blit.source == nullptr;
        s_overlaps += blit.overlaps;
    }
    ++s_completed;
    regs.d[0] = 1;
#else
    (void) regs;
#endif
}

void QDReport ()
{
    if (!s_report)
        return;
    const unsigned now = CTimer::Get ()->GetTicks ();
    if (now - s_lastReport < 5 * HZ)
        return;
    s_lastReport = now;
    if (s_calls)
        CLogger::Get ()->Write (FROM, LogNotice,
                                "BitBlt %u calls, %u verified, %u eligible, %u native "
                                "(%u copies, %u overlapping, %u fills, %u split), %llu bytes, %u us",
                                s_calls, s_romCalls, s_eligible, s_completed,
                                s_copies, s_overlaps, s_fills, s_split,
                                (unsigned long long) s_bytes, s_usecs);
    for (unsigned n = 0; n < unsigned (QDBlitReject::Count); ++n)
    {
        auto &sample = s_rejected[n];
        if (!sample.count)
            continue;
        if (sample.mode < 0)
            CLogger::Get ()->Write (FROM, LogNotice, "BitBlt rejected %u: %s",
                                    sample.count, s_rejectNames[n]);
        else
            CLogger::Get ()->Write (FROM, LogNotice,
                                    "BitBlt rejected %u: %s (first: mode %d, depth %u->%u, flags %04x)",
                                    sample.count, s_rejectNames[n], sample.mode, sample.sourceDepth,
                                    sample.destinationDepth, sample.colorize);
        sample.count = 0;
    }
    if (s_regionCalls)
        CLogger::Get ()->Write (FROM, LogNotice, "RgnBlt %u calls, %u native",
                                s_regionCalls, s_regionNative);
    s_regionCalls = s_regionNative = 0;
    if (s_slabCalls)
        CLogger::Get ()->Write (FROM, LogNotice, "DrawSlab %u calls, %u native",
                                s_slabCalls, s_slabNative);
    s_slabCalls = s_slabNative = 0;
    if (s_scaleCalls)
        CLogger::Get ()->Write (FROM, LogNotice, "ScaleBlt %u calls, %u native",
                                s_scaleCalls, s_scaleNative);
    if (s_scanCalls)
        CLogger::Get ()->Write (FROM, LogNotice, "stScanLoop %u calls, %u native",
                                s_scanCalls, s_scanNative);
    s_scaleCalls = s_scaleNative = s_scanCalls = s_scanNative = 0;
    s_calls = s_romCalls = s_eligible = s_completed = s_split = s_usecs = 0;
    s_copies = s_fills = s_overlaps = 0;
    s_bytes = 0;
}
