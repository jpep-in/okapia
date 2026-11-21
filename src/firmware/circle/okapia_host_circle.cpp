//
// okapia_host_circle.cpp — okapia_host.h on the bare board: Circle, FatFs, the
// Pi's network and clock. What used to sit inside okapia_firmware.cpp beside the
// menu's own logic, now src/common/okapia_menu.cpp.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_host.h"
#include "okapia_firmware.h"
#include "okapia_input.h"
#include "okapia_output.h"
#include "okapia_settings.h"

#include <circle/bcmframebuffer.h>
#include <circle/logger.h>
#include <circle/machineinfo.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/types.h>

#include <wrap_fatfs.h>

#include "board_net_circle.h"
#include "clock_circle.h"
#include "net_bench_circle.h"
#include "wlan_circle.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*
 *  The log
 */
void HostLog (const char *pFrom, THostLogLevel Level, const char *pFormat, ...)
{
    static const TLogSeverity Severity[] = { LogError, LogWarning, LogNotice, LogDebug };
    va_list Args;
    va_start (Args, pFormat);
    CLogger::Get ()->WriteV (pFrom, Severity[Level], pFormat, Args);
    va_end (Args);
}

static void (*s_pLogFlush) (void) = 0;

void FirmwareSetLogFlush (void (*pFlush) (void))
{
    s_pLogFlush = pFlush;
}

// The log is read from the card, where the board keeps it (hal_circle.cpp:
// 512 KB, padded to the sector). Two files, this session's and the previous
// one's, read through a second handle: FatFs is built without locking, and the
// board writes whole sectors only, so what a read sees is what the card holds.
void HostLogRead (bool bPrevious, THostChunk *pChunk)
{
    if (!bPrevious && s_pLogFlush != 0)
    {
        // What the board has logged since it last drained its ring, which on a
        // page opened a moment after the boot is the lines that matter.
        s_pLogFlush ();
    }

    static char Chunk[32 * 1024];       // no stack for it
    FIL File;
    if (f_open (&File, bPrevious ? "SD:/okapia-previous.log" : "SD:/okapia.log",
                FA_READ) != FR_OK)
    {
        return;
    }
    for (;;)
    {
        UINT nRead = 0;
        if (f_read (&File, Chunk, sizeof Chunk, &nRead) != FR_OK || nRead == 0)
        {
            break;
        }
        // A chunk of nothing but blanks is the padding past the end of the text.
        bool bBlank = true;
        for (UINT i = 0; i < nRead && bBlank; i++)
        {
            bBlank = Chunk[i] == ' ';
        }
        if (bBlank)
        {
            break;
        }
        pChunk (Chunk, nRead);
    }
    f_close (&File);
}

/*
 *  Time
 */
// A pause that lets the Wi-Fi run. Its driver and supplicant are tasks of
// Circle's cooperative scheduler, and a menu waiting in CTimer::MsDelay holds
// them still: a scan asked for from the network page would never come back.
void HostSleep (unsigned nMs)
{
    if (CScheduler::IsActive ())
    {
        CScheduler::Get ()->MsSleep (nMs);
    }
    else
    {
        CTimer::Get ()->MsDelay (nMs);
    }
}

void HostPoll (void)
{
    BoardNetPoll ();
    ClockPoll ();
}

unsigned HostLocalTime (void)
{
    return CTimer::Get ()->GetLocalTime ();
}

int HostTimeZoneMinutes (void)
{
    return CTimer::Get ()->GetTimeZone ();
}

THostClockSource HostClockSource (const char **ppDetail)
{
    switch (ClockSource (ppDetail))
    {
    case ClockFromNetwork:  return HostClockNetwork;
    case ClockFromMac:      return HostClockMac;
    case ClockFromVolume:   return HostClockVolume;
    default:                return HostClockBuild;
    }
}

void HostClockSetZone (const char *pKey, int nFixedMinutes)
{
    ClockSetZone (pKey, nFixedMinutes);
}

void HostClockSetServer (const char *pServer)
{
    ClockSetServer (pServer);
}

bool HostClockIsHosts (void)
{
    return false;
}

/*
 *  The machine
 */
THostKind HostKind (void)
{
    return HostBoard;
}

const char *HostMachineName (void)
{
    return CMachineInfo::Get ()->GetMachineName ();
}

void HostMenuPrelude (void)
{
    // Before the window, once per power-on, and only when `netbench` asks:
    // the network measured with nothing else running.
    NetBenchRun ();
}

