/*
 * okapia_icons.h — the marks the chrome wears, and the pictures its rows carry.
 *
 * Split out of the theme because they are not the theme: the theme decides what
 * a button looks like and how much room it takes, while these are drawings that
 * happen to be monochrome. Keeping them in okapia_theme.cpp made that file two
 * subjects long.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_icons_h
#define _okapia_icons_h

#include "okapia_gfx.h"

// The chrome's own marks are painted, not stored. A sixteen-pixel bitmap has
// no larger version to fall back on and is the one thing a magnified interface
// would still have to enlarge; computed from the rectangle it is given, an icon is as sharp at
// 2.25 as at 1. It also settles a smaller matter: the chrome now quotes no
// system at all, which is what §7.12 asked for in the first place.
//
// The background is passed because some of these cut a notch out of themselves
// — the power ring's gap is not a colour, it is an absence.
typedef void (*TIconPainter) (TSurface *, const TRect &, TOkapiaColor, TOkapiaColor);

void OkapiaPaintSettings (TSurface *, const TRect &, TOkapiaColor, TOkapiaColor);
void OkapiaPaintPram (TSurface *, const TRect &, TOkapiaColor, TOkapiaColor);
void OkapiaPaintPower (TSurface *, const TRect &, TOkapiaColor, TOkapiaColor);

// A circled letter, for the pane that says what Okapia found. Its "i" comes
// from the typeface for the same reason the caution mark's exclamation does:
// a letter drawn with rectangles is a letter that stops looking like one as
// soon as the interface is scaled.
void OkapiaPaintInfo (TSurface *, const TRect &, TOkapiaColor, TOkapiaColor);

/*
 *  The three marks an alert may wear
 *
 *  The Macintosh had three levels and they are worth keeping, because they are
 *  a promise about consequences and not a decoration: a note says something
 *  happened, a caution says this may cost you something, a stop says it cannot
 *  go on. Answering with the wrong one teaches people to click past all three.
 *
 *  The drawings are ours. Apple's note was a face in profile beside a speech
 *  balloon and its stop a raised hand in an octagon; §7.12 says the chrome
 *  quotes no system, so what is kept is the shape that carries the meaning —
 *  the balloon, the triangle, the octagon — and not the artwork.
 */
void OkapiaPaintNote (TSurface *, const TRect &, TOkapiaColor, TOkapiaColor);

// A triangle and an exclamation, which is what has said "read this before you
// answer" since long before the Macintosh — and the one mark that has to be
// legible at a glance, since an alert is read in the second before someone
// clicks past it.
void OkapiaPaintCaution (TSurface *, const TRect &, TOkapiaColor, TOkapiaColor);

void OkapiaPaintStop (TSurface *, const TRect &, TOkapiaColor, TOkapiaColor);

// Content icons: the era belongs to these, not to the chrome around them.
extern const TGlyphImage OkapiaIconSystem6;
extern const TGlyphImage OkapiaIconSystem7;
extern const TGlyphImage OkapiaIconMacOS8;
extern const TGlyphImage OkapiaIconMacOS9;

// Okapia's own, and the only one here that is drawn rather than traced: a
// CD-ROM is a medium, not an era. assets/icons/cdrom.svg is the drawing and
// scripts/svg-to-icon.py is what reduces it.
extern const TGlyphImage OkapiaIconCdrom;
#endif

