#ifdef __linux__
#include "../endian.h"
#include "thread.h"

// pretty much same as the generic implementation, except I can use futex instead of the linked list - much easier

void runonce::run(function<void()> fn)
{
	int st = lock_read_acq(&futex);
	if (st == st_done) return;
	if (st == st_uninit)
	{
		st = lock_cmpxchg_loose(&futex, st_uninit, st_busy);
		if (st == st_uninit)
		{
			fn();
			st = lock_cmpxchg_rel(&futex, st_busy, st_done);
			if (st != st_busy)
			{
				lock_write_loose(&futex, st_done);
				futex_wake_all(&futex);
			}
			return;
		}
		else if (st == st_done) return;
	}
	
	// don't bother spinning, just sleep immediately
	
	lock_cmpxchg_loose(&futex, st_busy, st_busy_waiters); // ignore whether this succeeds
	while (lock_read_acq(&futex) == st_busy_waiters)
		futex_sleep_if_eq(&futex, st_busy_waiters);
}
#endif
