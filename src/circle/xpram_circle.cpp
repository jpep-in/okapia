/*
 * xpram_circle.cpp — the Mac's parameter RAM, kept on the card.
 *
 * 256 bytes holding what the Mac remembers between sessions: startup disk,
 * sound volume, mouse tracking, the desktop pattern. Replaces
 * dummy/xpram_dummy.cpp, which names the file relatively — that resolves
 * against FatFs's current directory rather than against anything this kernel
 * chose, so the file's location was an accident that happened to work.
 *
 * Upstream saves it once, at a clean shutdown. That is a poor trade here: a
 * pulled plug is a case this project takes seriously, and losing the Mac's
 * settings to one is avoidable. XPRAMWatchdog() writes the file when anything
 * in it changes, called from the Mac's own PRAM access rather than from a
 * timer — see xpram_hook_circle.cpp.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"

#include <stdio.h>
#include <string.h>

#include "xpram.h"

#define FROM "okapia-pram"

// At the root of the card, beside the preferences and the disk images.
static const char XPRAM_FILE_NAME[] = "/BasiliskII_XPRAM";

// What is currently on the card, so a write only happens on a real change.
static uint8 s_Saved[XPRAM_SIZE];

void LoadXPRAM (const char *vmdir)
{
    (void) vmdir;                       // no VM directory: the card is the VM

    FILE *f = fopen (XPRAM_FILE_NAME, "rb");
    if (f == 0)
    {
        CLogger::Get ()->Write (FROM, LogNotice,
                                "No %s yet; the Mac starts with an empty PRAM",
                                XPRAM_FILE_NAME);
        memset (s_Saved, 0, sizeof s_Saved);
        return;
    }

    size_t nRead = fread (XPRAM, 1, XPRAM_SIZE, f);
    fclose (f);

    if (nRead != XPRAM_SIZE)
    {
        // Short means truncated, which means the last write did not finish.
        // Starting from zero is what a Mac does with a dead PRAM battery, and
        // it is honest; pretending a partial file is settings is not.
        CLogger::Get ()->Write (FROM, LogWarning,
                                "%s is %u bytes, not %u — starting from an empty PRAM",
                                XPRAM_FILE_NAME, (unsigned) nRead, XPRAM_SIZE);
        memset (XPRAM, 0, XPRAM_SIZE);
    }
    else
    {
        CLogger::Get ()->Write (FROM, LogNotice, "Read %s", XPRAM_FILE_NAME);
    }

    memcpy (s_Saved, XPRAM, XPRAM_SIZE);
}

void SaveXPRAM (void)
{
    FILE *f = fopen (XPRAM_FILE_NAME, "wb");
    if (f == 0)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Cannot write %s", XPRAM_FILE_NAME);
        return;
    }

    size_t nWritten = fwrite (XPRAM, 1, XPRAM_SIZE, f);
    if (fclose (f) != 0 || nWritten != XPRAM_SIZE)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "%s: wrote %u of %u bytes",
                                XPRAM_FILE_NAME, (unsigned) nWritten, XPRAM_SIZE);
        return;
    }

    memcpy (s_Saved, XPRAM, XPRAM_SIZE);
}

void ZapPRAM (void)
{
    remove (XPRAM_FILE_NAME);

    // The live copy has to go too, not just the file and the shadow. Clearing
    // only those leaves XPRAM holding the settings the user asked to discard,
    // and the watchdog below would write them straight back at the Mac's very
    // next clock access — a few milliseconds later, since MacOS reads the
    // clock constantly. Zapping would silently undo itself.
    memset (XPRAM, 0, XPRAM_SIZE);
    memset (s_Saved, 0, sizeof s_Saved);

    CLogger::Get ()->Write (FROM, LogNotice, "PRAM zapped");
}

/*
 *  Write the PRAM back as soon as it changes.
 *
 *  Driven by the event, not by a timer: every read and write of the Mac's
 *  clock/PRAM chip arrives as M68K_EMUL_OP_CLKNOMEM, and that is the only path
 *  by which XPRAM changes once the Mac is running (emul_op.cpp:159 and :170).
 *  xpram_hook_circle.cpp watches for it, so the comparison happens exactly when
 *  something could have changed and never otherwise.
 *
 *  It runs in the 68k thread, the same context as Sys_write, so writing to the
 *  card here is allowed — which it would not be from the tick handler, that one
 *  running at IRQ level.
 */

void XPRAMWatchdog (void)
{
    if (memcmp (s_Saved, XPRAM, XPRAM_SIZE) == 0)
    {
        return;
    }

    // The Mac writes PRAM one byte at a time, so a single settings change
    // arrives as a burst of these. Say it once and drop to debug after: the
    // file is a single sector, and writing it three times in ten milliseconds
    // costs less than the machinery needed to coalesce the burst.
    static bool s_bReported;
    CLogger::Get ()->Write (FROM, s_bReported ? LogDebug : LogNotice,
                            "PRAM changed, writing it back");
    s_bReported = true;

    SaveXPRAM ();
}
