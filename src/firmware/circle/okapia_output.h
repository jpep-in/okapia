//
// okapia_output.h — the firmware's one claim on the display.
//
// The frame buffer is claimed once for the life of the board and lent out, for
// the same reason okapia_input.cpp holds the one mouse registration: the screen
// changes hands several times — firmware, Macintosh, firmware again on every
// restart from Mac OS — and a claim released and taken again at each handover
// is a mailbox transaction that has to succeed every single time. It did not:
// after a few rounds the boot failed and the machine stopped.
//
// Nothing about the surface changes between owners anyway. The display's size
// and depth are the display's, not ours, so the second claim could only ever
// ask for exactly what the first one already has.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_output_h
#define _okapia_output_h

#include <circle/bcmframebuffer.h>

// The display, 32 bits per pixel, at whatever size the firmware granted — which
// is asked for as 0x0 and reported rather than assumed, because a Pi 5 ignores
// the request outright. Answers 0 when there is no usable frame buffer; that is
// not fatal, a machine with no screen still boots.
//
// Never delete what this returns.
CBcmFrameBuffer *FwOutputClaim (void);

// Whether red is the low byte of an output pixel. The firmware answers, and the
// answer is not the same everywhere: a Raspberry Pi scans out blue in the low
// byte, as Circle's COLOR32 macro says, while QEMU's bcm2835-fb defaults to red
// there. Colours were fixed once against QEMU alone, and a real board then
// showed every Macintosh colour with red and blue exchanged. Valid after a
// successful FwOutputClaim().
bool FwOutputRedLow (void);

#endif
