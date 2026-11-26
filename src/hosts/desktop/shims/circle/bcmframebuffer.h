// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
// The output frame buffer, as the compositor borrows it: on a desktop, the
// window's surface (board_desktop.cpp).
#ifndef _circle_bcmframebuffer_h
#define _circle_bcmframebuffer_h
#include <circle/types.h>
class CBcmFrameBuffer
{
public:
    CBcmFrameBuffer (u8 *pBuffer, unsigned nWidth, unsigned nHeight, unsigned nPitch)
    :   m_pBuffer (pBuffer), m_nWidth (nWidth), m_nHeight (nHeight), m_nPitch (nPitch) {}
    uintptr GetBuffer (void) const  { return (uintptr) m_pBuffer; }
    unsigned GetWidth (void) const  { return m_nWidth; }
    unsigned GetHeight (void) const { return m_nHeight; }
    unsigned GetPitch (void) const  { return m_nPitch; }
    unsigned GetDepth (void) const  { return 32; }
private:
    u8 *m_pBuffer;
    unsigned m_nWidth, m_nHeight, m_nPitch;
};
#endif
