/*
 * user_strings_circle.cpp — the strings Okapia shows to the Mac.
 *
 * Basilisk names the shared volume through a platform string, not a preference:
 * every port overrides STR_EXTFS_VOLUME_NAME in its own table ("Amiga", "BeOS",
 * upstream's default "Host"). That is fine for a program built per platform and
 * wrong for an appliance whose configuration lives on the card, so this table
 * answers from the preferences first and falls back to a compiled default.
 *
 * Replaces dummy/user_strings_dummy.cpp, whose platform table is empty.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"

#include <string.h>

#include "prefs.h"
#include "user_strings.h"

// Shown by Get Info as the volume's format, next to "Mac OS Standard". It
// describes the file system, not one volume, so it is not configurable.
static const char EXTFS_NAME[] = "Okapia Shared Folder";

// The name on the desktop when the preferences say nothing.
static const char EXTFS_VOLUME_NAME[] = "Okapia";

user_string_def platform_strings[] = {
	{STR_EXTFS_NAME, EXTFS_NAME},
	{STR_EXTFS_VOLUME_NAME, EXTFS_VOLUME_NAME},
	{-1, NULL}	// End marker
};

/*
 *  Fetch pointer to string, given the string number
 */

const char *GetString(int num)
{
	// The one string the card gets to decide. ExtFSInit copies it into a
	// 28-byte vcbVN, which is a Pascal string: 27 characters at most, and a
	// longer one would be truncated inside the volume record rather than here.
	if (num == STR_EXTFS_VOLUME_NAME) {
		static char name[28];
		const char *pref = PrefsFindString("extfsname");
		if (pref != NULL && *pref != '\0') {
			strncpy(name, pref, sizeof(name) - 1);
			name[sizeof(name) - 1] = 0;
			return name;
		}
	}

	// First search for platform-specific string
	int i = 0;
	while (platform_strings[i].num >= 0) {
		if (platform_strings[i].num == num)
			return platform_strings[i].str;
		i++;
	}

	// Not found, search for common string
	i = 0;
	while (common_strings[i].num >= 0) {
		if (common_strings[i].num == num)
			return common_strings[i].str;
		i++;
	}
	return NULL;
}
