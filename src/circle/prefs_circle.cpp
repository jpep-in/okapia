/*
 * prefs_circle.cpp — preferences, read from the SD card.
 *
 * The format, the keywords and the parser are upstream's: LoadPrefsFromStream()
 * and SavePrefsToStream() do all the work, so a BasiliskII_Prefs file written
 * by Basilisk II on a desktop is readable here, and the file this writes is
 * readable there. Three things are ours and nothing else: where the file lives
 * (the root of the card), what the defaults are, and a header comment written
 * into a file we create.
 *
 * This replaces dummy/prefs_dummy.cpp, which looked for the file relative to a
 * current directory the kernel never sets.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"

#include <stdio.h>
#include <string>

#include "okapia_circle.h"

#include "prefs.h"

#define FROM "okapia-prefs"

// FatFs mounts the card as the root of the filesystem, so this is the card's
// top-level directory next to the ROM and the disk images.
static const char PREFS_FILE_NAME[] = "/BasiliskII_Prefs";

// Only reached when the card carries no preferences file. They name the files
// scripts/make-sd-image.sh puts on the card.
static const char DEFAULT_ROM[]  = "/okapia.rom";
static const char DEFAULT_DISK[] = "/machd76.image";

// Basilisk II declares this for the platforms that accept a --config option.
// There is no command line here, but code shared with those platforms links
// against it.
std::string UserPrefsPath;

/*
 *  Preferences items that exist on this platform only.
 *
 *  Being listed here is what makes a keyword accepted by the parser and written
 *  back by SavePrefsToStream; an item set with PrefsReplace* but never declared
 *  works at runtime and then silently disappears from the file.
 */

prefs_desc platform_prefs_items[] = {
	{"nonet", TYPE_BOOLEAN, false, "disable Ethernet (no networking yet: always true)"},
	{"timezone", TYPE_INT32, false, "minutes east of UTC; a Mac's clock is local time"},
	{"extfsname", TYPE_STRING, false, "name of the shared folder's volume on the Mac desktop"},
	{"hfsrepair", TYPE_BOOLEAN, false, "scavenge a volume left in use by an interrupted session"},
	{"hfsinventory", TYPE_BOOLEAN, false, "list the HFS volumes found on the card at startup"},
	{"language", TYPE_STRING, false, "the boot firmware's language: a two-letter code, en or fr"},
	// Four states and not a boolean: audio_circle.cpp used to hard-code the
	// jack, which a Pi 5 does not have and which is the wrong socket on a
	// television. "off" is the value an unreadable one falls back to, because a
	// device claimed and not working is what froze the guest once already.
	{"soundoutput", TYPE_STRING, false, "where sound comes out: off, hdmi, jack or usb"},
	// Declared, not endorsed: rsrc_patches.cpp acts on it, but the patched idle
	// loop has never been measured here. Declaring it keeps the parser from
	// rejecting a prefs file brought over from a desktop Basilisk II.
	{"modelidauto", TYPE_BOOLEAN, false, "set modelid from the System installed on the boot volume"},
	{"idlewait", TYPE_BOOLEAN, false, "let the Mac's idle loop sleep (untested here)"},
	{NULL, TYPE_END, false, NULL}	// End of list
};

/*
 *  Defaults, applied before the card is read so that the card overrides them.
 *
 *  Scalars only. An item declared "multiple" — disk, floppy, cdrom — is *added*
 *  to by the parser rather than replaced, so a default set here would survive
 *  alongside the card's own entry and the Mac would see both. Those are handled
 *  after loading instead, in LoadPrefs().
 */

