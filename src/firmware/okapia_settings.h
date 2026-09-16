/*
 * okapia_settings.h — the settings screen.
 *
 * What is here and what is not is the whole design, and the rule is the same
 * one §7.12 states: a setting earns its place when the guest cannot make it
 * itself, when the need appears *after* the card was written, when Okapia's own
 * code really consumes it, and — the point of the firmware — when it can
 * unblock a machine that will not start. Everything else stays in the file,
 * where it has always been editable, and the README documents it.
 *
 * So the resolution is absent (the display's, not ours — and a manual override
 * would work on a Pi 3 and be a no-op on a Pi 5, which is the worst kind of
 * setting), the date and time are absent (Mac OS has a control panel for them
 * and its write is intercepted, §7.7), and the network is absent until there is
 * a choice worth making beyond on and off.
 *
 * Like the chooser, this knows nothing about a preferences file. It is handed
 * values and answers values; the caller reads and writes. That is what lets the
 * whole screen be driven by synthetic events on a development machine.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_settings_h
#define _okapia_settings_h

#include "okapia_widgets.h"

// Where the Mac's sound comes out. Four states and not a boolean, because
// audio_circle.cpp hard-codes the jack — which a Pi 5 does not have, and which
// is the wrong socket on a television. Off is first so that a card whose value
// is missing or unreadable stays silent: a device claimed and not working is
// what froze the guest once already (docs/topics/sound.md).
enum TSoundOutput
{
    SoundOff,
    SoundHDMI,
    SoundJack,
    SoundUSB,
    SoundOutputCount
};

// The name of a shared volume ends up in a Pascal string in the volume record,
// so twenty-seven characters and a terminator is all there is room for.
static const unsigned SETTINGS_NAME = 28;

// The folder on the card that the Mac sees. A path and not a name: it is the
// one setting here that names something outside the Macintosh.
static const unsigned SETTINGS_PATH = 64;

struct TSettingsValues
{
    unsigned nMemoryMB;
    int      nFrameSkip;                // 0 is Dynamic; otherwise VBLs per frame
    int      nMouseDpi;                 // what the pointing device reports per inch
    unsigned nSound;                    // TSoundOutput
    unsigned nLanguage;                 // TLanguage
    // Open the boot menu at every start, without holding Option and without
    // waiting out the window. A habit rather than a setting of the Macintosh:
    // whoever changes System every day wants it, whoever changes it twice a
    // year does not — and a board with no keyboard has no other way to ask.
    bool     bBootMenu;
    bool     bShared;                   // the shared folder is offered at all
    char     SharedPath[SETTINGS_PATH]; // where it is on the card
    char     SharedName[SETTINGS_NAME]; // what the Mac calls it on the desktop
};

struct TSettings
{
    TSettingsValues V;
    // One bit per TSoundOutput, for the outputs this board actually has. Circle
    // builds USB audio for a Pi 4 and a Pi 5 only, so on a Pi 3 the entry has
    // to be there and unusable rather than absent: an option that vanishes
    // between two machines reads as a version difference, and a greyed one says
    // what is true. Zero means "offer them all", so a caller that has not
    // thought about it does not silently offer nothing.
    unsigned nSoundAvailable;
    // What they were when the screen opened. Kept so that the screen can say
    // whether anything asked for a restart, which is not the same question as
    // whether anything changed.
    TSettingsValues Opened;
};

enum TSettingsAction
{
    SettingsNothing,                    // the model changed; repaint and carry on
    // The model changed in a way that moves things: the language, whose labels
    // are not the same width twice. Lay the page out again rather than repaint
    // a geometry that no longer fits its words.
    SettingsRelayout,
    SettingsBack,                       // leave, keeping nothing
    SettingsSave,
    SettingsSaveRestart
};

// Whether what has been changed needs the board to start again — as opposed to
// the Macintosh, which starts again every time the chooser's own button is
// pressed and therefore needs no ceremony at all.
bool SettingsNeedsRestart (const TSettings *pSettings);

// The nearest memory size this screen can offer. A card naming one it cannot —
// hand-edited, or written by another version — would otherwise be shown as the
// closest match and saved as itself, which is the interface lying about what
// pressing the button will do.
unsigned SettingsMemoryOffered (unsigned nMB);

void SettingsDraw (TSurface *pSurface, TSettings *pSettings);
void SettingsRepaint (TSurface *pSurface);
unsigned SettingsWidgets (TWidget **ppList);

// The frame the last layout drew, so a test can check that nothing strayed
// into its border. Every element spilling out was invisible in the numbers
// until the numbers were asked for.
TRect SettingsDialog (void);
void SettingsSync (TSettings *pSettings);
TSettingsAction SettingsOperate (TSettings *pSettings, int nIndex);

#endif