/*
 *  The picture
 */
bool HostOutputClaim (TSurface *pSurface)
{
    CBcmFrameBuffer *pOutput = FwOutputClaim ();
    if (pOutput == 0)
    {
        return false;
    }
    if (pOutput->GetDepth () != 32)
    {
        CLogger::Get ()->Write ("firmware", LogWarning, "Expected 32 bpp, got %u",
                                pOutput->GetDepth ());
        return false;
    }
    pSurface->pPixels = (unsigned char *) (uintptr) pOutput->GetBuffer ();
    pSurface->nWidth  = pOutput->GetWidth ();
    pSurface->nHeight = pOutput->GetHeight ();
    pSurface->nPitch  = pOutput->GetPitch ();
    return true;
}

void HostOutputPresent (int, int, unsigned, unsigned)
{
    // Scanned out by the hardware: what is in the buffer is on the screen.
}

// Report directives present in the card's top-level config. The GPU owns
// conditional filters and include processing, so presence only is reported.
static bool HasHdmiDirectives (bool *pbAvailable)
{
    FILE *pFile = fopen ("/config.txt", "r");
    *pbAvailable = pFile != nullptr;
    if (pFile == nullptr)
        return false;

    static const char *const Keys[] = {
        "hdmi_mode", "hdmi_group", "hdmi_timings", "hdmi_cvt",
        "framebuffer_width", "framebuffer_height", "max_framebuffer_width", "max_framebuffer_height"
    };

    char Line[192];
    bool bFound = false;
    while (!bFound && fgets (Line, sizeof Line, pFile) != nullptr)
    {
        const char *p = Line + strspn (Line, " \t");
        const size_t nKey = strcspn (p, " \t:=\r\n");
        for (const char *pKey : Keys)
        {
            if (strlen (pKey) == nKey && strncmp (p, pKey, nKey) == 0)
            {
                bFound = true;
                break;
            }
        }
    }
    fclose (pFile);
    return bFound;
}

void HostOutputDescribe (THostOutputFacts *pFacts)
{
    const TFwMonitor &Monitor = FwOutputMonitor ();
    pFacts->nPreferredWidth  = Monitor.nPreferredWidth;
    pFacts->nPreferredHeight = Monitor.nPreferredHeight;
    snprintf (pFacts->Monitor, sizeof pFacts->Monitor, "%s", Monitor.Name);
    pFacts->bHdmiDirectives = HasHdmiDirectives (&pFacts->bConfigAvailable);
}

/*
 *  Input
 */
bool HostInputWatch (void)                          { return FwInputWatch (); }
void HostInputBounds (unsigned nWidth, unsigned nHeight) { FwInputBounds (nWidth, nHeight); }
bool HostInputNext (TEvent *pOut)                   { return FwInputNext (pOut); }
void HostInputPointer (int *pX, int *pY)            { FwInputPointer (pX, pY); }
unsigned HostInputSeenModifiers (void)              { return FwInputSeenModifiers (); }
bool HostInputSeenKey (unsigned char ucUsage)       { return FwInputSeenKey (ucUsage); }
bool HostInputSeenAnything (void)                   { return FwInputSeenAnything (); }
bool HostQuitRequested (void)                       { return false; }

/*
 *  The card
 */
bool HostCardSpace (unsigned long *pTotalMB, unsigned long *pFreeMB)
{
    // FatFs counts clusters rather than bytes; turned into megabytes here, in
    // that order: the multiplication overflows a 32-bit count of bytes on any
    // card worth using.
    FATFS *pFS = 0;
    DWORD nFreeClusters = 0;
    if (f_getfree ("SD:", &nFreeClusters, &pFS) != FR_OK || pFS == 0)
    {
        return false;
    }
    const unsigned long nSectorsPerMB = (1024UL * 1024) / FF_MAX_SS;
    *pTotalMB = (unsigned long) (pFS->n_fatent - 2) * pFS->csize / nSectorsPerMB;
    *pFreeMB  = (unsigned long) nFreeClusters * pFS->csize / nSectorsPerMB;
    return true;
}

bool HostFileExists (const char *pPath)
{
    char Full[96];
    snprintf (Full, sizeof Full, "SD:%s", pPath);
    FILINFO Info;
    return f_stat (Full, &Info) == FR_OK;
}