void AddPlatformPrefsDefaults(void)
{
	PrefsReplaceString("rom", DEFAULT_ROM);

	// 256 MB is the ceiling this project targets; see planification.md §2.
	PrefsReplaceInt32("ramsize", 256 * 1024 * 1024);

	// Upstream defaults frameskip to 6, i.e. a 10 Hz screen. The Mac draws its
	// own cursor into its own framebuffer, so a decimated screen reads as a
	// laggy mouse. 0 is Dynamic, which holds the compositor to a fraction of
	// wall time instead of to a fixed rate.
	PrefsReplaceInt32("frameskip", 0);

	// 5 = Mac IIci, which System 7.x before 7.5 requires: an enabler checks the
	// machine it runs on, and announcing a Quadra 900 (14) stops System 7.1 on
	// an empty Welcome box. Mac OS 8.x wants 14. This follows the System
	// actually installed, which is why it belongs in a file on the card.
	PrefsReplaceInt32("modelid", 5);

	// Which of the two is right depends on the System, not on the ROM, so the
	// boot volume is asked before the emulator starts and "modelid" above is
	// only the answer for a volume that will not say. Set modelidauto false to
	// force the value in this file.
	PrefsReplaceBool("modelidauto", true);

	PrefsReplaceInt32("cpu", 4);		// 68040
	PrefsReplaceBool("fpu", true);		// a 68040 always has one

#ifdef CIRCLE_QEMU
	// QEMU's raspi3b models no sound output at all. Claiming a sound device the
	// Mac cannot actually hear is worse than silence: it froze the guest once,
	// and the hard stop that followed cost a card.
  #ifdef OKAPIA_FORCE_SOUND
	PrefsReplaceBool("nosound", false);
  #else
	PrefsReplaceBool("nosound", true);
  #endif
#else
	PrefsReplaceBool("nosound", false);
#endif

	PrefsReplaceBool("nonet", true);	// networking comes later

	// The shared folder, as a path on the card. Upstream's Unix default is "/",
	// which here would hand the Finder the ROM and every disk image — including
	// the one it has booted from. A directory of its own is the only sane
	// default; the kernel creates it if the card has none.
	PrefsReplaceString("extfs", "/shared");

	// Upstream calls it "Host" and every port renames it in its own string
	// table — there is no preference for it, which suits a program built per
	// platform and not an appliance configured from its card. 27 characters at
	// most: it ends up in a Pascal string inside the volume record.
	PrefsReplaceString("extfsname", "Okapia");

	// A Mac of this era has no time zone: its clock is local time. Okapia's own
	// clock starts from the build time, which is a UTC instant, so without this
	// the Mac runs an hour or two behind and it looks like a stale build.
	PrefsReplaceInt32("timezone", 0);

	// Upstream's keyword for a key-mapping file. The built-in table is generated
	// from Basilisk's own keycodes file at build time and is normally right, so
	// this only matters for a keyboard that reports something unusual — and then
	// it beats a rebuild. Absent from the card means "keep the built-in table".
	PrefsReplaceString("keycodefile", "/BasiliskII.keycodes");

	// A Mac refuses to start from a volume whose MDB still says "in use", which
	// is exactly what an interrupted session leaves behind — on real hardware
	// too. Repairing it is what a second bootable System would do; doing it
	// ourselves is why a hard stop costs the boot and not the data. It writes
	// to the volume, so it is a preference and not a hard-coded policy.
	PrefsReplaceBool("hfsrepair", true);
	PrefsReplaceBool("hfsinventory", true);

	PrefsReplaceBool("idlewait", false);
}

/*
 *  Load preferences from the card
 */

void LoadPrefs(const char *vmdir)
{
	(void) vmdir;			// no VM directory: the card is the VM

	bool bLoaded = false;
	FILE *f = fopen(PREFS_FILE_NAME, "r");
	if (f != NULL) {
		LoadPrefsFromStream(f);
		fclose(f);
		bLoaded = true;
		CLogger::Get()->Write(FROM, LogNotice, "Read %s", PREFS_FILE_NAME);
	} else
		CLogger::Get()->Write(FROM, LogNotice,
				      "No %s on the card, writing one with the defaults",
				      PREFS_FILE_NAME);

	// Multi-valued items, which could not be defaulted before the read.
	if (PrefsFindString("disk") == NULL)
		PrefsAddString("disk", DEFAULT_DISK);

	// No floppy drives. Without an explicit entry, SonyInit() calls
	// SysAddFloppyPrefs(), whose fallback branch adds the Linux /dev/fd0 and
	// /dev/fd1 — phantom drives the Mac then tries, and fails, to mount.
	if (PrefsFindString("floppy") == NULL)
		PrefsAddString("floppy", "");

	if (!bLoaded)
		SavePrefs();
}

