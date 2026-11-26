//
// okapia_card_check.h — what a volume is as an Okapia card, read only.
//
// The installer's diagnosis (okapia-installer core/src/card.rs), in C++ for the
// welcome page: whether the Raspberry Pi can start from the volume (FAT32, and
// clusters large enough to be quick), whether Okapia is on it and which build,
// and whether the board was last switched off without Shut Down. Nothing here
// writes.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_card_check_h
#define _okapia_card_check_h

enum TCardFs
{
    CardFsUnknown,                      // statfs said nothing
    CardFsFat12, CardFsFat16, CardFsFat32,
    CardFsOther                         // exFAT, APFS, HFS+…: FsName says which
};

// A mounted volume, as the host sees it (HostCards, HostVolumeOf).
struct TCardVolume
{
    char               Name[64];        // "OKAPIA"
    char               Mount[256];      // "/Volumes/OKAPIA"
    char               FsName[16];      // "msdos", "exfat", "apfs"
    TCardFs            Fs;
    unsigned long long nTotal, nFree;   // bytes
    unsigned           nCluster;        // bytes, FAT only; 0 unknown
    bool               bRemovable;      // a card in a reader, not a folder
};

// FAT12, FAT16 or FAT32 told apart by the cluster count, as the FAT
// specification itself does.
TCardFs CardFatKind (unsigned long long nClusters);

// The Raspberry Pi 4 files a card cannot start without, and Okapia's kernel.
static const char CARD_KERNEL[] = "kernel8-rpi4.img";

struct TCardDiagnosis
{
    bool     bFatOK;                    // FAT32: the Pi starts from it
    bool     bSmallClusters;            // FAT32 under 32 KB clusters: it works, slower
    bool     bKernel;                   // CARD_KERNEL is there
    char     Missing[96];               // the boot files that are not, "" when all are
    char     Version[32];               // the build found in the kernel, or the log's
    bool     bUncleanShutdown;          // the last session did not end with Shut Down
    unsigned nSessions;                 // starts the log records
};

// pRoot is the volume's root on the host; pVolume what the host said of it.
void CardDiagnose (const char *pRoot, const TCardVolume *pVolume, TCardDiagnosis *pOut);

// The pieces, for the host's tests.
// The build name gen-version.sh writes, YYMMDD-sha or YYMMDD-sha+xxxx, the
// first one found in pData. False when there is none.
bool CardFindVersion (const unsigned char *pData, unsigned long nBytes, char *pOut, unsigned nSize);
// okapia.log's sessions: how many started, the last one's build, and whether it
// left no Macintosh running — every "Initialising the emulator" followed by a
// "Shut down cleanly". A log that wrapped is read in order.
void CardLogSummary (const char *pText, unsigned *pnSessions, char *pVersion, unsigned nVersion,
                     bool *pbClean);

#endif
