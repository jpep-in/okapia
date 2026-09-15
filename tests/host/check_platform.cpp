/*
 * check_platform.cpp — the platform answers nobody sees going wrong.
 *
 * All three are decidable without a card, an emulator or a screen, and all
 * three fail quietly: a preferences line that disappears is noticed the day the
 * setting was needed, a System read as the wrong sort after the boot menu has
 * offered the wrong emulator, and a memory plan that is out by a megabyte gives
 * a Macintosh reading someone else's bytes rather than an error.
 *
 * The preferences half runs against the *real* keyword tables — upstream's
 * common_prefs_items and our platform_prefs_items are linked in, not mocked —
 * so a keyword added to either is covered by these checks the day it is added.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "hfs_volume_circle.h"
#include "mac_layout.h"
#include "mac_ram_circle.h"
#include "compositor_circle.h"
#include "video_sizes_circle.h"
#include "rom_chime.h"

// prefs_circle.cpp. Declared here rather than in a header of its own: it has
// exactly one caller in the kernel, SavePrefs(), a few lines below it.
extern unsigned PrefsCollectUnknown (const char *pPath, char *pBuffer, size_t nSize,
                                     unsigned *pnKept);

// prefs_items.cpp calls it from AddPrefsDefaults(); the serial ports are not
// what is under test.
void SysAddSerialPrefs (void) {}

// hfs_volume_circle.cpp reaches the card through these. Nothing below mounts a
// volume — the flavour rule is asked about a version, not about an image — so
// they only have to exist.
size_t Sys_read (void *fh, void *buffer, long long offset, size_t length)
{
    return pread ((int) (long) fh, buffer, length, (off_t) offset);
}
void *Sys_open (const char *pName, bool, bool)
{
    int nFD = open (pName, O_RDONLY);
    return nFD < 0 ? 0 : (void *) (long) nFD;
}
void Sys_close (void *fh) { close ((int) (long) fh); }

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s %s\n", bOK ? "ok  " : "FAIL ", pWhat);
    if (!bOK)
    {
        s_nFailures++;
    }
}

/*
 *  Preferences: the lines this build does not understand
 */

static const char TMP[] = "check_platform.tmp";

static void Write (const char *pContents)
{
    FILE *f = fopen (TMP, "w");
    fputs (pContents, f);
    fclose (f);
}

static void CheckUnknownLines (void)
{
    printf ("\npreference lines nobody declared\n");

    char     Buffer[512];
    unsigned nKept  = 0;
    unsigned nFound = 0;

    // "ramsize" is upstream's, "hfsrepair" is ours: both tables are consulted,
    // and a line is only kept when neither claims its keyword.
    Write ("ramsize 268435456\n"
           "hfsrepair true\n"
           "gfxaccel true\n");
    nFound = PrefsCollectUnknown (TMP, Buffer, sizeof Buffer, &nKept);
    Expect (nFound == 1 && nKept == 1, "only one line of three is unknown");
    Expect (strcmp (Buffer, "gfxaccel true\n") == 0,
            "and it comes back whole, value included");

    // A value with spaces in it, which is the case a keyword-and-value split
    // gets wrong: everything after the keyword belongs to the value.
    Write ("machine /Mac OS 9.image | sheepshaver | /macosrom\n");
    nFound = PrefsCollectUnknown (TMP, Buffer, sizeof Buffer, &nKept);
    Expect (nKept == 1
            && strcmp (Buffer, "machine /Mac OS 9.image | sheepshaver | /macosrom\n") == 0,
            "the spaces in a value are kept");

    // The header SavePrefs() writes is regenerated at every save, so keeping
    // its comments would double the file line by line.
    Write ("# a comment\n"
           "; another\n"
           "\n"
           "   \n"
           "ramsize 42\n");
    nFound = PrefsCollectUnknown (TMP, Buffer, sizeof Buffer, &nKept);
    Expect (nFound == 0 && nKept == 0,
            "comments, empty lines and blank lines are ignored");

    // Nothing may be lost quietly: what does not fit is still counted, and
    // SavePrefs() warns on the difference.
    Write ("aaaa 1\nbbbb 2\ncccc 3\n");
    nFound = PrefsCollectUnknown (TMP, Buffer, 10, &nKept);
    Expect (nFound == 3, "lines that do not fit are counted");
    Expect (nKept < nFound, "and not written");
    Expect (strlen (Buffer) < 10, "the buffer is never overrun");

    // A save on a card that has no preferences file yet, which is the first
    // boot: SavePrefs() calls this before creating the file.
    unlink (TMP);
    nFound = PrefsCollectUnknown (TMP, Buffer, sizeof Buffer, &nKept);
    Expect (nFound == 0 && nKept == 0 && Buffer[0] == '\0',
            "a missing file reports nothing");
}