/*
 *  Save preferences to the card
 */

void SavePrefs(void)
{
	FILE *f = fopen(PREFS_FILE_NAME, "w");
	if (f == NULL) {
		CLogger::Get()->Write(FROM, LogWarning, "Cannot write %s", PREFS_FILE_NAME);
		return;
	}

	// SavePrefsToStream() writes every declared keyword, including the ones no
	// driver on this platform reads, and it writes no comments. A file the user
	// is meant to edit deserves both a warning about that and enough examples
	// to change something without opening the README.
	//
	// Only a # or a ; in the *first* column starts a comment (prefs.cpp:400);
	// there is no trailing-comment syntax, so every example below is a whole
	// line of its own.
	fprintf(f,
		"# Okapia preferences. Basilisk II format: one \"keyword value\" per line.\n"
		"# A line whose first character is # or ; is ignored. There are no\n"
		"# trailing comments: everything after the keyword is the value, so an\n"
		"# explanation must go on a line of its own — as below.\n"
		"#\n"
		"# Quick start: copy a line, remove the leading \"# \", edit the value.\n"
		"#\n"
		"# The boot volume, at the root of the card. Repeat the keyword for a\n"
		"# second volume; the Mac starts from the first one that is bootable.\n"
		"# disk /machd76.image\n"
		"# disk /scratch.image\n"
		"#\n"
		"# The Macintosh ROM image.\n"
		"# rom /okapia.rom\n"
		"#\n"
		"# Mac RAM in bytes. 268435456 is 256 MB, 134217728 is 128 MB.\n"
		"# ramsize 268435456\n"
		"#\n"
		"# 5 = Mac IIci, for System 7.0 to 7.1. 14 = Quadra 900, for Mac OS 8.x.\n"
		"# Read from the System on the boot volume unless modelidauto is false,\n"
		"# in which case the value below is used as it stands.\n"
		"# modelid 5\n"
		"# modelidauto true\n"
		"#\n"
		"# 0 = Dynamic. 1 = every frame, 2 = 30 Hz, 6 = 10 Hz.\n"
		"# frameskip 0\n"
		"#\n"
		"# Minutes east of UTC. 60 for CET, 120 for CEST, -300 for EST. A Mac has\n"
		"# no time zone: its clock is local time, and Okapia's starts at UTC.\n"
		"# timezone 120\n"
		"#\n"
		"# Leave the sound hardware alone.\n"
		"# nosound true\n"
		"#\n"
		"# The shared folder, as a directory on the card. It appears on the Mac's\n"
		"# desktop as a volume. System 7.5 and later need nothing extra; 7.0 and 7.1\n"
		"# need the File System Manager 1.2 extension. Leave it empty for\n"
		"# no shared folder at all.\n"
		"# extfs /shared\n"
		"#\n"
		"# What that volume is called on the Mac's desktop. 27 characters at most.\n"
		"# extfsname Okapia\n"
		"#\n"
		"# Repair a volume an interrupted session left marked in use, and list\n"
		"# the volumes found on the card at startup.\n"
		"# hfsrepair true\n"
		"# hfsinventory true\n"
		"#\n"
		"# Keywords below that Circle has nothing to do (scsi*, jit*, ether*,\n"
		"# seriala, serialb, mousewheel*, and the rest) are listed by upstream and\n"
		"# written back unchanged. README.md says which ones and why.\n"
		"\n");
	SavePrefsToStream(f);
	fclose(f);
}
