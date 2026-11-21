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
#include "quickdraw_okapia.h"
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
static constexpr uint32 TOOL_TABLE = 0xe00;  // Toolbox trap table, 1024 vectors, Macintosh II and later
static constexpr unsigned THUNK_SAVED_BYTES = 6;
static constexpr unsigned GATE_OPCODE_OFFSET = 60;  // of the emulator opcode in a gated trampoline
static constexpr unsigned DEFAULT_MINIMUM = 4096;  // bytes, see QDInstall

static QDMemory s_memory;
// Each is the address of the trampoline's emulator opcode, which is what the
// program counter holds when QDEmulOp runs.
static uint32 s_thunk;
static uint32 s_regionThunk;
static uint32 s_slabThunk;
static uint32 s_scaleThunk, s_scanThunk, s_scanCaller;
static bool s_installAttempted;
static bool s_parallel;
static bool s_report;
static bool s_probe;
static bool s_colorLoops;   // the guest's colour loops are the ones this file knows

// Verify mode: BitBlt and RgnBlt are drawn twice. The trampoline calls the
// host, lets the guest's own handler draw, and calls the host again, which
// puts the destination back as it was, draws natively and compares. The
// guest's pixels are the ones left on the screen either way.
static bool s_verify;
static constexpr unsigned VERIFY_SECOND = 20;       // bytes between the trampoline's two emulator opcodes
static constexpr size_t VERIFY_BYTES = 4u << 20;    // the largest destination compared
static uint8_t *s_before, *s_guest;
struct QDPendingVerify
{
    bool active, region;
    uint32 sp;
    int mode;
    size_t span;
    int top, left;
    QDBlit blit;
    QDRegion regions[3];
};
static QDPendingVerify s_pending;
// For BitBlt, then for RgnBlt.
static unsigned s_verified[2], s_differed[2], s_outside[2], s_uncompared[2];
static unsigned s_differencesLogged;
static uint32 s_systemReturn;
static uint32 s_systemRegionReturn;
static uint32 s_systemScaleReturn;
static unsigned s_regionCalls, s_regionNative;
static unsigned s_slabCalls, s_slabNative;
static unsigned s_slabCallers, s_slabModes;     // rejected for the call site, for the mode dispatcher
static unsigned s_slabSeen, s_slabServed;       // since installation: the hook's trial
static uint32 s_slabOriginal;
static constexpr unsigned SLAB_TRIAL = 65536;
static uint32 s_slabCaller, s_slabMode, s_slabShift;
static unsigned s_scaleCalls, s_scaleNative, s_scanCalls, s_scanNative;
static unsigned s_calls, s_romCalls, s_eligible, s_completed, s_split, s_usecs, s_lastReport;
static unsigned s_copies, s_fills, s_overlaps, s_colorized;
static uint64_t s_bytes;

struct RejectedBlits
{
    unsigned count;
    unsigned sourceDepth, destinationDepth;
    int mode;
    unsigned colorize;
};

// What BitBlt declined, then what RgnBlt declined.
static RejectedBlits s_rejected[2][unsigned (QDBlitReject::Count)];
static const char *const s_rejectNames[] = {
    "caller", "frame", "depth", "mode", "colorize", "alpha",
    "pixmap", "rectangle", "pixel bounds", "pattern", "overlap", "region"
};
static_assert (sizeof s_rejectNames / sizeof *s_rejectNames == unsigned (QDBlitReject::Count),
               "BitBlt rejection names must match QDBlitReject");

