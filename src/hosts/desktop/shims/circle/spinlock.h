// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
// CSpinLock as a mutex: on the board it guards against the interrupt on the
// same core; here the tick and the input are other threads.
#ifndef _circle_spinlock_h
#define _circle_spinlock_h
#include <pthread.h>
class CSpinLock
{
public:
    CSpinLock (unsigned = 0) { pthread_mutex_init (&m_Mutex, 0); }
    ~CSpinLock (void) { pthread_mutex_destroy (&m_Mutex); }
    void Acquire (void) { pthread_mutex_lock (&m_Mutex); }
    void Release (void) { pthread_mutex_unlock (&m_Mutex); }
private:
    pthread_mutex_t m_Mutex;
};
#endif
