/*
 * resolver.cpp — the sliver of the historical resolver API that ether.cpp needs.
 *
 * Okapia resolves nothing: the emulated Mac runs its own TCP/IP stack and does
 * its own lookups. This exists so upstream's ether.cpp compiles unmodified, and
 * says so if it is ever actually called.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "config.h"
#include <stdio.h>

struct hostent *gethostbyname (const char *name)
{
    fprintf (stderr, "okapia: gethostbyname(\"%s\") is not implemented\n",
             name != 0 ? name : "(null)");
    return 0;
}
