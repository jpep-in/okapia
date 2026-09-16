/*
 * okapia_strings.cpp — everything the firmware says, in every language it speaks.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_strings.h"

// English, because that is the language the layouts are first read in and the
// one the code is written in. The card's preference overrides it before
// anything is drawn.
static TLanguage s_Language = (TLanguage) 0;

const char *Str (TStringId Id)
{
    return OkapiaStringTable[s_Language][Id];
}

void StringsSetLanguage (TLanguage Language)
{
    if (Language < LanguageCount)
    {
        s_Language = Language;
    }
}

TLanguage StringsLanguage (void)
{
    return s_Language;
}

const char *StringsCode (TLanguage Language)
{
    return OkapiaLanguageCode[Language < LanguageCount ? Language : 0];
}

TLanguage StringsFromCode (const char *pCode)
{
    if (pCode != 0)
    {
        for (unsigned i = 0; i < LanguageCount; i++)
        {
            const char *p = OkapiaLanguageCode[i];
            if (p[0] == pCode[0] && p[1] == pCode[1] && pCode[2] == '\0')
            {
                return (TLanguage) i;
            }
        }
    }
    return (TLanguage) 0;
}

unsigned StrAppend (char *pOut, unsigned nSize, unsigned nAt, const char *pWhat)
{
    if (pWhat == 0)
    {
        return nAt;
    }
    while (*pWhat != '\0' && nAt + 1 < nSize)
    {
        pOut[nAt++] = *pWhat++;
    }
    pOut[nAt] = '\0';
    return nAt;
}

unsigned StrAppendNumber (char *pOut, unsigned nSize, unsigned nAt, unsigned long nValue)
{
    char Digits[12];
    unsigned n = 0;
    do
    {
        Digits[n++] = (char) ('0' + (nValue % 10));
        nValue /= 10;
    }
    while (nValue != 0 && n < sizeof Digits);

    while (n-- > 0 && nAt + 1 < nSize)
    {
        pOut[nAt++] = Digits[n];
    }
    pOut[nAt] = '\0';
    return nAt;
}