#ifndef OKAPIA_JIT
static void RecordRejection (const M68kRegisters &regs, const QDBlitDiagnostic &diagnostic,
                             bool region = false)
{
    auto &sample = s_rejected[region][unsigned (diagnostic.reason)];
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
    s_report = s_probe = s_parallel = s_colorLoops = s_verify = false;
    s_pending.active = false;
    s_differencesLogged = 0;
    for (unsigned hook = 0; hook < 2; ++hook)
        s_verified[hook] = s_differed[hook] = s_outside[hook] = s_uncompared[hook] = 0;
    s_systemReturn = 0;
    s_systemRegionReturn = 0;
    s_systemScaleReturn = 0;
    s_regionCalls = s_regionNative = 0;
    s_slabCalls = s_slabNative = s_slabCallers = s_slabModes = 0;
    s_slabSeen = s_slabServed = 0;
    s_slabOriginal = 0;
    s_scaleCalls = s_scaleNative = s_scanCalls = s_scanNative = 0;
    s_calls = s_romCalls = s_eligible = s_completed = s_split = s_usecs = 0;
    s_copies = s_fills = s_overlaps = s_colorized = 0;
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

// What a trampoline checks in 68k before it calls the host at all.
struct QDGate
{
    uint32 firstCaller, secondCaller;   // the two verified return addresses
    uint32 minimum;                     // bytes of pixels in the clipped rectangle
};

// Returns the address of the emulator opcode inside the new trampoline.
static uint32 BuildThunk (uint32 original, uint32 finish, bool returnSuccess, const QDGate *gate)
{
    if (s_verify && !finish && !returnSuccess)
    {
        const uint16 code[] = {
            0x42e7, 0x2f00,                                 //  0 MOVE CCR,-(SP); MOVE.L D0,-(SP)
            QD_EMUL_OP,                                     //  4 is this call one to compare?
            0x4a00, 0x6716,                                 //  6 TST.B D0; BEQ.S 32
            0x201f, 0x44df,                                 // 10 MOVE.L (SP)+,D0; MOVE (SP)+,CCR
            0x4eb9, uint16 (original >> 16), uint16 (original), // 14 JSR original
            0x42e7, 0x2f00,                                 // 20 MOVE CCR,-(SP); MOVE.L D0,-(SP)
            QD_EMUL_OP,                                     // 24 draw it again and compare
            0x201f, 0x44df, 0x4e75,                         // 26 MOVE.L (SP)+,D0; MOVE (SP)+,CCR; RTS
            0x201f, 0x44df, 0x4ef9, uint16 (original >> 16), uint16 (original)
                                                            // 32 the same, then JMP original
        };
        static_assert (sizeof code == 42 && VERIFY_SECOND == 20, "the branch offsets above assume this layout");
        const uint32 addr = AllocateThunk (code, sizeof code / sizeof *code);
        return addr ? addr + 4 : 0;
    }
    // Preserve the inputs on fallback. ScaleBlt returns success in D0,
    // and stScanLoop transfers to the guest cleanup to restore its stack.
    if (finish)
    {
        const uint16 code[] = {
            0x42e7, 0x2f00, QD_EMUL_OP, 0x4a00, 0x670a,
            0x201f, 0x44df, 0x4ef9, uint16 (finish >> 16), uint16 (finish),
            0x201f, 0x44df, 0x4ef9, uint16 (original >> 16), uint16 (original)
        };
        const uint32 addr = AllocateThunk (code, sizeof code / sizeof *code);
        return addr ? addr + 4 : 0;
    }
    if (gate)
    {
        // Leaving translated code costs more than a small transfer does in
        // it. The call site and the clipped area are tested here, in 68k the
        // translator compiles, and only a transfer worth the journey reaches
        // the host. The area is counted in bits, the pixels shifted by the
        // depth BitBlt and RgnBlt receive in D4: what the guest's loops cost
        // follows the bytes they move, at any depth. A6 is the StretchBits
        // frame once the return address is one of the two verified call sites.
        // At 1 and 2 bits a row is a few dozen bytes and the guest's loops
        // beat the journey whatever the area (MacBench under QEMU: 49 ticks
        // of scrolling became 109): those depths stay in the guest here, and
        // go to the host with the interpreter, whose trampoline has no gate.
        const uint16 code[] = {
            0x42e7,                                             //  0 MOVE    CCR,-(SP)
            0x2f00,                                             //  2 MOVE.L  D0,-(SP)
            0x0caf, uint16 (gate->firstCaller >> 16), uint16 (gate->firstCaller), 0x0006,
                                                                //  4 CMPI.L  #first,6(SP)
            0x670a,                                             // 12 BEQ.S   24
            0x0caf, uint16 (gate->secondCaller >> 16), uint16 (gate->secondCaller), 0x0006,
                                                                // 14 CMPI.L  #second,6(SP)
            0x6630,                                             // 22 BNE.S   72
            0x0c44, 0x0002,                                     // 24 CMPI.W  #2,D4       4 bits a pixel
            0x652a,                                             // 28 BCS.S   72
            0x302e, 0xff92,                                     // 30 MOVE.W  -$6E(A6),D0 right
            0x906e, 0xff8e,                                     // 34 SUB.W   -$72(A6),D0 left
            0x3f00,                                             // 38 MOVE.W  D0,-(SP)
            0x302e, 0xff90,                                     // 40 MOVE.W  -$70(A6),D0 bottom
            0x906e, 0xff8c,                                     // 44 SUB.W   -$74(A6),D0 top
            0xc0df,                                             // 48 MULU.W  (SP)+,D0
            0xe9a8,                                             // 50 LSL.L   D4,D0
            0x0c80, uint16 (gate->minimum >> 13), uint16 (gate->minimum << 3),
                                                                // 52 CMPI.L  #8 * minimum,D0
            0x650c,                                             // 58 BCS.S   72
            QD_EMUL_OP,                                         // 60
            0x4a00,                                             // 62 TST.B   D0
            0x6706,                                             // 64 BEQ.S   72
            0x201f, 0x44df, 0x4e75,                             // 66 MOVE.L (SP)+,D0; MOVE (SP)+,CCR; RTS
            0x201f, 0x44df, 0x4ef9, uint16 (original >> 16), uint16 (original)
                                                                // 72 the same, then JMP original
        };
        static_assert (sizeof code == 82 && GATE_OPCODE_OFFSET == 60,
                       "the branch offsets above assume this layout");
        const uint32 addr = AllocateThunk (code, sizeof code / sizeof *code);
        return addr ? addr + GATE_OPCODE_OFFSET : 0;
    }
    const uint16 code[] = {
        0x42e7, 0x2f00, QD_EMUL_OP, 0x4a00, 0x6706,
        uint16 (returnSuccess ? 0x588f : 0x201f), 0x44df, 0x4e75,
        0x201f, 0x44df, 0x4ef9, uint16 (original >> 16), uint16 (original)
    };
    const uint32 addr = AllocateThunk (code, sizeof code / sizeof *code);
    return addr ? addr + 4 : 0;
}

static uint32 InstallThunk (uint32 trap, uint32 original, uint32 finish = 0, bool returnSuccess = false,
                            const QDGate *gate = nullptr)
{
    // The vector is written into the trap table itself. SetToolTrapAddress,
    // called here, runs in the context of whichever application was idling,
    // and the Process Manager then makes the patch that application's own:
    // it took the hook out whenever another application was drawing.
    const uint32 vector = TOOL_TABLE + 4 * (trap & 0x3ff);
    if (ReadMacInt32 (vector) != original)
        return 0;
    const uint32 opcode = BuildThunk (original, finish, returnSuccess, gate);
    if (!opcode)
        return 0;
    WriteMacInt32 (vector, opcode - (gate ? GATE_OPCODE_OFFSET : 4));
    return opcode;
}

// The loops the two dispatchers reach through the trap table when the frame
// asks for colours: BitBlt's for a row of several longs, then of one, then
// RgnBlt's; copy, OR and BIC each time. The System replaces some of their
// neighbours, so the native version stands in only for the ROM's own.
static bool ColorLoopsAreKnown ()
{
    static const struct { uint16 vector; uint32 loop; } expected[] = {
        {0x1af0, 0x2cea2}, {0x1af4, 0x2ced2}, {0x1afc, 0x2cf02},
        {0x1b30, 0x2cea0}, {0x1b34, 0x2ced0}, {0x1b3c, 0x2cf00},
        {0x1bb8, 0x2af60}, {0x1bbc, 0x2b00a}, {0x1bc4, 0x2b07a},
    };
    for (const auto &entry : expected)
        if (ReadMacInt32 (entry.vector) != ROMBaseMac + entry.loop)
            return false;
    return true;
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
    s_verify = strcmp (mode, "verify") == 0;
    if (!s_parallel && !s_probe && !s_verify && strcmp (mode, "sync") != 0)
        return;
    s_report = s_probe || s_verify || PrefsFindBool ("perfreport");
    if (s_verify && !s_before)
    {
        // Once for the life of the kernel: the Macintosh restarts, these stay.
        s_before = new uint8_t[VERIFY_BYTES];
        s_guest = new uint8_t[VERIFY_BYTES];
    }
    // With the translator, QuickDraw's own loops run as ARM64 code and a call
    // to the host is what costs. Timed modes then gate the two transfer hooks
    // in 68k and leave DrawSlab, one call for each scan line, to the guest.
    // Probe mode keeps every hook open to count what reaches them.
#ifdef JIT_ARM64
    const bool translated = PrefsFindBool ("jit");
#else
    const bool translated = false;
#endif
    const bool gated = translated && !s_probe && !s_verify;
    const int32 wanted = PrefsFindInt32 ("qdaccelmin");
    // In bytes; the trampoline compares bits and its immediate is 32 bits wide.
    const uint32 minimum = wanted > 0 ? (uint32 (wanted) < (1u << 28) ? uint32 (wanted) : (1u << 28)) : DEFAULT_MINIMUM;

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

    const uint32 romReturn = ROMBaseMac + BITBLT_RETURN;
    const QDGate blitGate = {romReturn, s_systemReturn ? s_systemReturn : romReturn, minimum};
    s_thunk = InstallThunk (BITBLT, original, 0, false, gated ? &blitGate : nullptr);
    if (!s_thunk)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "BitBlt disabled: no system heap for the trampoline");
        return;
    }
    CLogger::Get ()->Write (FROM, LogNotice,
                            "BitBlt installed (%s, StretchBits %08x, verified RAM return %08x)",
                            mode, stretch, s_systemReturn);
    if (gated)
        CLogger::Get ()->Write (FROM, LogNotice,
                                "translator: transfers under %u bytes or 4 bits a pixel, and DrawSlab, stay in the guest",
                                minimum);
    s_colorLoops = ColorLoopsAreKnown ();
    if (!s_colorLoops)
        CLogger::Get ()->Write (FROM, LogNotice, "colour loops patched: colorized transfers stay in the guest");

    regs.d[0] = RGNBLT;
    Execute68kTrap (0xa746, &regs);
    const uint32 region = regs.a[0];
    const uint32 regionReturn = ROMBaseMac + RGNBLT_RETURN;
    const QDGate regionGate = {regionReturn, s_systemRegionReturn ? s_systemRegionReturn : regionReturn,
                               minimum};
    if (region == ROMBaseMac + RGNBLT_ROM || MatchesCode (region, 4096, 0xde7cb520))
        s_regionThunk = InstallThunk (RGNBLT, region, 0, false, gated ? &regionGate : nullptr);
    if (s_regionThunk)
        CLogger::Get ()->Write (FROM, LogNotice, "RgnBlt installed (%s, handler %08x)", mode, region);

    regs.d[0] = DRAWSLAB;
    Execute68kTrap (0xa746, &regs);
    if (regs.a[0] == ROMBaseMac + DRAWSLAB_ROM && !gated && !s_verify)
    {
        s_slabOriginal = regs.a[0];
        s_slabThunk = InstallThunk (DRAWSLAB, s_slabOriginal);
    }

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

