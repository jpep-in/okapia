//
// wraps_desktop.cpp — the Pi's --wrap hooks, on a linker that has no --wrap.
//
// On the board, ld redirects every call to EmulOp, extfs_write and the two
// encoding functions to Okapia's __wrap_ versions, and __real_ to upstream's
// (src/kernel/Makefile, ENGINE_WRAPS). Apple's ld64 has neither. So the desktop
// Makefile renames upstream's definition in the one file that holds it
// (-DEmulOp=OkapiaRealEmulOp, and so on), and this file gives the original name
// to the hook and the __real_ name to upstream's code. The hooks themselves are
// the Pi's, unchanged: emul_op_hook_okapia.cpp, extfs_sync_okapia.cpp,
// mac_encoding_okapia.cpp.
//
// Two more: the card's paths, which upstream opens as they are written in the
// preferences ("/boot71.img", "/shared"), are found in the card folder.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "sysdeps.h"
#include "main.h"
#include "prefs.h"
#include "okapia_card.h"

#include <string.h>
#include <sys/types.h>

#ifndef SHEEPSHAVER
// EmulOp: the Macintosh going round, and QuickDraw (emul_op_hook_okapia.cpp).
// The 68k engine's only, as on the board: the PowerPC one has no hook on it.
void OkapiaRealEmulOp (uint16 opcode, M68kRegisters *r);
extern "C" void __wrap__Z6EmulOptP13M68kRegisters (uint16 opcode, M68kRegisters *r);

extern "C" void __real__Z6EmulOptP13M68kRegisters (uint16 opcode, M68kRegisters *r)
{
    OkapiaRealEmulOp (opcode, r);
}

void EmulOp (uint16 opcode, M68kRegisters *r)
{
    __wrap__Z6EmulOptP13M68kRegisters (opcode, r);
}
#endif

#ifdef SHEEPSHAVER
// Open Transport's three entry points into SheepShaver's DLPI module, which
// ether_okapia.cpp logs (the PowerPC engine's only). Its __real_ references
// carry their symbol names verbatim (__asm__), so the definitions here do too.
#include "cpu_emulation.h"
#include "ether.h"

uint8 OkapiaRealInitStreamModule (void *theID);
void  OkapiaRealTerminateStreamModule (void);
int   OkapiaRealEtherOpen (queue_t *rdq, void *dev, int flag, int sflag, void *creds);
extern "C" uint8 __wrap__Z16InitStreamModulePv (void *theID);
extern "C" void  __wrap__Z21TerminateStreamModulev (void);
extern "C" int   __wrap__Z10ether_openP5queuePviiS1_ (queue_t *rdq, void *dev, int flag,
                                                       int sflag, void *creds);

uint8 RealInitStreamModule (void *theID) __asm__ ("__real__Z16InitStreamModulePv");
uint8 RealInitStreamModule (void *theID) { return OkapiaRealInitStreamModule (theID); }
void  RealTerminateStreamModule (void) __asm__ ("__real__Z21TerminateStreamModulev");
void  RealTerminateStreamModule (void) { OkapiaRealTerminateStreamModule (); }
int   RealEtherOpen (queue_t *rdq, void *dev, int flag, int sflag, void *creds)
    __asm__ ("__real__Z10ether_openP5queuePviiS1_");
int   RealEtherOpen (queue_t *rdq, void *dev, int flag, int sflag, void *creds)
{
    return OkapiaRealEtherOpen (rdq, dev, flag, sflag, creds);
}

uint8 InitStreamModule (void *theID)    { return __wrap__Z16InitStreamModulePv (theID); }
void  TerminateStreamModule (void)      { __wrap__Z21TerminateStreamModulev (); }
int   ether_open (queue_t *rdq, void *dev, int flag, int sflag, void *creds)
{
    return __wrap__Z10ether_openP5queuePviiS1_ (rdq, dev, flag, sflag, creds);
}
#endif

// extfs_write: every write to the shared folder synced (extfs_sync_okapia.cpp).
ssize_t OkapiaRealExtfsWrite (int fd, void *buffer, size_t length);
extern "C" ssize_t __wrap__Z11extfs_writeiPvm (int fd, void *buffer, size_t length);

extern "C" ssize_t __real__Z11extfs_writeiPvm (int fd, void *buffer, size_t length)
{
    return OkapiaRealExtfsWrite (fd, buffer, length);
}

ssize_t extfs_write (int fd, void *buffer, size_t length)
{
    return __wrap__Z11extfs_writeiPvm (fd, buffer, length);
}

// The names between the Mac and the host (mac_encoding_okapia.cpp), which
// replace upstream's whole.
extern "C" const char *__wrap__Z25host_encoding_to_macromanPKc (const char *pName);
extern "C" const char *__wrap__Z25macroman_to_host_encodingPKc (const char *pName);

const char *host_encoding_to_macroman (const char *pName)
{
    return __wrap__Z25host_encoding_to_macromanPKc (pName);
}

const char *macroman_to_host_encoding (const char *pName)
{
    return __wrap__Z25macroman_to_host_encodingPKc (pName);
}

// The disk images and CD-ROMs, opened by upstream's sys_unix.cpp under the
// names the preferences give them.
void *OkapiaUnixSysOpen (const char *name, bool read_only, bool is_cdrom);

void *Sys_open (const char *name, bool read_only, bool is_cdrom)
{
    return OkapiaUnixSysOpen (CardPath (name), read_only, is_cdrom);
}

// The shared folder's root, which extfs.cpp reads from the preferences itself.
const char *OkapiaCardPrefsString (const char *name, int index)
{
    const char *pValue = PrefsFindString (name, index);
    return strcmp (name, "extfs") == 0 ? CardPath (pValue) : pValue;
}
