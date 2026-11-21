//
// okapia_desktop.h — between the desktop's entry point and its host.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_desktop_h
#define _okapia_desktop_h

#include <stdarg.h>
#include <time.h>

#include "okapia_gfx.h"

// The card folder, without a trailing slash.
void        DesktopSetRoot (const char *pRoot);
const char *DesktopRoot (void);

void DesktopLogOpen (bool bQuiet);
void DesktopLogClose (void);
void OkapiaHostLogV (const char *pFrom, int nSeverity, const char *pFormat, va_list Args);

bool DesktopWindowOpen (unsigned nWidth, unsigned nHeight, bool bHeadless);
void DesktopWindowClose (void);
const TSurface *DesktopSurface (void);
bool DesktopSavePNG (const char *pPath);

// The main thread's loop while the emulation thread runs, until *pbDone.
void DesktopMainLoop (volatile bool *pbDone);
void DesktopPump (void);
// Who has the keyboard and mouse: the Macintosh (true) or the menu.
void DesktopMacInput (bool bOn);
bool DesktopMacHasInput (void);
// What the Macintosh is sent, as the Pi's USB devices send it (board_desktop.cpp).
void DesktopMacKeys (unsigned char ucModifiers, const unsigned char Keys[6]);
void DesktopMacMouse (unsigned nButtons, int nDX, int nDY);
// A script's keys, for whichever has the keyboard.
void DesktopTypeChar (unsigned char c);
void DesktopPressUsage (unsigned char ucUsage, unsigned nModifiers, unsigned nChar);
// The window closed while the Macintosh runs (main_desktop.cpp).
void DesktopCloseWhileRunning (void);
void DesktopPostKey (unsigned char ucUsage, unsigned nModifiers, unsigned nChar);
void DesktopPostText (unsigned nChar, unsigned nModifiers);
// nButton: 0 a move, 1 a press, -1 a release.
void DesktopPostMouse (int nX, int nY, int nButton);
void DesktopHoldModifiers (unsigned nModifiers);
void DesktopRequestQuit (void);

// The test mode (main_desktop.cpp): what is due now, and the clock it fixes.
void   DesktopScriptStep (void);
time_t DesktopScriptTime (time_t nNow);

#endif
