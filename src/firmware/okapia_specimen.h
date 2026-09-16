/*
 * okapia_specimen.h — every component, in every state, on one screen.
 *
 * Nothing here is copied from an existing system, so there is nothing to check
 * the theme against except the eye. That makes this screen the only arbiter of
 * the design, and the reason to build it before the boot chooser rather than
 * after: it is also what makes "correcting a part corrects it everywhere"
 * something one can watch happen.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_specimen_h
#define _okapia_specimen_h

#include "okapia_gfx.h"

// The design's own size. It is not a canvas any more: the interface is drawn at
// the surface's resolution, and these are the units the layout is expressed in
// before the theme's scale multiplies them.
static const unsigned SPECIMEN_WIDTH  = 640;
static const unsigned SPECIMEN_HEIGHT = 480;

// How many pages the components need on a surface this size — not a constant,
// because it is not a decision. At 640x480, with a frame, a margin and a rhythm
// that all read correctly, a dialogue does not hold forty components; on a
// roomier display it may. A section that does not fit is moved whole to the
// next page rather than shaved, shaving being what four rounds of crowding
// were. A specimen is a document, and documents paginate.
unsigned SpecimenPageCount (TSurface *pSurface);

// Draws into a surface of any size; the scale follows from it. A page beyond
// the last is the first.
void SpecimenDraw (TSurface *pSurface, unsigned nPage);

// The dialogue the last call drew, so a test can check that nothing strayed
// into its frame. Every element spilling into the border was invisible in the
// numbers until the numbers were asked for.
TRect SpecimenDialog (void);

// The components the last call placed, so a test can check that none of them
// touch. Two controls overlapping is invisible in a margin check and obvious
// on screen; it needs its own measurement.
//
// Mutable, because an event loop above this owns their state: the specimen says
// where a control is, the loop says whether it holds the focus. That division
// is what lets the same screen be a static document here and a live one under
// an emulator without being written twice.
struct TWidget;
unsigned SpecimenWidgets (TWidget **ppList);

// Repaints from the components as they now stand, laying nothing out again.
// SpecimenDraw() is a layout followed by one of these.
void SpecimenRepaint (TSurface *pSurface);

#endif
