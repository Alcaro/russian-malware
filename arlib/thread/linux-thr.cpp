#ifdef __linux__
#include "thread.h"

// pretty much same as the generic implementation, except I can use futex instead of the linked list - much easier

void runonce::run(function<void()> fn)
{
	int st = lock_read<lock_acq>(&futex);
	if (LIKELY(st == st_done)) return;
	if (st == st_uninit)
	{
		st = lock_cmpxchg<lock_loose, lock_loose>(&futex, st_uninit, st_busy);
		if (st == st_uninit)
		{
			fn();
			st = lock_xchg<lock_rel>(&futex, st_done);
			if (st != st_busy)
				futex_wake_all(&futex);
			return;
		}
		else if (st == st_done) return;
	}
	
	// don't bother spinning, just sleep immediately
	
	lock_cmpxchg<lock_loose, lock_loose>(&futex, st_busy, st_busy_waiters); // ignore whether this succeeds
	do { futex_sleep_if_eq(&futex, st_busy_waiters); }
	while (lock_read<lock_acq>(&futex) == st_busy_waiters);
}
#endif
