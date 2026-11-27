/* okapiabench.r - OkapiaBench's resources: its memory partition and the
 * resources tb_resources reads.
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: MIT
 */
#include "Processes.r"
#include "Menus.r"
#include "Windows.r"
#include "MacTypes.r"

/* 8 MB preferred (cpu_chase_4m and cpu_stream need 4 MB), 3 MB at least:
   a test that finds no room says "nomem" and the others run. */
resource 'SIZE' (-1) {
	reserved, acceptSuspendResumeEvents, reserved, canBackground,
	multiFinderAware, backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents,
	is32BitCompatible, isHighLevelEventAware, onlyLocalHLEvents, notStationeryAware,
	reserved, reserved, reserved, reserved,
	8192 * 1024, 3072 * 1024
};

data 'OKBD' (128) { $"4F6B 6170 6961 4265 6E63 6820 7265 736F 7572 6365 2030 2031 3238" };
data 'OKBD' (129) { $"0102 0304 0506 0708 090A 0B0C 0D0E 0F10 1112 1314 1516 1718 191A" };
data 'OKBD' (130) { $"FFEE DDCC BBAA 9988 7766 5544 3322 1100 0011 2233 4455 6677 8899" };
data 'OKBD' (131) { $"5468 6520 7175 6963 6B20 6272 6F77 6E20 666F 7820 6A75 6D70 7320" };
data 'OKBD' (132) { $"0000 0001 0000 0002 0000 0003 0000 0004 0000 0005 0000 0006 0007" };
data 'OKBD' (133) { $"A5A5 5A5A A5A5 5A5A A5A5 5A5A A5A5 5A5A A5A5 5A5A A5A5 5A5A A5A5" };
data 'OKBD' (134) { $"1357 9BDF 2468 ACE0 1357 9BDF 2468 ACE0 1357 9BDF 2468 ACE0 1357" };
data 'OKBD' (135) { $"4D61 6369 6E74 6F73 6820 5175 6164 7261 2036 3530 2033 3320 4D48" };
