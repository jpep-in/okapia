//
// sys_unix_desktop.cpp — upstream's Unix/sys_unix.cpp, with a flush policy for
// the guest's disk images.
//
// Upstream writes a disk image with a bare write() and closes it with a bare
// close(): the host's page cache then stands between the guest and the medium,
// which AGENTS.md and docs/project/decisions.md ("No write-back cache between
// the guest and the card") allow only with a flush policy. This file is that
// policy, compiled in place of sys_unix.cpp for both engines (Makefile:
// re_sys_unix.o), so upstream's file is used unchanged:
//
//  - every write to a writable image is followed by a barrier
//    (HostBarrierFile()): the writes reach the medium in the order the File
//    Manager issued them, as on the card, and an abrupt stop of the host
//    loses the last of them, never an earlier one;
//  - closing a writable image empties the drive's cache (HostSyncFile()), so
//    that Shut Down means what the log says it means.
//
// A write whose barrier fails is reported to the guest as a failed write: the
// File Manager then knows, where a write that seemed to succeed would not.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#define Sys_write OkapiaRealSysWrite
#define Sys_close OkapiaRealSysClose
#include "sys_unix.cpp"
#undef Sys_write
#undef Sys_close

#include "sync_desktop.h"

// The descriptor to sync, or -1 for a handle that writes nowhere this host
// owns: read-only, a CD-ROM, a sparse bundle or other generic disk.
static int SyncTarget (void *arg)
{
	const mac_file_handle *fh = (const mac_file_handle *)arg;
	if (fh == NULL || fh->read_only || fh->is_cdrom || fh->generic_disk != NULL)
		return -1;
	return fh->fd;
}

size_t Sys_write(void *arg, void *buffer, loff_t offset, size_t length)
{
	const size_t written = OkapiaRealSysWrite(arg, buffer, offset, length);
	const int fd = SyncTarget(arg);
	if (written > 0 && fd >= 0 && !HostBarrierFile(fd)) {
		CLogger::Get()->Write("okapia-disk", LogError,
		                      "A disk image did not confirm a write of %u bytes: reported as failed",
		                      (unsigned)length);
		return 0;
	}
	return written;
}

void Sys_close(void *arg)
{
	const int fd = SyncTarget(arg);
	if (fd >= 0 && !HostSyncFile(fd))
		CLogger::Get()->Write("okapia-disk", LogError, "A disk image did not confirm its last writes");
	OkapiaRealSysClose(arg);
}
