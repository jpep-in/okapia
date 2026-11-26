//
// sync_desktop.h — writes made to reach the medium on a desktop (sync_desktop.cpp)
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sync_desktop_h
#define _sync_desktop_h

// Everything written to the file is on the medium, the drive's cache emptied
// where the host can ask for it. False when the file cannot say so.
bool HostSyncFile (int nFD);

// Everything written to the file before this call reaches the medium before
// anything written after it. Cheaper than HostSyncFile() where the host has a
// barrier; the same where it has not.
bool HostBarrierFile (int nFD);

// HostSyncFile() on a file or directory named by its host path: a directory
// is synced to make a rename or a new name in it last.
bool HostSyncPath (const char *pPath);
bool HostSyncDirectory (const char *pDirectory);

#endif