#ifndef OKAPIA_JIT
// The first half of a compared call: keep the destination as it is now.
static bool BeginVerify (const M68kRegisters &regs, const QDBlit &blit, const QDRegion *regions,
                         int top, int left, int mode)
{
    const bool region = regions != nullptr;
    const size_t span = size_t (blit.height - 1) * blit.destinationPitch + blit.rowBytes;
    if (span > VERIFY_BYTES)
    {
        ++s_uncompared[region];
        return false;
    }
    // A call that never came back for its second half is simply replaced:
    // RgnBlt leaves through StretchBits when the stack is too short.
    memcpy (s_before, blit.destination, span);
    s_pending.active = true;
    s_pending.region = region;
    s_pending.sp = regs.a[7];
    s_pending.mode = mode;
    s_pending.span = span;
    s_pending.top = top;
    s_pending.left = left;
    s_pending.blit = blit;
    for (unsigned n = 0; region && n < 3; ++n)
        s_pending.regions[n] = regions[n];
    return true;
}

// The second half: the guest has drawn. Put the destination back, draw
// natively, compare, and leave the guest's pixels if the two differ. The
// source is whole again too when it lies in the destination, and nothing
// else was written.
static void FinishVerify (const M68kRegisters &regs)
{
    auto &pending = s_pending;
    if (!pending.active || pending.sp != regs.a[7])
        return;
    pending.active = false;
    const QDBlit &blit = pending.blit;
    const unsigned hook = pending.region;
    uint8_t *const destination = blit.destination;
    memcpy (s_guest, destination, pending.span);
    memcpy (destination, s_before, pending.span);
    if (pending.region)
        QDRunRegionBlit (blit, pending.regions, pending.top, pending.left);
    else
        QDRunBlit (blit, 0, blit.height);
    if (memcmp (destination, s_guest, pending.span) == 0)
    {
        ++s_verified[hook];
        return;
    }
    size_t at = 0;
    while (destination[at] == s_guest[at])
        ++at;
    const unsigned row = unsigned (at / blit.destinationPitch), byte = unsigned (at % blit.destinationPitch);
    // Between two rows of the rectangle lie bytes neither side writes: a
    // difference there is something an interrupt drew meanwhile.
    const bool outside = byte >= blit.rowBytes;
    ++(outside ? s_outside : s_differed)[hook];
    if (!outside && s_differencesLogged < 16)
    {
        ++s_differencesLogged;
        CLogger::Get ()->Write (FROM, LogWarning,
                                "%s differs: mode %d, %u bits, %ux%u at bit %u, source %s bit %u, "
                                "flip %08x, colours %d (%08x/%08x), pattern %ux%u, overlap %d: "
                                "row %u byte %u, guest %02x, native %02x",
                                hook ? "RgnBlt" : "BitBlt", pending.mode, 1u << blit.shift, blit.width,
                                blit.height, blit.destinationBit, blit.source ? "at" : "none,", blit.sourceBit,
                                blit.flip, blit.colorize, blit.foreColor, blit.backColor,
                                blit.patternRows ? blit.patternWidth : 0, blit.patternRows ? blit.patternHeight : 0,
                                blit.overlaps, row, byte, s_guest[at], destination[at]);
    }
    memcpy (destination, s_guest, pending.span);
}
#endif