/*
 *  Which emulator can boot this System
 */

static THfsSystemFlavour Flavour (unsigned nMajor, unsigned nMinor, unsigned nBugfix,
                                  bool bNative)
{
    THfsSystemVersion Version;
    memset (&Version, 0, sizeof Version);
    Version.nMajor      = nMajor;
    Version.nMinor      = nMinor;
    Version.nBugfix     = nBugfix;
    Version.bNativeCode = bNative;
    return HfsFlavourOf (&Version);
}

static void CheckFlavour (void)
{
    printf ("\nwhat sort of System\n");

    // The two volumes staged in qemu/sd-contents/, measured 2026-09-04: these
    // two lines are the ones that have a right answer outside this file.
    Expect (Flavour (7, 1, 2, false) == HfsFlavour68k,
            "7.1.2 with no native fragment: 68k, nothing to ask");
    Expect (Flavour (7, 6, 1, true) == HfsFlavourUniversal,
            "7.6.1 with native fragments: universal, so ask");

    // The lower bound is SheepShaver's earliest correctives, 7.5.2. Below it,
    // a native fragment changes nothing: no emulator would take the volume.
    Expect (Flavour (7, 5, 0, true) == HfsFlavour68k,
            "7.5 is still below, fragments or not");
    Expect (Flavour (7, 5, 2, true) == HfsFlavourUniversal,
            "7.5.2 is the first version both cover");

    // The upper bound is where Basilisk's own patch list stops.
    Expect (Flavour (8, 1, 0, true) == HfsFlavourUniversal,
            "8.1 is the last both cover");
    Expect (Flavour (8, 5, 0, true) == HfsFlavourPowerPC,
            "8.5 is PowerPC, nothing to ask");
    Expect (Flavour (9, 0, 4, true) == HfsFlavourPowerPC,
            "so is 9.0.4");

    // Deliberately not "PowerPC": Mac OS stayed largely 68k code inside, so a
    // fragment says the PowerPC half is installed and never that it is alone.
    Expect (Flavour (8, 0, 0, false) == HfsFlavour68k,
            "inside the band, with no native fragment, it is 68k");

    // A System whose version cannot be read leaves the structure zeroed — System
    // 6 does today, because its System file is typed 'ZSYS' rather than 'zsys'.
    // Answering "68k" there would be right by accident; the rule has to say it
    // does not know.
    Expect (Flavour (0, 0, 0, false) == HfsFlavourUnreadable,
            "no readable version: the rule draws no conclusion");
}

/*
 *  Where SheepShaver's Macintosh lands in memory
 */

