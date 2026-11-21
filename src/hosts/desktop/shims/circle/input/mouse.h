// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _circle_input_mouse_h
#define _circle_input_mouse_h
#define MOUSE_BUTTON_LEFT   (1 << 0)
#define MOUSE_BUTTON_RIGHT  (1 << 1)
#define MOUSE_BUTTON_MIDDLE (1 << 2)

typedef void TMouseStatusHandler (unsigned nButtons, int nDisplacementX, int nDisplacementY,
                                  int nWheelMove);
class CMouseDevice
{
};
#endif