void QDEmulOp (M68kRegisters &regs)
{
#ifndef OKAPIA_JIT
    const uint32 sp = regs.a[7];
    const unsigned shift = uint16_t (regs.d[0]);
    regs.d[0] = 0;
    const uint32 pc = m68k_getpc ();
    if (s_verify && ((s_thunk && pc == s_thunk + VERIFY_SECOND) ||
                     (s_regionThunk && pc == s_regionThunk + VERIFY_SECOND)))
    {
        FinishVerify (regs);
        return;
    }
    const bool region = s_regionThunk && pc == s_regionThunk;
    const bool slab = s_slabThunk && pc == s_slabThunk;
    const bool scale = s_scaleThunk && pc == s_scaleThunk;
    const bool scan = s_scanThunk && pc == s_scanThunk;
    if ((!region && !slab && !scale && !scan && (!s_thunk || pc != s_thunk)) ||
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
        if (!QDPrepareScanBlit (s_memory, regs.a[6], scale, systemScan, blit, regions, top, left))
            return;
        ++native;   // In probe and verify modes: what would have been drawn here.
        if (s_probe || s_verify)
            return;
        QDRunRegionBlit (blit, regions, top, left);
        regs.d[0] = 1;
        return;
    }
    if (slab)
    {
        ++s_slabCalls;
        // DrawSlab is called for every scan line of every shape. A hook that
        // serves none of them only costs: after a trial it gives the vector
        // back, unless the probe is counting or someone has patched over it.
        if (!s_probe && ++s_slabSeen == SLAB_TRIAL && s_slabServed < SLAB_TRIAL / 100)
        {
            const uint32 vector = TOOL_TABLE + 4 * (DRAWSLAB & 0x3ff);
            if (ReadMacInt32 (vector) == s_slabThunk - 4)
            {
                WriteMacInt32 (vector, s_slabOriginal);
                CLogger::Get ()->Write (FROM, LogNotice,
                                        "DrawSlab left to the guest: %u of its first %u calls were served",
                                        s_slabServed, SLAB_TRIAL);
            }
        }
        // The slow, mask-buffer paths of DrawLine, DrawArc and QuickPolys.
        // The ROM has separate tables for run lists and expanded patterns.
        const uint32 offset = caller - ROMBaseMac;
        if (shift != 3 || (offset != 0x29e3c && offset != 0x34322 && offset != 0x34ce2))
        {
            if (s_report && !s_slabCallers++)
            {
                s_slabCaller = caller;
                s_slabShift = shift;
            }
            return;
        }
        constexpr uint32 maskModes[] = {0x2f1c4, 0x2f1e6, 0x2f1fc, 0x2f226};
        for (unsigned op = 0; op < sizeof maskModes / sizeof *maskModes; ++op)
            if (regs.a[4] == ROMBaseMac + maskModes[op])
            {
                if (QDSlab8 (s_memory, regs.a, regs.d, op, !s_probe))
                {
                    ++s_slabNative;
                    ++s_slabServed;
                    regs.d[0] = !s_probe;
                }
                return;
            }
        if (s_report && !s_slabModes++)
            s_slabMode = regs.a[4];
        return;
    }
    if (region)
    {
        ++s_regionCalls;
        QDBlit blit;
        QDRegion regions[3];
        int top, left;
        QDBlitDiagnostic diagnostic = {QDBlitReject::Caller};
        bool accepted = (caller == ROMBaseMac + RGNBLT_RETURN ||
                         (s_systemRegionReturn && caller == s_systemRegionReturn)) &&
                        QDPrepareRegionBlit (s_memory, regs.a, regs.d, blit, regions, top, left,
                                             s_report ? &diagnostic : nullptr);
        if (accepted && blit.colorize && !s_colorLoops)
        {
            diagnostic.reason = QDBlitReject::Colorize;
            accepted = false;
        }
        if (!accepted)
        {
            if (s_report)
                RecordRejection (regs, diagnostic, true);
            return;
        }
        ++s_regionNative;
        if (s_verify)
            regs.d[0] = BeginVerify (regs, blit, regions, top, left, diagnostic.mode);
        else if (!s_probe)
        {
            QDRunRegionBlit (blit, regions, top, left);
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
    bool accepted = QDPrepareBlit (s_memory, regs.a, regs.d, blit, s_report ? &diagnostic : nullptr);
    if (accepted && blit.colorize && !s_colorLoops)
    {
        diagnostic.reason = QDBlitReject::Colorize;
        accepted = false;
    }
    if (!accepted)
    {
        if (s_report)
            RecordRejection (regs, diagnostic);
        return;
    }
    ++s_eligible;
    if (s_verify)
        regs.d[0] = BeginVerify (regs, blit, nullptr, 0, 0, diagnostic.mode);
    if (s_probe || s_verify)
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
        s_colorized += blit.colorize;
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
                                "(%u copies, %u overlapping, %u colorized, %u fills, %u split), %llu bytes, %u us",
                                s_calls, s_romCalls, s_eligible, s_completed,
                                s_copies, s_overlaps, s_colorized, s_fills, s_split,
                                (unsigned long long) s_bytes, s_usecs);
    for (unsigned hook = 0; hook < 2; ++hook)
        for (unsigned n = 0; n < unsigned (QDBlitReject::Count); ++n)
        {
            auto &sample = s_rejected[hook][n];
            const char *const name = hook ? "RgnBlt" : "BitBlt";
            if (!sample.count)
                continue;
            if (sample.mode < 0)
                CLogger::Get ()->Write (FROM, LogNotice, "%s rejected %u: %s",
                                        name, sample.count, s_rejectNames[n]);
            else
                CLogger::Get ()->Write (FROM, LogNotice,
                                        "%s rejected %u: %s (first: mode %d, depth %u->%u, colour loops %u)",
                                        name, sample.count, s_rejectNames[n], sample.mode, sample.sourceDepth,
                                        sample.destinationDepth, sample.colorize);
            sample.count = 0;
        }
    for (unsigned hook = 0; s_verify && hook < 2; ++hook)
        if (s_verified[hook] || s_differed[hook] || s_outside[hook] || s_uncompared[hook])
        {
            CLogger::Get ()->Write (FROM, LogNotice,
                                    "%s compared: %u the same, %u different, %u disturbed between rows, %u too large",
                                    hook ? "RgnBlt" : "BitBlt", s_verified[hook], s_differed[hook],
                                    s_outside[hook], s_uncompared[hook]);
            s_verified[hook] = s_differed[hook] = s_outside[hook] = s_uncompared[hook] = 0;
        }
    // Probe and verify modes draw nothing of their own: the second figure is what qualified.
    const char *const done = s_probe || s_verify ? "eligible" : "native";
    if (s_regionCalls)
        CLogger::Get ()->Write (FROM, LogNotice, "RgnBlt %u calls, %u %s",
                                s_regionCalls, s_regionNative, done);
    s_regionCalls = s_regionNative = 0;
    if (s_slabCalls)
        CLogger::Get ()->Write (FROM, LogNotice, "DrawSlab %u calls, %u %s",
                                s_slabCalls, s_slabNative, done);
    if (s_slabCallers)
        CLogger::Get ()->Write (FROM, LogNotice, "DrawSlab rejected %u: caller (first: %08x, depth shift %u)",
                                s_slabCallers, s_slabCaller, s_slabShift);
    if (s_slabModes)
        CLogger::Get ()->Write (FROM, LogNotice, "DrawSlab rejected %u: mode (first: dispatcher %08x)",
                                s_slabModes, s_slabMode);
    s_slabCalls = s_slabNative = s_slabCallers = s_slabModes = 0;
    if (s_scaleCalls)
        CLogger::Get ()->Write (FROM, LogNotice, "ScaleBlt %u calls, %u %s",
                                s_scaleCalls, s_scaleNative, done);
    if (s_scanCalls)
        CLogger::Get ()->Write (FROM, LogNotice, "stScanLoop %u calls, %u %s",
                                s_scanCalls, s_scanNative, done);
    s_scaleCalls = s_scaleNative = s_scanCalls = s_scanNative = 0;
    s_calls = s_romCalls = s_eligible = s_completed = s_split = s_usecs = 0;
    s_copies = s_fills = s_overlaps = s_colorized = 0;
    s_bytes = 0;
}
