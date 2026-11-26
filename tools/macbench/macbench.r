/* macbench.r - MacBench's resources: its memory partition.
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: MIT
 */
#include "Processes.r"
#include "Menus.r"
#include "Windows.r"
#include "MacTypes.r"

resource 'SIZE' (-1) {
	reserved, acceptSuspendResumeEvents, reserved, canBackground,
	multiFinderAware, backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents,
	is32BitCompatible, isHighLevelEventAware, onlyLocalHLEvents, notStationeryAware,
	reserved, reserved, reserved, reserved,
	3072 * 1024, 3072 * 1024
};
