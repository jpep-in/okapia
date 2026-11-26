//
// okapia_card.h — a path on the card, as the host's file system names it.
//
// Every path Okapia writes in the preferences or builds for itself is the
// card's own, rooted at "/": "/Okapia_Prefs", "/machd76.image". On the Pi
// that is the file system's root, and CardPath() is the identity. A desktop
// opens a folder laid out like a card, so there the same path has to be found
// under that folder: the desktop host maps it (src/hosts/desktop/). The
// preferences and the menu keep the card's form either way, so a card and a
// copy of it say the same thing.
//
// Only the places that open a file call it; a path that is shown or stored is
// never mapped.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_card_h
#define _okapia_card_h

// Okapia_Prefs left by a save that stopped between removing the old file and
// renaming the new one, renamed into place (prefs_okapia.cpp). Called before
// the preferences file is read or written, by the engines and the firmware.
void CardPrefsRecover (void);

#ifdef OKAPIA_HOSTED
// The host's name for a card path; any other path is returned unchanged. The
// answer lives in a small ring of buffers, enough for the two or three a call
// site holds at once.
const char *CardPath (const char *pPath);
// The card file's last writes on the medium, not in the host's cache. On the
// board every write is already there when FatFs returns.
bool CardSync (const char *pPath);
#else
static inline const char *CardPath (const char *pPath)
{
    return pPath;
}
#endif

#endif
