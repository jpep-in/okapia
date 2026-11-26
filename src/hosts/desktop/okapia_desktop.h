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
#include "okapia_host.h"

// The card folder, without a trailing slash.
void        DesktopSetRoot (const char *pRoot);
const char *DesktopRoot (void);

void DesktopLogOpen (bool bQuiet);
void DesktopLogClose (void);
void DesktopLogFollowRoot (void);
void OkapiaHostLogV (const char *pFrom, int nSeverity, const char *pFormat, va_list Args);

bool DesktopWindowOpen (unsigned nWidth, unsigned nHeight, bool bHeadless, THostWindowMode Mode);
// The window's state, remembered for the next start (main_desktop.cpp).
void DesktopSaveWindowState (THostWindowMode Mode, unsigned nWidth, unsigned nHeight);
// Start again at the boot menu's page nTab, the window as given (main_desktop.cpp).
void DesktopStartForMenu (THostWindowMode Mode, unsigned nWidth, unsigned nHeight, int nTab);
void DesktopWindowClose (void);
// Before DesktopWindowOpen: the welcome page's window, borderless and sized
// with the mouse, its size its own (never saved for the boot menu); the boot
// menu's window, which it becomes after, is Mode, nWidth × nHeight.
void DesktopWindowForWelcome (THostWindowMode Mode, unsigned nWidth, unsigned nHeight);
// From the engine thread, once the welcome page is done: the window becomes
// the boot menu's, by the main thread, and the output with it.
void DesktopWelcomeEnd (void);
// The check for a newer Okapia (install_host.cpp, R5): on or off, kept in the
// app's support folder; and one now, the last answer not trusted.
bool DesktopUpdateCheckOn (void);
void DesktopUpdateCheckSet (bool bOn);
void DesktopUpdateCheckNow (void);
// The welcome window resized as the mouse would (the test mode's --resize).
void DesktopWindowResize (unsigned nWidth, unsigned nHeight);
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
// SDL's menus with Control instead of Command (menus_desktop.mm, macOS).
void DesktopMenusToControl (void);
// Whether Shift is held now (menus_desktop.mm): at launch, it asks for a folder.
bool DesktopShiftHeld (void);
// The host's menu bar over a full-screen window (menus_desktop.mm).
void DesktopMenuBarVisible (void *pNSWindow, bool bShown);
// The File menu (menus_desktop.mm).
void DesktopRevealWorkspace (const char *pPath);
int  DesktopTakeMenuRequest (void);
// The welcome page for another workspace (main_desktop.cpp), Control-O.
void DesktopChangeWorkspace (void);
// The test mode's next --pick, or false when there is none.
bool DesktopScriptPick (char *pOut, unsigned nSize);
// The window's title follows the workspace.
void DesktopRootChanged (void);
// The main loop's turn for them (main_desktop.cpp), and full screen (host).
void DesktopMenuRequests (void);
void DesktopToggleFullScreen (void);
// The Macintosh's rectangle in the output (BoardOutputShows), magnified alone.
void DesktopMacArea (unsigned nX, unsigned nY, unsigned nWidth, unsigned nHeight, unsigned nScale);
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
