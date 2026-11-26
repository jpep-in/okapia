// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
// CMemorySystem's heap, as malloc. Defined in board_desktop.cpp.
#ifndef _circle_memory_h
#define _circle_memory_h
#include <circle/types.h>
enum { HEAP_LOW, HEAP_HIGH, HEAP_ANY, HEAP_DMA30 };
class CMemorySystem
{
public:
    static CMemorySystem *Get (void);
    static void *HeapAllocate (size_t nSize, int nType);
    static void HeapFree (void *pBlock);
    size_t GetHeapFreeSpace (int nType);
};
#endif
