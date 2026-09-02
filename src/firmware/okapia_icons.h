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

// The mark an alert wears. A triangle and an exclamation, which is what has
// said "read this before you answer" since long before the Macintosh — and the
// one icon of the chrome that has to be legible at a glance, since an alert is
// read in the second before someone clicks past it.
void OkapiaPaintCaution (TSurface *, const TRect &, TOkapiaColor, TOkapiaColor);

// Content icons: the era belongs to these, not to the chrome around them.
extern const TGlyphImage OkapiaIconSystem6;
extern const TGlyphImage OkapiaIconSystem7;
extern const TGlyphImage OkapiaIconMacOS8;
extern const TGlyphImage OkapiaIconMacOS9;
#endif