// Create the file at its full size. f_lseek past the end of a file opened for
// writing is what allocates the clusters; f_expand would say so more plainly
// but FF_USE_EXPAND is 0 in circle-stdlib's ffconf.h, which lives in external/
// and is not ours to change.
bool HostFileCreate (const char *pPath, unsigned long nMB)
{
    char Full[96];
    snprintf (Full, sizeof Full, "SD:%s", pPath);
    FIL File;
    if (f_open (&File, Full, FA_CREATE_NEW | FA_WRITE) != FR_OK)
    {
        CLogger::Get ()->Write ("firmware", LogError, "%s: cannot create", Full);
        return false;
    }
    const FSIZE_t nBytes = (FSIZE_t) nMB * 1024 * 1024;
    const FRESULT nSeek = f_lseek (&File, nBytes);
    const bool bWhole = nSeek == FR_OK && f_tell (&File) == nBytes;
    f_close (&File);

    if (!bWhole)
    {
        // A file that could not be given its whole size is not a smaller
        // volume, it is a volume the Macintosh would run off the end of. It
        // goes, rather than being left for somebody to find later.
        CLogger::Get ()->Write ("firmware", LogError,
                                "%s: only part of %lu MB could be allocated", Full, nMB);
        f_unlink (Full);
        return false;
    }
    return true;
}

// f_unlink and not remove(): the newlib glue's remove() deletes the file and
// then leaves something behind that wedges the next fopen — the ROM never
// opened and the kernel stopped dead, with no log at all.
THostRemove HostFileRemove (const char *pPath)
{
    char Full[96];
    snprintf (Full, sizeof Full, "SD:%s", pPath);
    const FRESULT nResult = f_unlink (Full);
    return nResult == FR_OK ? HostRemoved : nResult == FR_NO_FILE ? HostAbsent : HostRemoveFailed;
}

/*
 *  What this board offers
 */
unsigned HostSoundOutputs (void)
{
    // Circle builds USB audio for a Pi 4 and a Pi 5 only (lib/sound/Makefile:37),
    // so on a Pi 3 the entry is there and grey.
    unsigned nMask = (1u << SoundOff) | (1u << SoundHDMI) | (1u << SoundJack);
#if RASPPI >= 4
    nMask |= 1u << SoundUSB;
#endif
    return nMask;
}

/*
 *  The network
 */
unsigned HostNetAvailable (void)
{
    return (1u << NetworkOff) | (1u << NetworkCable) | (WlanAvailable () ? 1u << NetworkWiFi : 0);
}

unsigned HostNetRunning (void)
{
    switch (BoardNetKind ())
    {
    case BoardNetWired: return NetworkCable;
    case BoardNetWLAN:  return NetworkWiFi;
    default:            return NetworkOff;
    }
}

bool HostNetTried (void)                    { return BoardNetTried (); }
bool HostNetStart (void)                    { return BoardNetStart (); }
bool HostNetLinkUp (unsigned *pMbps)        { return BoardNetLinkUp (pMbps); }
bool HostWlanScan (void)                    { return WlanScan (); }

unsigned HostWlanSeen (TNetworkSeen *pSeen, unsigned nMax)
{
    static okapia_wlan_bss Found[48];
    const unsigned nFound = WlanSeen (Found, nMax < 48 ? nMax : 48);
    for (unsigned i = 0; i < nFound; i++)
    {
        memcpy (pSeen[i].SSID, Found[i].ssid, sizeof pSeen[i].SSID);
        pSeen[i].nDbm     = Found[i].dbm;
        pSeen[i].bSecured = Found[i].secured != 0;
    }
    return nFound;
}

TNetworkState HostWlanState (char *pSubject, unsigned nSize, int *pDbm, unsigned nSeen)
{
    switch (WlanState (pSubject, nSize, pDbm))
    {
    case OKAPIA_WLAN_SEARCHING: return NetStateSearching;
    case OKAPIA_WLAN_JOINING:   return NetStateJoining;
    case OKAPIA_WLAN_CONNECTED: return NetStateConnected;
    case OKAPIA_WLAN_REFUSED:   return NetStateRefused;
    case OKAPIA_WLAN_LOST:      return NetStateLost;
    case OKAPIA_WLAN_IDLE:      return nSeen == 0 ? NetStateSearching : NetStateReady;
    default:                    return NetStateStarting;
    }
}

bool HostWlanApply (const char *pSSID, const char *pPassword, const char *pCountry)
{
    return WlanApply (pSSID, pPassword, pCountry);
}
