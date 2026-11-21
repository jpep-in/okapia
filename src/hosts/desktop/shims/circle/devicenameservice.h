// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
// "ukbd1" and "mouse1" are the desktop's window (board_desktop.cpp).
#ifndef _circle_devicenameservice_h
#define _circle_devicenameservice_h
#ifndef FALSE
#define FALSE false
#define TRUE  true
#endif
class CDeviceNameService
{
public:
    static CDeviceNameService *Get (void);
    void *GetDevice (const char *pName, bool bBlockDevice);
};
#endif
