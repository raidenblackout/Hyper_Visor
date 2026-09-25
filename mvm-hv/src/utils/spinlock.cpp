#include <ntddk.h>
#include "spinlock.h"

static unsigned max_wait = 65536;

spinlock::spinlock(volatile long* _lock) : m_lock(_lock)
{
    lock();
}

spinlock::~spinlock()
{
    unlock();
}

bool spinlock::try_lock()
{
    return (!(*m_lock) && !_interlockedbittestandset(m_lock, 0));
}

void spinlock::lock()
{
    unsigned __int32 wait = 1;

    while (!try_lock())
    {
        for (unsigned __int32 i = 0; i < wait; ++i)
        {
            _mm_pause();
        }

        if (wait * 2 > max_wait)
        {
            wait = max_wait;
        }
        else
        {
            wait = wait * 2;
        }
    }
}

void spinlock::unlock()
{
    *m_lock = 0;
}