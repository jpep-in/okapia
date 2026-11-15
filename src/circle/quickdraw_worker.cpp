/*
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "quickdraw_worker.h"
#include "quickdraw_blit.h"
#include <circle/logger.h>
#include <circle/memory.h>
#include <circle/multicore.h>

#if defined(ARM_ALLOW_MULTI_CORE) && RASPPI == 4
namespace
{
enum class Status : unsigned { Idle, Pending, Done };
alignas(64) Status s_status;
alignas(64) QDBlit s_job;
static bool s_ready;
static bool s_stopping;

void Notify ()
{
    // Publish the job/completion globally before waking the other core.
    asm volatile ("dsb ishst; sev" ::: "memory");
}

class CQDWorker final : public CMultiCoreSupport
{
public:
    CQDWorker () : CMultiCoreSupport (CMemorySystem::Get ()) {}

    void Run (unsigned core) override
    {
        if (core == 0)
            return;
        while (!__atomic_load_n (&s_stopping, __ATOMIC_ACQUIRE))
        {
            if (core == 2 && __atomic_load_n (&s_status, __ATOMIC_ACQUIRE) == Status::Pending)
            {
                QDRunBlit (s_job, s_job.height / 2, s_job.height);
                __atomic_store_n (&s_status, Status::Done, __ATOMIC_RELEASE);
                Notify ();
            }
            asm volatile ("wfe" ::: "memory");
        }
    }
};
}
#endif

void QDStartWorker ()
{
#if defined(ARM_ALLOW_MULTI_CORE) && RASPPI == 4
    // Circle keeps this instance for the life of the board.
    static CQDWorker worker;
    s_ready = worker.Initialize ();
    CLogger::Get ()->Write ("quickdraw", LogNotice, "parallel worker %s",
                            s_ready ? "ready on core 2" : "unavailable, using core 0");
#endif
}

void QDStopWorker ()
{
#if defined(ARM_ALLOW_MULTI_CORE) && RASPPI == 4
    __atomic_store_n (&s_stopping, true, __ATOMIC_RELEASE);
    Notify ();
#endif
}

bool QDParallelBlit (const QDBlit &blit)
{
#if defined(ARM_ALLOW_MULTI_CORE) && RASPPI == 4
    constexpr size_t minBytes = 256 * 1024;
    if (s_ready && !blit.overlaps && !blit.serial && blit.height >= 2 &&
        size_t (blit.rowBytes) * blit.height >= minBytes &&
        __atomic_load_n (&s_status, __ATOMIC_RELAXED) == Status::Idle)
    {
        s_job = blit;
        __atomic_store_n (&s_status, Status::Pending, __ATOMIC_RELEASE);
        Notify ();
        QDRunBlit (blit, 0, blit.height / 2);
        while (__atomic_load_n (&s_status, __ATOMIC_ACQUIRE) != Status::Done)
            asm volatile ("wfe" ::: "memory");
        __atomic_store_n (&s_status, Status::Idle, __ATOMIC_RELAXED);
        return true;
    }
#else
    (void) blit;
#endif
    return false;
}
