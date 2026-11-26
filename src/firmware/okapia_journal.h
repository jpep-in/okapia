/*
 * okapia_journal.h — the Journal page: the log on the card, read in the menu.
 *
 * Reading okapia.log used to mean taking the card out. The page shows the end
 * of it in a list that scrolls, the previous session's on request, and holds
 * the two switches that decide what the serial port is made to carry. It takes
 * its text from the caller and touches no file, so a development machine can
 * drive it with a sample (tests/host/).
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _OKAPIA_JOURNAL_H
#define _OKAPIA_JOURNAL_H

#include "okapia_widgets.h"

// How much of the log the page keeps, and how many lines of it. The log on the
// card is 512 KB; a session that fits in 64 KB is the ordinary one, and a
// longer one is shown by its end, which is where the news is.
static const unsigned JOURNAL_TEXT  = 64 * 1024;
static const unsigned JOURNAL_LINES = 1024;

struct TJournal
{
    bool bPrevious;                     // the previous session, and not this one
    bool bSerial;                       // `serial`: the log's lines on the port
    bool bCounters;                     // `perfreport`: the periodic counters
    bool bSerialChanged;
    bool bCountersChanged;

    unsigned nText;
    bool     bCut;                      // the oldest text was dropped: the first line is a fragment
    unsigned nEscape;                   // inside a colour sequence the log carries
    char     Text[JOURNAL_TEXT + 1];    // one more, for the terminator of the last line
};

// Empties the text, keeping the switches.
void JournalClear (TJournal *pJournal);

// Adds what was read from the log, in as many pieces as it took. Control
// characters and the colour sequences are dropped: a line is text, and the page
// is not a terminal. Past JOURNAL_TEXT the oldest text goes, a quarter at a
// time.
void JournalAppend (TJournal *pJournal, const char *pData, unsigned nBytes);

void JournalDraw (TSurface *pSurface, TJournal *pJournal);
void JournalRepaint (TSurface *pSurface);
unsigned JournalWidgets (TWidget **ppList);

enum TJournalAction
{
    JournalNothing,
    JournalReload                       // the other session's text is wanted
};

TJournalAction JournalOperate (TJournal *pJournal, int nIndex);

// The frame the last layout drew, and whether it ran out of room, for the same
// checks the other pages are given.
TRect JournalDialog (void);
bool JournalOverflowed (void);

// How many lines the last JournalDraw put in the list, which a test may compare
// with what it fed in.
unsigned JournalLines (void);

#endif