static void CheckLayout (void)
{
    printf ("\nSheepShaver's memory layout\n");

    TMacLayout L;
    Expect (MacLayoutPlan (256 * 1024 * 1024, &L), "256 MB fit");

    Expect (L.nRAMBase == 0x10000000, "the RAM starts where upstream puts it");
    Expect (L.nROMBase == L.nRAMBase + L.nRAMSize,
            "the ROM follows the RAM immediately");
    Expect ((L.nROMBase & 0xFFFFF) == 0, "and on a megabyte boundary");
    Expect (L.nSigStack == L.nROMBase + OKAPIA_ROM_AREA_SIZE,
            "the signal stack follows the ROM area, five megabytes and not four");
    Expect (L.nSheepBase == L.nSigStack + OKAPIA_SIG_STACK_SIZE,
            "then SheepShaver's own block");
    Expect (L.nFrameBase == L.nSheepBase + OKAPIA_SHEEP_SIZE,
            "and the screen, which must be at a Mac address like the rest");
    Expect (L.nHostBytes == (size_t) (L.nEnd - L.nRAMBase),
            "the host block covers exactly the guest, with no hole");

    // 256 MB + 5 (ROM) + 0.0625 (stack) + 0.5 (SheepMem) + 16 (screen).
    Expect (L.nHostBytes == 256u * 1024 * 1024 + OKAPIA_ROM_AREA_SIZE
                            + OKAPIA_SIG_STACK_SIZE + OKAPIA_SHEEP_SIZE + OKAPIA_FRAME_SIZE,
            "that is 277.6 MB for 256 MB of Mac RAM");
    // And the overhead mac_ram_circle.h claims for both engines must cover this
    // layout, or the second engine asks for a block again.
    Expect (L.nHostBytes - L.nRAMSize <= OKAPIA_MAC_BLOCK_OVERHEAD,
            "the shared overhead covers the hungriest layout");

    // An alignment that does nothing when there is nothing to do.
    TMacLayout M;
    Expect (MacLayoutPlan (255 * 1024 * 1024 + 1, &M)
            && M.nROMBase == 0x10000000 + 256u * 1024 * 1024,
            "an unaligned size pushes the ROM to the next megabyte");

    // The three refusals. They say no at startup, the only moment a wrong
    // memory layout can still be explained.
    Expect (!MacLayoutPlan (0, &M), "zero bytes of RAM are refused");
    Expect (!MacLayoutPlan (0xF0000000u, &M),
            "so is a size that would overflow the 32-bit space");

    // The Kernel Data is at 0x68ffe000 and the ROM places it, not us: the block
    // must end below it, not merely start below it.
    Expect (!MacLayoutPlan (0x58ffe000u, &M),
            "and a RAM that would reach the ROM's Kernel Data");
    // The largest RAM that still fits, computed rather than written down: it
    // moves whenever the screen or the ROM changes size.
    const uint32_t nTail = OKAPIA_ROM_AREA_SIZE + OKAPIA_SIG_STACK_SIZE
                         + OKAPIA_SHEEP_SIZE + OKAPIA_FRAME_SIZE;
    const uint32_t nLargest = (OKAPIA_KERNEL_DATA_BASE - 0x10000000u - nTail) & ~0xFFFFFu;
    Expect (MacLayoutPlan (nLargest, &M) && M.nEnd <= OKAPIA_KERNEL_DATA_BASE,
            "just below, it still fits");
    Expect (!MacLayoutPlan (nLargest + 0x100000u, &M),
            "and one megabyte more no longer does");
}

/*
 *  The resolutions offered
 *
 *  Both Macintosh get the same list; only the identifiers differ. A fixed size
 *  keeps its identifier whatever the display, because Mac OS 7.5 and later
 *  remember the Monitors control panel's choice by identifier.
 */

static void CheckSizes (void)
{
    printf ("\nthe resolutions offered to both Macintosh\n");

    u32 Ids[VIDEO_SCREEN_STANDARD];
    for (unsigned i = 0; i < VIDEO_SCREEN_STANDARD; i++)
    {
        Ids[i] = 0x100 + i;
    }
    const u32 Extra[VIDEO_SCREEN_EXTRA] = { 0x200, 0x201 };
    TVideoScreenSize L[VIDEO_SCREEN_SIZES_MAX];

    bool bOrdered = true;
    for (unsigned i = 1; i < VIDEO_SCREEN_STANDARD; i++)
    {
        const unsigned *a = VideoScreenStandard[i - 1], *b = VideoScreenStandard[i];
        bOrdered = bOrdered && (a[0] < b[0] || (a[0] == b[0] && a[1] < b[1]));
    }
    Expect (bOrdered, "the fixed list is ordered like the Monitors control panel, with no duplicate");

    unsigned n = VideoScreenSizeList (Ids, Extra, 2560, 1440, 1280, 720, L);
    bool bSame = n == VIDEO_SCREEN_STANDARD;
    for (unsigned i = 0; bSame && i < n; i++)
    {
        bSame = L[i].nWidth == VideoScreenStandard[i][0]
             && L[i].nHeight == VideoScreenStandard[i][1] && L[i].nId == Ids[i];
    }
    Expect (bSame, "a 1440p display and screen 1280/720: the fixed list alone, each size under its identifier");

    n = VideoScreenSizeList (Ids, Extra, 1366, 768, 1280, 720, L);
    Expect (n == VIDEO_SCREEN_STANDARD + 1 && L[n - 1].nWidth == 1366
            && L[n - 1].nHeight == 768 && L[n - 1].nId == 0x200,
            "the own size of a display not in the list comes after, under the first free identifier");

    n = VideoScreenSizeList (Ids, Extra, 1366, 768, 1000, 700, L);
    Expect (n == VIDEO_SCREEN_SIZES_MAX && L[n - 2].nId == 0x200
            && L[n - 1].nWidth == 1000 && L[n - 1].nId == 0x201,
            "then the one the preferences ask for, under the second");

    n = VideoScreenSizeList (Ids, Extra, 0, 0, 1000, 700, L);
    Expect (n == VIDEO_SCREEN_STANDARD + 1 && L[n - 1].nWidth == 1000 && L[n - 1].nId == 0x200,
            "with no usable display size, the preference takes the first");

    n = VideoScreenSizeList (Ids, Extra, 1366, 768, 1366, 768, L);
    Expect (n == VIDEO_SCREEN_STANDARD + 1, "a preference equal to the display is not offered twice");

    n = VideoScreenSizeList (Ids, Extra, 1920, 1080, 0, 0, L);
    Expect (n == VIDEO_SCREEN_STANDARD, "with no preference, nothing more");
}

