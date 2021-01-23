#include "thread.h"

#if (defined(_WIN32) && _WIN32_WINNT < _WIN32_WINNT_LONGHORN) || (defined(__unix__) && !defined(__linux__))
// design criteria:
// - safe and correct, of course
// - zero malloc and syscalls if uncontented (stack allocations and cmpxchg are fine)
// - sizeof(runonce) <= sizeof(void*)
// - performance given multiple waiters can safely be ignored
//
// data use:
// 0 - not initialized
// 1 - busy, no waiters
// 2 - finished
// other - casted pointer to state struct on each thread's stack, containing
// - a semaphore (unique for each waiter - yes, it's wasteful, but multiple waiters are rare)
// - state* next (or prev, it's an unordered set) ((void*)1 for the first waiter)
//
// procedure:
// read_acq()
// if 2: return
// if 0: first thread
// if anything else: not first thread
//
// first thread:
//   cmpxchg_loose(0 -> 1)
//   on failure, not first thread, go to the other branch
//   fn()
//   xchg_acqrel(-> 2)
//   if xchg return value is not 1:
//     it's the linked list head, so SetEvent each one (read next before calling SetEvent, to dodge another race condition)
//
// other threads:
//   create event, put in struct, along with next = (void*) read_acq result
//   cmpxchg_rel(1 -> struct)
//   on failure, put prev val in struct, repeat the above cmpxchg
//   unless prev is st_done, in which case just return
//   wait on event
//   delete event

// it's hard to test things like this...
void runonce::run(function<void()> fn)
{
	struct state {
		semaphore sem; // a unique semaphore for each waiter; wasteful, but multiple waiters are rare,
		state* next;   // and I can't figure out how to solve lifetime issues without that
	};
	
	uintptr_t st = lock_read<lock_acq>(&m_st);
	if (LIKELY(st == st_done)) return;
	if (st == st_uninit)
	{
		st = lock_cmpxchg<lock_loose, lock_loose>(&m_st, st_uninit, st_busy);
		if (st == st_uninit)
		{
			fn();
			st = lock_xchg<lock_acqrel>(&m_st, st_done);
			state* stp = (state*)st;
			while (stp != (state*)st_busy)
			{
				state* next = stp->next;
				stp->sem.release();
				stp = next;
			}
			return;
		}
		else if (st == st_done) return;
		// else it's either 'busy, no waiters' or a pointer; in both cases, insert ourselves
	}
	
	// don't bother spinning, just sleep immediately
	
	state sts;
	while (true)
	{
		uintptr_t prevst = st;
		sts.next = (state*)st;
		st = lock_cmpxchg<lock_rel, lock_loose>(&m_st, st, (uintptr_t)&sts);
		if (st == prevst) break;
		if (st == st_done) return;
	}
	sts.sem.wait();
}
#endif

#include "../test.h"
#include "../os.h"
#ifndef _WIN32
#include <unistd.h> // usleep
#else
#define usleep(n) Sleep((n)/1000)
#endif

// numbers must be high, Valgrind's and Windows' schedulers are unpredictable at best
#define US_DELAYSTART     500000
#define US_INIT          1000000
#define US_TOLERANCE      400000
#define US_TOLERANCE_NEG   10000
static_assert(US_TOLERANCE+US_TOLERANCE_NEG < US_DELAYSTART);
static_assert(US_TOLERANCE+US_TOLERANCE_NEG < US_INIT - US_DELAYSTART);

test("thread runonce","","thread")
{
	test_skip("kinda slow");
	
	test_nothrow {
	
	runonce once;
	semaphore sem;
	timer t;
	
	for (int thread_id=0;thread_id<4;thread_id++)
	{
		thread_create([&sem, &once, &t]() {
			assert_range(t.us(), 0, US_TOLERANCE);
			usleep(US_DELAYSTART);
			assert_range(t.us(), US_DELAYSTART-US_TOLERANCE_NEG, US_DELAYSTART+US_TOLERANCE);
			once.run([](){ assert_unreachable(); });
			assert_range(t.us(), US_INIT-US_TOLERANCE_NEG, US_INIT+US_TOLERANCE);
			sem.release();
		});
	}
	once.run([&t](){
		assert_range(t.us(), 0, US_TOLERANCE);
		usleep(US_INIT);
		assert_range(t.us(), US_INIT-US_TOLERANCE_NEG, US_INIT+US_TOLERANCE);
	});
	once.run([](){ assert_unreachable(); });
	
	for (int thread_id=0;thread_id<4;thread_id++)
		sem.wait();
	
	assert_range(t.us(), US_INIT-US_TOLERANCE_NEG, US_INIT+US_TOLERANCE);
	}
}
