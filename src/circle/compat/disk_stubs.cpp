/*
 * disk_stubs.cpp — disk image formats Okapia does not read.
 *
 * disk.cpp walks a list of factories to identify an image. Sparse bundles are a
 * macOS bundle format: a directory of band files, meaningless on a FAT card.
 * Declining cleanly lets the plain-image factory take over.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "sysdeps.h"
#include "disk_unix.h"

disk_generic::status disk_sparsebundle_factory (const char *path, bool read_only,
                                                disk_generic **disk)
{
    (void) path; (void) read_only; (void) disk;
    return disk_generic::DISK_UNKNOWN;      // not ours: try the next factory
}