/*
 *  The chime, read from the ROM
 *
 *  ROMs fabricated here, one per way of keeping the chime: a Power Macintosh's
 *  'beep' 0, a sampled 'snd ' laid down as data (Quadra), the ASC chip's table
 *  (Macintosh II, IIci). No Apple ROM in the repository; the ones on the machine
 *  are tried as well when they are there.
 */

static void Put32 (unsigned char *p, u32 v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void Put16 (unsigned char *p, u32 v)
{
    p[0] = v >> 8; p[1] = v;
}

static void PutCommand (unsigned char *p, unsigned nLength, unsigned nCommand, u32 nAddress)
{
    memset (p, 0, 16);
    p[0] = nLength; p[1] = nLength >> 8; p[2] = nCommand; p[3] = nCommand >> 8;
    p[4] = nAddress; p[5] = nAddress >> 8; p[6] = nAddress >> 16; p[7] = nAddress >> 24;
}

// The amplitude of one frequency in the left channel, by Goertzel's algorithm.
static double Tone (const s16 *pStereo, unsigned nFrom, unsigned nCount, double fHz)
{
    const double w = 2.0 * 3.14159265358979 * fHz / ROM_CHIME_OUTPUT_RATE;
    const double c = 2.0 * cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (unsigned i = nFrom; i < nFrom + nCount; i++)
    {
        const double s0 = pStereo[i * 2] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / nCount;
}

static double Rms (const s16 *pStereo, unsigned nFrom, unsigned nCount)
{
    double f = 0.0;
    for (unsigned i = nFrom; i < nFrom + nCount; i++)
    {
        f += (double) pStereo[i * 2] * pStereo[i * 2];
    }
    return sqrt (f / nCount);
}

static void CheckRomChime (void)
{
    printf ("\nthe chime read from the ROM\n");

    const u32 nSize = 0x100000;
    unsigned char *pImage = (unsigned char *) calloc (nSize, 1);
    TRomChime Chime;
    const unsigned MAX_FRAMES = 262144;
    s16 *pOut = (s16 *) malloc (MAX_FRAMES * 2 * sizeof (s16));

    // 1. The 'beep' 0: map at 0x800, 8-byte headers, 'snd ' 1 then 'beep' 0.
    Put32 (pImage + 0x1A, 0x800);
    pImage[0x805] = 8;
    Put32 (pImage + 0x800, 0x900);
    Put32 (pImage + 0x908, 0xA00);  Put32 (pImage + 0x90C, 0x5000);
    Put32 (pImage + 0x910, 0x736E6420); pImage[0x915] = 1;
    Put32 (pImage + 0xA08, 0);      Put32 (pImage + 0xA0C, 0x6000);
    Put32 (pImage + 0xA10, 0x62656570);

    const u32 nTop = 0xFFF00000, nSamples = 0x6040;
    Put32 (pImage + 0x6000, 0x10); Put32 (pImage + 0x6004, 2);
    Put32 (pImage + 0x6008, 0x1800); Put32 (pImage + 0x600C, nTop + nSamples);
    PutCommand (pImage + 0x6010, 0x1000, 0x0000, nTop + nSamples);
    PutCommand (pImage + 0x6020, 0x0800, 0x1000, nTop + nSamples + 0x1000);
    PutCommand (pImage + 0x6030, 0, 0x7000, 0);

    Expect (RomChimeFind (pImage, nSize, &Chime) == RomChimeFound
            && Chime.Kind == RomChimeBeep && Chime.nOffset == nSamples
            && Chime.nFrames == 0x1800 / 4,
            "the 'beep' 0 is found, its samples in the right place");
    Expect (RomChimeRender (pImage, nSize, &Chime, pOut, MAX_FRAMES) == 0x1800 / 4 * 2,
            "and rendered at 44.1 kHz, twice the frames of 22.05");

    PutCommand (pImage + 0x6020, 0x0800, 0x1000, nTop + nSamples + 0x1004);
    Expect (RomChimeFind (pImage, nSize, &Chime) == RomChimeMalformed,
            "a command that does not follow the previous one is refused");
    PutCommand (pImage + 0x6020, 0x0800, 0x1000, nTop + nSamples + 0x1000);

    Put32 (pImage + 0x6008, 0x1804);
    Expect (RomChimeFind (pImage, nSize, &Chime) == RomChimeMalformed,
            "so is a byte count the commands do not cover");

    Put32 (pImage + 0x908, 0x800);  // a map that loops
    Expect (RomChimeFind (pImage, nSize, &Chime) != RomChimeFound,
            "a map that loops ends");
    memset (pImage, 0, nSize);

    // 2. A sampled 'snd ' laid down as data: 1 s of A at 440 Hz, 22,254.5 Hz.
    const u32 nSnd = 0x20000, nFrames8 = 22254;
    Put16 (pImage + nSnd, 1); Put16 (pImage + nSnd + 2, 1); Put16 (pImage + nSnd + 4, 5);
    Put32 (pImage + nSnd + 6, 0xA0); Put16 (pImage + nSnd + 10, 1);
    Put16 (pImage + nSnd + 12, 0x8051); Put32 (pImage + nSnd + 16, 20);
    Put32 (pImage + nSnd + 24, nFrames8); Put32 (pImage + nSnd + 28, 0x56EE8BA3);
    pImage[nSnd + 41] = 60;
    for (u32 i = 0; i < nFrames8; i++)
    {
        pImage[nSnd + 42 + i] = (u8) (128 + 100 * sin (2 * 3.14159265358979 * 440 * i / 22254.5454));
    }
    Expect (RomChimeFind (pImage, nSize, &Chime) == RomChimeFound
            && Chime.Kind == RomChimeSampled && Chime.nOffset == nSnd + 42
            && Chime.nFrames == nFrames8 && Chime.nRate == 0x56EE8BA3,
            "a 'snd ' outside the resource map is found by its header");
    unsigned n = RomChimeRender (pImage, nSize, &Chime, pOut, MAX_FRAMES);
    Expect (n > 44090 && n < 44110, "and rendered at 44.1 kHz, still one second");
    Expect (Tone (pOut, 4410, 8820, 440) > 10 * Tone (pOut, 4410, 8820, 523),
            "at the same pitch: 440 Hz, not another");

    Put32 (pImage + nSnd + 24, 2720);  // the system beep, a tenth of a second
    Expect (RomChimeFind (pImage, nSize, &Chime) == RomChimeNone,
            "a sound too short for a chime is not one");
    memset (pImage, 0, nSize);

    // 3. The ASC table: two voices at 0x20000 (347.8 Hz), the second 300 steps
    //    later, 30,000 steps.
    const u32 nTable = 0x7158;
    Put16 (pImage + nTable, 0x0204); Put32 (pImage + nTable + 2, 13);
    Put32 (pImage + nTable + 6, 300); Put32 (pImage + nTable + 10, 30000);
    Put16 (pImage + nTable + 14, 2);
    Put32 (pImage + nTable + 16, 0x20000); Put32 (pImage + nTable + 20, 0x20000);
    Expect (RomChimeFind (pImage, nSize, &Chime) == RomChimeFound
            && Chime.Kind == RomChimeSynthesised && Chime.nOffset == nTable,
            "the ASC chip's table is found");
    n = RomChimeRender (pImage, nSize, &Chime, pOut, MAX_FRAMES);
    Expect (n > 30300 && n < 30420, "30,000 steps of 22.95 us: 0.69 s");
    // The stairs put the energy on the second harmonic: 695.6 Hz.
    Expect (Tone (pOut, 2205, 4410, 695.6) > 5 * Tone (pOut, 2205, 4410, 600),
            "the 0x20000 increment sounds at 347.8 Hz, mostly through its second harmonic");
    Expect (Rms (pOut, n - 3000, 3000) < 0.3 * Rms (pOut, 2205, 4410),
            "and the step-by-step smoothing makes it die away");

    pImage[nTable + 15] = 5;
    Expect (RomChimeFind (pImage, nSize, &Chime) != RomChimeFound,
            "more than four voices is not an ASC table");
    free (pImage);

    // The machine's own, if they are there: never in the repository.
    static const struct { const char *pPath; u32 nBytes; TRomChimeKind Kind; unsigned nMin, nMax; } ROMS[] =
    {
        { "../../qemu/sd-contents/powermac9600v1.rom", 0x400000, RomChimeBeep,        103000, 104000 },
        { "../../qemu/sd-contents/okapia.rom",         0x100000, RomChimeSampled,      61500,  62000 },
        { "../../qemu/sd-contents/macIIci.rom",        0x080000, RomChimeSynthesised,  30300,  30420 },
    };
    for (unsigned r = 0; r < sizeof ROMS / sizeof ROMS[0]; r++)
    {
        FILE *pFile = fopen (ROMS[r].pPath, "rb");
        if (pFile == 0)
        {
            continue;
        }
        u8 *pRom = (u8 *) malloc (ROMS[r].nBytes);
        const bool bRead = fread (pRom, 1, ROMS[r].nBytes, pFile) == ROMS[r].nBytes;
        fclose (pFile);
        n = 0;
        const bool bFound = bRead && RomChimeFind (pRom, ROMS[r].nBytes, &Chime) == RomChimeFound
                            && Chime.Kind == ROMS[r].Kind;
        if (bFound)
        {
            n = RomChimeRender (pRom, ROMS[r].nBytes, &Chime, pOut, MAX_FRAMES);
        }
        char Label[128];
        snprintf (Label, sizeof Label, "%s on this machine: chime found, %u frames", ROMS[r].pPath + 22, n);
        Expect (bFound && n >= ROMS[r].nMin && n <= ROMS[r].nMax, Label);
        free (pRom);
    }
    free (pOut);
}

int main (void)
{
    CheckSizes ();
    CheckRomChime ();
    CheckUnknownLines ();
    CheckFlavour ();
    CheckLayout ();
    unlink (TMP);

    /*
     *  Announced tiles
     *
     *  video_set_dirty_area() is the one thing the PowerPC engine has and the
     *  68k does not: the Macintosh says what it changed. The tile arithmetic is
     *  the part that can go wrong silently — a forgotten tile leaves a piece of
     *  stale screen, which looks like tearing and not like a wrong sum.
     */
    /*
     *  Direct modes
     *
     *  The Mac is big-endian and its 16-bit pixel is 0RRRRRGG GGGBBBBB, which
     *  video_blit.cpp's direct blitters do not assume. Handing them a direct
     *  mode gave a screen entirely one colour at 16 bits and vertical bands at
     *  32 — a fault seen at once on screen and never at link time. Here it would
     *  be seen first.
     */
    printf ("\n  The Macintosh's direct modes\n");

    unsigned char Src[8];
    unsigned Out[4];

    // 16 bits, big-endian: pure red, pure green, pure blue.
    Src[0] = 0x7C; Src[1] = 0x00;   // 0x7C00
    Src[2] = 0x03; Src[3] = 0xE0;   // 0x03E0
    Src[4] = 0x00; Src[5] = 0x1F;   // 0x001F
    CompositorConvert16To32 ((u8 *) Out, Src, 6);
    Expect (Out[0] == 0xFF0000FFu, "16 bits: the Mac's red comes out in the low byte");
    Expect (Out[1] == 0xFF00FF00u, "16 bits: green in the middle");
    Expect (Out[2] == 0xFFFF0000u, "16 bits: blue at the top");

    // 32 bits: the Mac writes xRGB, the leading byte is ignored.
    Src[0] = 0x00; Src[1] = 0x12; Src[2] = 0x34; Src[3] = 0x56;
    CompositorConvert32To32 ((u8 *) Out, Src, 4);
    Expect (Out[0] == 0xFF563412u, "32 bits: xRGB becomes BGRA, the leading byte dropped");

    printf ("\n  Regions announced by the Macintosh\n");

    TCompositor C;
    memset (&C, 0, sizeof C);
    C.nWidth  = 640;
    C.nHeight = 480;

    CompositorAnnounce (&C, 0, 0, 1, 1);
    Expect (C.Announced[0] == 1, "one pixel top left marks the first tile");
    unsigned nSet = 0;
    for (unsigned i = 0; i < 16; i++) nSet += (unsigned) __builtin_popcount (C.Announced[i]);
    Expect (nSet == 1, "and only it");

    memset (C.Announced, 0, sizeof C.Announced);
    CompositorAnnounce (&C, 0, 0, 640, 480);
    nSet = 0;
    for (unsigned i = 0; i < 16; i++) nSet += (unsigned) __builtin_popcount (C.Announced[i]);
    Expect (nSet == 256, "the whole screen marks all 256 tiles");

    // 640/16 = 40 pixels per tile, 480/16 = 30. A rectangle straddling the
    // boundary must mark both, not one.
    memset (C.Announced, 0, sizeof C.Announced);
    CompositorAnnounce (&C, 39, 0, 2, 1);
    Expect (C.Announced[0] == 0x3, "a straddling rectangle marks both tiles");

    memset (C.Announced, 0, sizeof C.Announced);
    CompositorAnnounce (&C, 600, 450, 200, 200);
    Expect (C.Announced[15] == 0x8000, "what overflows is clipped, not wrapped");

    memset (C.Announced, 0, sizeof C.Announced);
    CompositorAnnounce (&C, 700, 0, 10, 10);
    nSet = 0;
    for (unsigned i = 0; i < 16; i++) nSet += (unsigned) __builtin_popcount (C.Announced[i]);
    Expect (nSet == 0, "entirely off screen marks nothing");

    CompositorAnnounce (&C, 0, 0, -5, 10);
    CompositorAnnounce (&C, 0, 0, 10, 0);
    nSet = 0;
    for (unsigned i = 0; i < 16; i++) nSet += (unsigned) __builtin_popcount (C.Announced[i]);
    Expect (nSet == 0, "nor does a zero or negative width or height");

    /*
     *  What the compositor writes to the screen
     *
     *  The Pi's frame buffer is Device memory: an unaligned access there is a
     *  fault, and a Macintosh at 912x492 killed the board because its 57-pixel
     *  tiles put the vectorised copy out of alignment. The fault does not show
     *  here — the host accepts anything — but the path rewritten to avoid it
     *  must draw exactly the same thing at odd widths, odd origins and with a
     *  pitch that is not a multiple of 16. That is what is checked, pixel by
     *  pixel.
     */
    printf ("\n  What the compositor writes to the screen\n");
    {
        struct { unsigned w, h, ow, oh, scale; const char *pWhat; } Cases[] = {
            {  912, 492, 1824,  984, 2, "912x492 doubled into 1824x984, the mode that killed the board" },
            {  853, 480, 2560, 1440, 3, "853x480 tripled into 2560x1440" },
            { 1280, 720, 1824,  984, 1, "1280x720 at scale 1, centred" },
            {  640, 480, 1366,  768, 1, "640x480 in 1366x768: odd origin, pitch not a multiple of 16" },
        };
        for (unsigned c = 0; c < sizeof Cases / sizeof Cases[0]; c++)
        {
            const unsigned w = Cases[c].w, h = Cases[c].h;
            const unsigned ow = Cases[c].ow, oh = Cases[c].oh;
            u8 *pMac    = (u8 *) calloc ((size_t) w * h, 4);
            u8 *pShadow = (u8 *) calloc ((size_t) w * h, 4);
            u8 *pOut    = (u8 *) aligned_alloc (16, (size_t) ow * oh * 4);
            memset (pOut, 0xEE, (size_t) ow * oh * 4);
            for (unsigned y = 0; y < h; y++)
            {
                for (unsigned x = 0; x < w; x++)
                {
                    u8 *p = pMac + (y * w + x) * 4;
                    p[0] = 0; p[1] = (u8) x; p[2] = (u8) y; p[3] = (u8) ((x >> 8) + (y >> 8) * 16);
                }
            }

            TCompositor K;
            memset (&K, 0, sizeof K);
            K.pSource = pMac; K.nWidth = w; K.nHeight = h;
            K.nBytesPerRow = w * 4; K.nSourceBits = 32;
            K.pOutput = pOut; K.nOutputWidth = ow; K.nOutputHeight = oh;
            K.nOutputPitch = ow * 4; K.nOutputBits = 32;
            K.pConvertRow = CompositorConvert32To32Bgr;
            K.pShadow = pShadow; K.nShadowBytes = w * h * 4;

            bool bOK = CompositorPlan (&K) && K.nScale == Cases[c].scale;
            if (bOK)
            {
                CompositorRun (&K);
            }
            for (unsigned y = 0; bOK && y < oh; y++)
            {
                for (unsigned x = 0; bOK && x < ow; x++)
                {
                    unsigned v;
                    memcpy (&v, pOut + (y * ow + x) * 4, 4);
                    const bool bInside = x >= K.nOriginX && x < K.nOriginX + w * K.nScale
                                      && y >= K.nOriginY && y < K.nOriginY + h * K.nScale;
                    unsigned nWant = 0;
                    if (bInside)
                    {
                        const u8 *p = pMac + (((y - K.nOriginY) / K.nScale) * w
                                              + (x - K.nOriginX) / K.nScale) * 4;
                        nWant = 0xFF000000u | (unsigned) p[1] << 16 | (unsigned) p[2] << 8 | p[3];
                    }
                    bOK = v == nWant;
                }
            }
            Expect (bOK, Cases[c].pWhat);
            free (pMac); free (pShadow); free (pOut);
        }
    }

    // A volume made from end to end, here, with no card: hfs_format() writes
    // over a file that already has its size, so the test makes it the way FatFs
    // will and then checks what the Macintosh would read. A format that writes
    // something the next mount does not read back is a volume found broken
    // later, with files on it.
    printf ("\nmaking a volume\n");
    {
        const char *pPath = "/tmp/okapia-check-newvolume.image";
        const unsigned long nBytes = 20UL * 1024 * 1024;

        unlink (pPath);
        int nFD = open (pPath, O_RDWR | O_CREAT | O_TRUNC, 0600);
        Expect (nFD >= 0 && ftruncate (nFD, (off_t) nBytes) == 0,
                "a 20 MB file, as FatFs will make it on the card");
        if (nFD >= 0)
        {
            close (nFD);
        }

        Expect (HfsFormat (pPath, "Macintosh HD"), "hfs_format() accepts it");

        THfsVolumeInfo Info;
        Expect (HfsDescribe (pPath, &Info), "and it reads back as a volume");
        Expect (strcmp (Info.Name, "Macintosh HD") == 0, "under the name asked for");
        Expect (Info.Blessed == 0, "empty, so with no System Folder: it does not start");
        Expect (Info.bClean, "and clean, since nobody has mounted it yet");
        // What the Mac will really see: standard HFS reserves its tables, so the
        // free space is a little under the size asked for, never above it.
        Expect (Info.TotalKB <= nBytes / 1024 && Info.TotalKB > nBytes / 1024 - 512,
                "the size of the file, give or take the tables");
        Expect (Info.FreeKB <= Info.TotalKB, "and it invents no space");

        // The names libhfs refuses. The Create button refuses them first, but
        // this is where the rule is written, and a rule in two places is a rule
        // that will drift.
        Expect (!HfsFormat (pPath, ""), "an empty name is refused");
        Expect (!HfsFormat (pPath, "Disk:2"), "so is a name with a colon");
        Expect (!HfsFormat (pPath, "a name longer than twenty-seven characters"),
                "and a name too long");
        // Refused, and the volume from before is still there.
        Expect (HfsDescribe (pPath, &Info) && strcmp (Info.Name, "Macintosh HD") == 0,
                "and a refusal does not destroy the volume that was there");
        unlink (pPath);
    }

    printf ("\na volume's date, as the clock's floor\n");
    {
        // 2026-09-15 00:00, the build time in the local frame.
        const long nBuild = 1789430400L;
        // drLsMod at $FFFFFFFF, as libhfs hands it back (offset by 2082844800).
        const long nSaturated = (long) (0xFFFFFFFFul - 2082844800ul);
        Expect (nSaturated == HFS_SATURATED_DATE, "the saturated value is indeed 2040-02-06 06:28:15");
        Expect (!HfsDateIsPlausible (nSaturated, nBuild),
                "a saturated drLsMod is refused (the clock stayed stuck in 2040)");
        Expect (!HfsDateIsPlausible (nSaturated - 1, nBuild),
                "so is one second before: ten years after the build is not use");
        Expect (HfsDateIsPlausible (nBuild - 86400, nBuild), "yesterday is plausible");
        Expect (HfsDateIsPlausible (nBuild + 5L * 365 * 86400, nBuild),
                "so is five years after the build: a kernel ages");
        Expect (!HfsDateIsPlausible (nBuild + HFS_PLAUSIBLE_AHEAD + 1, nBuild),
                "past the bound, refused");
    }

    printf ("\n%u failure(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
