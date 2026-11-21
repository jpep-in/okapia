// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
// halt() and reboot(), as a process does them: the end of the program, and the
// program started again on the same card folder (main_desktop.cpp). The
// PowerPC engine cannot be entered twice in place, so on the board its Restart
// resets the board; here it starts the process again, which is the same thing.
#ifndef _circle_startup_h
#define _circle_startup_h
#define EXIT_HALT   0
#define EXIT_REBOOT 1
[[noreturn]] void halt (void);
[[noreturn]] void reboot (void);
#endif
