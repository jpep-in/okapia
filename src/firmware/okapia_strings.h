/*
 * okapia_strings.h — everything the firmware says, in every language it speaks.
 *
 * The text is typed once, in assets/strings.tsv, and the identifiers are
 * generated from the same rows — so a key renamed or removed stops the build
 * rather than leaving an empty label on a screen nobody looked at that day.
 * Same habit as the key table and the faces: nothing about the interface is
 * written down twice.
 *
 * The language is chosen at run time and nothing is reloaded when it changes:
 * the tables are all compiled in, which for two languages and forty strings
 * costs a couple of kilobytes and removes every question about when to read
 * what. Screens ask Str() as they draw, so the whole interface changes on the
 * next paint.
 *
 * This exists before the chooser on purpose. A layout laid out against one
 * language and translated afterwards is a layout that comes apart, and the
 * measurements in tests/host check every page in every language for exactly
 * that reason.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_strings_h
#define _okapia_strings_h

#include "okapia_strings_ids.h"

// The tables themselves. Screens go through Str(); these are here because the
// generated file has to declare something.
extern const char *const OkapiaStringTable[LanguageCount][StringCount];
extern const char *const OkapiaLanguageCode[LanguageCount];

// The text, in the language now chosen. Never answers a null pointer: an
// identifier that does not exist cannot be written, since it comes from the
// same file as the text.
const char *Str (TStringId Id);

void      StringsSetLanguage (TLanguage Language);
TLanguage StringsLanguage (void);

// The two-letter code the preferences carry, and the way back from one. An
// unknown code answers the first language rather than failing: a card written
// by a later version must still boot.
const char *StringsCode (TLanguage Language);
TLanguage   StringsFromCode (const char *pCode);

/*
 *  Putting a line together, without a format string
 *
 *  The firmware has no printf anywhere and there is no reason to acquire one:
 *  three appends say what one format would, and they cannot run off the end of
 *  the buffer while doing it. Each answers where it stopped, so the next one
 *  carries on from there, and the result is always terminated.
 *
 *  They live here because three screens now build text — the chooser's rows,
 *  the settings' menus, the information pane's every line — and the third copy
 *  of a routine is the one that starts to differ from the first two.
 */
unsigned StrAppend (char *pOut, unsigned nSize, unsigned nAt, const char *pWhat);
unsigned StrAppendNumber (char *pOut, unsigned nSize, unsigned nAt, unsigned long nValue);

#endif
