/*
 * okapia_chooser.h — the screen that asks which System to start.
 *
 * The first real screen of the firmware, and the reason the rest of it exists:
 * a card can hold several Systems, and choosing between them meant editing a
 * text file on another computer.
 *
 * Nothing here knows what HFS is, or what a preferences file looks like. It is
 * handed a list of volumes and answers what the user asked for; the caller does
 * the reading and the writing. That is what lets the whole screen be driven by
 * synthetic events on a development machine, which is where its behaviour is
 * checked — and it is the same division that already holds between the drawing
 * primitives and the frame buffer.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_chooser_h
#define _okapia_chooser_h

#include "okapia_widgets.h"

// Enough for any card worth booting from, and the same ceiling the kernel's own
// inventory uses.
static const unsigned CHOOSER_MAX = 8;

// One volume, as the chooser needs it. The strings are copied in rather than
// pointed at: the caller's inventory is free to go away, and a screen that
// outlives its data is a screen that draws freed memory.
// Which processor a volume's System is built for, and so which emulator can
// start it. A value the firmware is handed, never a reading it makes: nothing
// here knows what a resource map is, and everything that does stays in
// src/circle (hfs_volume_circle.h).
enum TChooserCPU
{
    CPUUnknown,         // no readable version, so no idea
    CPU68k,
    CPUPowerPC,
    CPUUniversal        // either, and one day somebody will have to choose
};

// How a volume is handed to the Macintosh, which is a choice and not a reading:
// nothing in an image says what it is. `disk` puts it on the hard disk driver,
// `cdrom` on the .AppleCD one (cdrom.cpp:324, where read-only is hardcoded), and
// find_hfs_partition() — the same routine, written twice, in disk.cpp:120 and
// cdrom.cpp:194 — reads a flat image and a partitioned one either way. So these
// three values are the whole vocabulary the preferences can express about one
// volume, and there is no fourth: a CD-ROM cannot be made writable.
enum TChooserMount
{
    MountHD,                            // disk <path>
    MountHDReadOnly,                    // disk *<path>   (disk.cpp:161)
    MountCD                             // cdrom <path>
};

struct TChooserVolume
{
    char Path[64];                      // as the preferences name it, no * prefix
    char Name[28];
    char System[32];                    // "7.1.2"; empty when there is none
    TChooserCPU CPU;                    // and which processor it is built for
    // Which emulator starts it. A System built for one processor settles it —
    // there is nothing to ask — and a universal one does not, so this is the
    // user's answer and it is remembered per volume. Only CPU68k and
    // CPUPowerPC are ever stored here; ChooserSync() forces the settled cases
    // so nothing downstream has to repeat the rule.
    TChooserCPU Engine;
    bool bBootable;                     // it has a blessed folder
    bool bClean;                        // it was unmounted properly
    TChooserMount Mount;                // which drive it goes in, and how
    bool bMounted;                      // listed in the preferences at all
    unsigned long nFreeKB;
    unsigned long nTotalKB;             // what a read-only volume is asked for
};

struct TChooser
{
    TChooserVolume Volumes[CHOOSER_MAX];
    unsigned       nCount;
    int            nStartup;            // the volume that boots, or -1
    // Which emulator this kernel image actually carries — one per image, since
    // the two cores define the same symbols (docs/contributing/build.md). Handed in as data so
    // the screen can say that starting the other one means going round through
    // the loader, and so a test can drive both without being rebuilt.
    TChooserCPU    Built;
};

enum TChooserAction
{
    ChooserNothing,                     // the model changed; repaint and carry on
    ChooserStart,
    ChooserSettings,
    // On the main screen rather than behind the settings: what Okapia found is
    // the first thing wanted when a machine will not start, and a page reached
    // in two clicks is a page nobody opens at that moment.
    ChooserInformation,
    // The command row at the top of the list. What it makes lands in that list,
    // which is why it is asked for there and not from the footer, where the
    // buttons are about the machine rather than about the card.
    ChooserNewVolume,
    ChooserForgetPram,
    ChooserShutDown
};

// Lays the screen out for this surface and paints it. Call again after anything
// that changes how much room a row wants; ChooserRepaint is enough otherwise.
void ChooserDraw (TSurface *pSurface, TChooser *pChooser);
void ChooserRepaint (TSurface *pSurface);

// The components, for ScreenInit. They are the chooser's own array and stay put
// until the next ChooserDraw.
unsigned ChooserWidgets (TWidget **ppList);

// The engine rule on its own, because the answer is wanted on an ordinary boot
// where no screen is ever laid out: a System built for one processor settles
// which emulator starts it, a universal one keeps what was remembered, and
// anything else falls back to what this kernel already carries. ChooserSync()
// calls it too, so the screen and the silent boot cannot disagree.
void ChooserSettleEngines (TChooser *pChooser);

// Brings the controls in step with whichever volume is selected: what is true
// of one is not true of the next, and a tick box left showing the last one's
// answer is worse than no tick box at all.
void ChooserSync (TChooser *pChooser);

// What the control at nIndex means. The model is brought up to date first, so a
// control that only changes the model answers ChooserNothing and the caller has
// nothing to do but repaint.
// nCell is which column of the list was operated, as the screen loop reports
// it: 0 for the row itself, 1 for the startup mark, 2 for mounted. Anything
// that is not the list ignores it.
TChooserAction ChooserOperate (TChooser *pChooser, int nIndex, unsigned nCell);

// The `disk` lines this model asks for, in order, and how many. The startup
// volume comes first because Basilisk offers them to the ROM in the order
// disk.cpp reads them, and a volume to be mounted read-only carries the `*`
// that disk.cpp:161 looks for. A volume that is not mounted is simply absent,
// and so is one mounted as a CD-ROM: that one is not a disk at all, it belongs
// to the `cdrom` lines below.
//
// It is a separate answer rather than something written from inside the screen:
// what the user chose and what a preferences file says are two different
// things, and only one of them can be checked on a development machine.
static const unsigned CHOOSER_LINE = 66;
unsigned ChooserDiskLines (const TChooser *pChooser, char Lines[][CHOOSER_LINE],
                           unsigned nMax);

// The `cdrom` lines, same contract: one path per volume mounted as a CD-ROM,
// with no `*` — CDROMInit() opens them read-only whatever anybody asks
// (cdrom.cpp:324). A volume is in exactly one of the two lists.
//
// The startup volume comes first here too. Not for the ROM's sake — it is told
// which *driver* to start from and not which drive — but because with two discs
// in two drives, first is the one the CD driver offers first, and that is the
// only thing left to distinguish them.
unsigned ChooserCdromLines (const TChooser *pChooser, char Lines[][CHOOSER_LINE],
                            unsigned nMax);

// The `engine` lines, same contract: "<path> 68k" or "<path> powerpc", one per
// volume whose System is universal. The settled ones are left out on purpose —
// a preference that repeats what the System already says is a preference that
// will one day contradict it.
unsigned ChooserEngineLines (const TChooser *pChooser, char Lines[][CHOOSER_LINE],
                             unsigned nMax);

// Which driver the Macintosh is to start from, as the `bootdriver` preference
// states it: main.cpp:139 writes it into the parameter RAM at 0x7a, which is
// where a Macintosh keeps its startup device, and SheepShaver reads the same
// preference (SheepShaver/src/main.cpp:113). CDROMRefNum, -62, for a startup
// volume mounted as a CD-ROM; 0 — no preference, the ROM's own order — for
// everything else.
//
// Answered here rather than written from inside the screen for the same reason
// the disk lines are: it is checkable on a development machine, and 0 must be
// written back as surely as -62, or a card that once started from a disc would
// go on trying for ever.
static const int CHOOSER_BOOT_CDROM = -62;      // cdrom.h:24
int ChooserBootDriver (const TChooser *pChooser);

// Which emulator the startup volume asks for, or the built-in one when there is
// no startup volume to ask. The kernel compares it with what it carries and
// goes round through the loader when they differ.
TChooserCPU ChooserStartupEngine (const TChooser *pChooser);

// Whether the last ChooserDraw ran out of room. The page takes its footer and
// its two bottom rows before the list, so the list absorbs a shortage by
// shrinking and nothing looks wrong — until the shortage is bigger than the
// list. Asked by the checks, in every language: French runs longer than English
// almost everywhere, and a row added against one language is a row that comes
// apart in the other.
bool ChooserOverflowed (void);

// Which volume is selected, or -1. The caller needs it to say what it is doing
// in the log, and the tests to say what they checked.
int ChooserSelected (void);

#endif
