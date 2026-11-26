//
// sync_desktop.cpp — what the card gives the guest for free on the board, asked
// of the host: that a write has reached the medium.
//
// On the Pi every write goes through FatFs to the card, with no cache in
// between (docs/topics/storage.md). A desktop puts its page cache there, and on
// macOS fsync() itself only hands the data to the drive, whose own cache may
// hold it: F_FULLFSYNC empties that too, F_BARRIERFSYNC orders it (what was
// written before reaches the medium before what follows) without waiting for
// it. A file system that knows neither, such as FAT on an older macOS, gets
// fsync().
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "sync_desktop.h"
#include "okapia_card.h"

#include <fcntl.h>
#include <unistd.h>

bool HostSyncFile (int nFD)
{
    if (nFD < 0)
    {
        return false;
    }
#ifdef F_FULLFSYNC
    if (fcntl (nFD, F_FULLFSYNC) == 0)
    {
        return true;
    }
#endif
    return fsync (nFD) == 0;
}

bool HostBarrierFile (int nFD)
{
    if (nFD < 0)
    {
        return false;
    }
#ifdef F_BARRIERFSYNC
    if (fcntl (nFD, F_BARRIERFSYNC) == 0)
    {
        return true;
    }
#endif
    return fsync (nFD) == 0;
}

bool HostSyncPath (const char *pPath)
{
    const int nFD = open (pPath, O_RDONLY);
    const bool bSynced = HostSyncFile (nFD);
    if (nFD >= 0)
    {
        close (nFD);
    }
    return bSynced;
}

bool HostSyncDirectory (const char *pDirectory)
{
    return HostSyncPath (pDirectory);
}

bool CardSync (const char *pPath)
{
    return HostSyncPath (CardPath (pPath));
}
