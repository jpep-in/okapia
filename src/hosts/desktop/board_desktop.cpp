//
// board_desktop.cpp — the board the shared platform files expect, played by a
// desktop: Circle's timer, heap and devices (shims/circle/), the Board* and
// Clock* functions, the firmware's output and input bridges, the Mac RAM block.
//
// Everything here exists so that src/common's platform files and
// src/kernel/kernel.cpp build unchanged (docs/notes/research/desktop-host.md:
// "about a third of src/circle/ builds unchanged behind a desktop shim"). What
// the Pi does with hardware, the host does with threads and its own clock.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_circle.h"
#include "hal_circle.h"
#include "clock_okapia.h"
#include "mac_ram_okapia.h"
#include "okapia_input.h"
#include "okapia_output.h"
#include "okapia_desktop.h"
#include "okapia_host.h"

#include <circle/bcmframebuffer.h>
#include <circle/usb/usbkeyboard.h>

#include <SDL3/SDL.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define FROM "okapia-board"

/*
 *  CTimer
 */
static CTimer s_Timer;

CTimer *CTimer::Get (void)
{
    return &s_Timer;
}

u64 CTimer::GetClockTicks64 (void)
{
    return SDL_GetTicksNS () / 1000;
}

unsigned CTimer::GetClockTicks (void)
{
    return (unsigned) GetClockTicks64 ();
}

void CTimer::SimpleusDelay (unsigned nMicros)
{
    SDL_DelayPrecise ((Uint64) nMicros * 1000);
}

void CTimer::SimpleMsDelay (unsigned nMs)
{
    SDL_Delay (nMs);
}

unsigned CTimer::GetTicks (void)
{
    return (unsigned) (SDL_GetTicks () / (1000 / HZ));
}

unsigned CTimer::GetTime (void)
{
    return (unsigned) DesktopScriptTime (time (0));
}

unsigned CTimer::GetLocalTime (void)
{
    return HostLocalTime ();
}

int CTimer::GetTimeZone (void)
{
    return HostTimeZoneMinutes ();
}

bool CTimer::SetTime (unsigned, bool)
{
    return false;                       // the host keeps its own clock
}

CDesktopTimeString *CTimer::GetTimeString (void)
{
    static CDesktopTimeString s_String;
    const time_t nNow = (time_t) GetTime ();
    struct tm Local;
    localtime_r (&nNow, &Local);
    strftime (s_String.m_Text, sizeof s_String.m_Text, "%b %d %H:%M:%S", &Local);
    return &s_String;
}

void CTimer::MsDelay (unsigned nMs)
{
    SDL_Delay (nMs);
}

void CTimer::usDelay (unsigned nMicros)
{
    SimpleusDelay (nMicros);
}

void CTimer::RegisterPeriodicHandler (TPeriodicTimerHandler *)
{
    // BoardFineTick() always succeeds here, so the fallback is never asked for.
    HostLog (FROM, HostLogWarning, "RegisterPeriodicHandler: not on a desktop");
}

/*
 *  CMemorySystem
 */
static CMemorySystem s_Memory;

CMemorySystem *CMemorySystem::Get (void)
{
    return &s_Memory;
}

// The Mac's addresses are host addresses minus MEMBaseDiff, the base of its RAM
// (DIRECT_ADDRESSING, main_okapia.cpp), so every block the Macintosh is shown —
// its frame buffer first — must lie above that base and within 4 GB of it. On
// the board Circle's heap does that by itself, the RAM block being claimed
// first. Here the block is reserved with room after it, and the heap is served
// from there; nothing it serves is ever given back (each is taken once for the
// life of the board, as on the Pi).
static const size_t ARENA_BYTES = (size_t) 512 << 20;
static unsigned char *s_pArena;
static size_t         s_nArenaUsed;

void *CMemorySystem::HeapAllocate (size_t nSize, int)
{
    if (s_pArena != 0)
    {
        const size_t nStart = (s_nArenaUsed + 4095) & ~(size_t) 4095;
        if (nStart + nSize <= ARENA_BYTES)
        {
            s_nArenaUsed = nStart + nSize;
            return s_pArena + nStart;
        }
        HostLog (FROM, HostLogError, "Heap: %zu KB more than the arena holds", nSize >> 10);
        return 0;
    }
    void *p = 0;
    return posix_memalign (&p, 64, nSize) == 0 ? p : 0;
}

