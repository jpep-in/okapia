/* fat.r - the resources that make one application of the two builds: the
 * PowerPC code fragment in the data fork, the 68k CODE copied beside it
 * (scripts/build-okapiabench.sh). A PowerPC Macintosh runs the fragment, a
 * 68k one the CODE.
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: MIT
 */
#include "CodeFragments.r"

resource 'cfrg' (0) {
	{
		kPowerPCCFragArch, kIsCompleteCFrag, kNoVersionNum, kNoVersionNum,
		kDefaultStackSize, kNoAppSubFolder,
		kApplicationCFrag, kDataForkCFragLocator, kZeroOffset, kCFragGoesToEOF,
		"OkapiaBench"
	}
};

#include "okapiabench.r"
