#ifdef __linux__
#include "thread.h"

// same idea as the generic implementation, but I can use futex instead of the linked list, much easier
// (generic would've been equally simple if I had a semaphore with an infinite-releases function,
//   but that'd make it bigger than a pointer, and takes a syscall to create on windows < vista)

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
			st = lock_xchg<lock_rel>(&futex, st_done); // generic is acqrel, to read the linked list, but that's not needed here
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