void CMemorySystem::HeapFree (void *pBlock)
{
    if (s_pArena == 0 || (unsigned char *) pBlock < s_pArena
        || (unsigned char *) pBlock >= s_pArena + ARENA_BYTES)
    {
        free (pBlock);
    }
}

size_t CMemorySystem::GetHeapFreeSpace (int)
{
    return s_pArena != 0 ? ARENA_BYTES - s_nArenaUsed : (size_t) 1 << 30;
}

/*
 *  The Mac RAM block: once for the life of the process, as on the board, with
 *  the heap's arena after it (above).
 */
void *MacRamClaim (size_t nBytes)
{
    static void  *s_pBlock;
    static size_t s_nBlock;
    if (s_pBlock != 0)
    {
        return nBytes <= s_nBlock ? s_pBlock : 0;
    }
    // Reserved whole and touched lazily: the host gives pages as they are used.
    const size_t nRam = (nBytes + 4095) & ~(size_t) 4095;
    void *p = mmap (0, nRam + ARENA_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED)
    {
        HostLog (FROM, HostLogError, "Mac RAM: %zu MB not available", nBytes >> 20);
        return 0;
    }
    s_pBlock = p;
    s_nBlock = nBytes;
    s_pArena = (unsigned char *) p + nRam;
    return p;
}

/*
 *  The board
 */
static COkapiaBoard s_Board;

COkapiaBoard &OkapiaBoard (void)
{
    return s_Board;
}

bool COkapiaBoard::Start (const char *)  { return true; }  // the log is open already
void COkapiaBoard::StartConsole (void)   {}
bool COkapiaBoard::StartCard (void)      { return true; }  // the folder is open already
bool COkapiaBoard::StartUSB (void)       { return true; }  // the window is the keyboard

// The Mac's 60 Hz from a thread on an absolute schedule, as the Pi's comparator
// is re-armed: a late tick is dropped, never repaid (architecture.md, Timing).
static TBoardTickHandler *s_pTick;
static unsigned           s_nTickPeriod;

static void *TickThread (void *)
{
    Uint64 nNext = SDL_GetTicksNS ();
    for (;;)
    {
        nNext += (Uint64) s_nTickPeriod * 1000;
        const Uint64 nNow = SDL_GetTicksNS ();
        if (nNext > nNow)
        {
            SDL_DelayPrecise (nNext - nNow);
        }
        else
        {
            nNext = nNow;
        }
        TBoardTickHandler *pHandler = s_pTick;
        if (pHandler != 0)
        {
            pHandler ();
        }
    }
    return 0;
}

bool BoardFineTick (unsigned nPeriodUsec, TBoardTickHandler *pHandler)
{
    s_nTickPeriod = nPeriodUsec;
    s_pTick = pHandler;
    static bool s_bStarted;
    if (!s_bStarted)
    {
        pthread_t Thread;
        if (pthread_create (&Thread, 0, TickThread, 0) != 0)
        {
            return false;
        }
        pthread_detach (Thread);
        s_bStarted = true;
    }
    return true;
}

void BoardWatchClock (bool)      {}
void BoardLogFlush (void)        {}   // every line is in the file already
void BoardOutputShows (unsigned nX, unsigned nY, unsigned nWidth, unsigned nHeight,
                       unsigned nScale)
{
    DesktopMacArea (nX, nY, nWidth, nHeight, nScale);
}
void BoardSerialLog (bool)       {}
void BoardSerialReplay (void)    {}
void BoardPowerOff (void)        {}

/*
 *  The clock: the host's
 */
static const u32 MAC_UNIX_EPOCH_DIFF = 2082844800U;

void ClockInitialize (void)
{
    HostLog (FROM, HostLogNotice, "Clock: the host's, zone UTC%+d min", HostTimeZoneMinutes ());
}

void ClockSetZone (const char *, int)       {}
void ClockSetServer (const char *)          {}
void ClockPoll (void)                       {}
void ClockMacStart (void)                   {}

// The Mac's Time global, once a second: the host's local time. A Date & Time
// set inside Mac OS lasts until the next second, which is the host keeping the
// clock — said in the menu ("From this computer").
bool ClockMacSecond (u32 nRead, u32 *pWrite)
{
    const u32 nNow = (u32) HostLocalTime () + MAC_UNIX_EPOCH_DIFF;
    *pWrite = nNow;
    return nRead != nNow;
}

