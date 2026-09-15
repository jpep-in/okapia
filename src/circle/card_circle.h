/*
 * card_circle.h — what the card carries, made ready before a Macintosh starts.
 *
 * The steps both engines owe the card before either of them opens a volume:
 * the inventory, the clock floor it makes possible, the repair of a volume an
 * interrupted session left in use, and the name of the volume the session is
 * about. They were the 68k kernel's alone, so a card whose startup volume
 * started the PowerPC engine reached its volumes unrepaired and its clock
 * unraised. Compiled once per engine, like the rest of the platform layer: each
 * copy reads its own engine's preferences.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_CARD_H
#define OKAPIA_CARD_H

// Scan every HFS volume on the card, and list them when `hfsinventory` says so.
// First, because everything below reads what it found.
void CardInventory (void);

// Raise the clock to the latest plausible drLsMod on the card. Never lowers it.
void CardRefineClock (void);

// Check every configured disk, repair what an interrupted session left in use
// (unless `hfsrepair false`), and decide which volume the Macintosh starts from.
// False when there is nothing to start from at all.
bool CardPrepareVolumes (void);

// The volume CardPrepareVolumes() decided on, or "" before it ran.
const char *CardBootVolume (void);

// A card carrying /repair-only asks for the repair and nothing else.
bool CardRepairOnly (void);

#endif