bool ClockSynced (void)                     { return true; }
bool ClockDecided (void)                    { return true; }
bool ClockRecordRead (TClockRecord *)       { return false; }
void ClockRecordForget (const char *)       {}
void ClockNoteSource (TClockSource, const char *) {}
TClockSource ClockSource (const char **ppDetail) { *ppDetail = ""; return ClockFromNetwork; }
void ClockSessionEnd (void)                 {}

/*
 *  The firmware's output bridge: the window's surface
 */
CBcmFrameBuffer *FwOutputClaim (void)
{
    static CBcmFrameBuffer *s_pOutput;
    if (s_pOutput == 0)
    {
        const TSurface *pSurface = DesktopSurface ();
        if (pSurface->pPixels == 0)
        {
            return 0;
        }
        s_pOutput = new CBcmFrameBuffer (pSurface->pPixels, pSurface->nWidth,
                                         pSurface->nHeight, pSurface->nPitch);
    }
    return s_pOutput;
}

bool FwOutputRedLow (void)
{
    return false;                       // 0x00RRGGBB in memory: blue first, as the Pi
}

const TFwMonitor &FwOutputMonitor (void)
{
    static TFwMonitor s_Monitor;
    return s_Monitor;
}

/*
 *  The firmware's input bridge, and the devices InputInit() looks for
 */
static CUSBKeyboardDevice    s_Keyboard;
static CMouseDevice          s_Mouse;
static TKeyStatusHandlerRaw *volatile s_pMacKeys;
static TMouseStatusHandler  *volatile s_pMacMouse;

CDeviceNameService *CDeviceNameService::Get (void)
{
    static CDeviceNameService s_Service;
    return &s_Service;
}

void *CDeviceNameService::GetDevice (const char *pName, bool)
{
    if (strcmp (pName, "ukbd1") == 0)  return &s_Keyboard;
    if (strcmp (pName, "mouse1") == 0) return &s_Mouse;
    return 0;
}

void CUSBKeyboardDevice::RegisterKeyStatusHandlerRaw (TKeyStatusHandlerRaw *pHandler)
{
    s_pMacKeys = pHandler;
    DesktopMacInput (true);
}

bool FwInputPassMouseTo (TMouseStatusHandler *pHandler)
{
    s_pMacMouse = pHandler;
    DesktopMacInput (true);
    return true;
}

bool FwInputWatch (void)
{
    return true;
}

void FwInputReclaim (void)
{
    s_pMacKeys  = 0;
    s_pMacMouse = 0;
    DesktopMacInput (false);
}

void DesktopMacKeys (unsigned char ucModifiers, const unsigned char Keys[6])
{
    TKeyStatusHandlerRaw *pHandler = s_pMacKeys;
    if (pHandler != 0)
    {
        pHandler (ucModifiers, Keys);
    }
}

void DesktopMacMouse (unsigned nButtons, int nDX, int nDY)
{
    TMouseStatusHandler *pHandler = s_pMacMouse;
    if (pHandler != 0)
    {
        pHandler (nButtons, nDX, nDY, 0);
    }
}

/*
 *  The sound and the network, until they come (audio_dummy.cpp and
 *  ether_dummy.cpp keep the Macintosh's side quiet; these are the seams' calls)
 */

// kernel.cpp hands the board's log flush to the menu; every line is in the
// file already here.
void FirmwareSetLogFlush (void (*) (void))  {}

#ifdef JIT_ARM64
/*
 *  The translator's poll (patches/macemu/0012, JIT_ARM64_OWN_POLL): the
 *  interpreter's 1 kHz seam, as jit_arm64_circle.cpp gives it on the board.
 *  The code memory is the patch's own hosted default, MAP_JIT.
 */
extern void cpu_do_check_ticks (void);

void jit_arm64_host_poll (void)
{
    static unsigned s_nLastVisit;
    static bool s_bSaid;
    if (!s_bSaid)
    {
        s_bSaid = true;
        HostLog (FROM, HostLogNotice, "Translator running (jit true)");
    }
    const unsigned nNow = CTimer::GetClockTicks ();
    if (nNow - s_nLastVisit >= 1000)
    {
        s_nLastVisit = nNow;
        cpu_do_check_ticks ();
    }
}
#endif
