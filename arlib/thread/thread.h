#pragma once
#include "../global.h"

#include "atomic.h"

#ifdef ARLIB_THREAD
//It is safe (though may yield a performance penalty) to malloc() something in one thread and free() it in another.
//A thread is rather heavy; for short-running jobs, use thread_create_short or thread_split.
enum priority_t { pri_default = 0, pri_high, pri_low, pri_idle };
void thread_create(function<void()>&& start, priority_t pri = pri_default);

//Returns the number of threads to create to utilize the system resources optimally.
unsigned int thread_num_cores();
//Returns the number of low-priority threads that can be created as pri_idle without interfering with other programs.
unsigned int thread_num_cores_idle();

#include <string.h>
#if defined(__unix__)
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

//This is a simple tool that ensures only one thread is doing a certain action at a given moment.
//Memory barriers are inserted as appropriate. Any memory access done while holding a lock is finished while holding this lock.
//This means that if all access to an object is done exclusively while holding the lock, no further synchronization is needed.
//It is not allowed for a thread to call lock() or try_lock() while holding the lock already. It is not allowed
// for a thread to release the lock unless it holds it. It is not allowed to delete the lock while it's held.
//However, it it allowed to hold multiple locks simultaneously.
//lock() is not guaranteed to release the CPU if it can't grab the lock. It may be implemented as a
// busy loop, or a hybrid scheme that spins a few times and then sleeps.
//Remember to create all relevant mutexes before creating a thread.
class mutex : nomove {
protected:
#if defined(__unix__)
	pthread_mutex_t mut;
	
	class noinit {};
	mutex(noinit) {}
public:
	mutex() { pthread_mutex_init(&mut, NULL); }
	void lock() { pthread_mutex_lock(&mut); }
	bool try_lock() { return pthread_mutex_trylock(&mut); }
	void unlock() { pthread_mutex_unlock(&mut); }
	~mutex() { pthread_mutex_destroy(&mut); }
	
#elif _WIN32_WINNT >= _WIN32_WINNT_LONGHORN
	SRWLOCK srwlock = SRWLOCK_INIT;
	
public:
	void lock() { AcquireSRWLockExclusive(&srwlock); }
	bool try_lock() { return TryAcquireSRWLockExclusive(&srwlock); }
	void unlock() { ReleaseSRWLockExclusive(&srwlock); }
	
#elif defined(_WIN32)
	CRITICAL_SECTION cs;
	
public:
	mutex() { InitializeCriticalSection(&cs); }
	void lock() { EnterCriticalSection(&cs); }
	bool try_lock() { return TryEnterCriticalSection(&cs); }
	void unlock() { LeaveCriticalSection(&cs); }
	~mutex() { DeleteCriticalSection(&cs); }
#endif
};

// Recursive mutex - can be locked while holding it. Should be unlocked as many times as it's locked.
class mutex_rec : public mutex {
#if defined(__unix__)
public:
	mutex_rec();
#else
private: // unimplemented
	mutex_rec() = delete;
#endif
};

class mutexlocker : nomove {
	mutexlocker() = delete;
	mutex* m;
public:
	mutexlocker(mutex* m) { this->m =  m; this->m->lock(); }
	mutexlocker(mutex& m) { this->m = &m; this->m->lock(); }
	//void unlock() { if (this->m) this->m->unlock(); this->m = NULL; }
	//~mutexlocker() { unlock(); }
	~mutexlocker() { m->unlock(); }
};
#define synchronized(mutex) using(mutexlocker LOCK(mutex))


//A semaphore is a cross-thread notification mechanism; the intention is one thread waits, the other releases. It starts locked.
//Can be used as a mutex, but a real mutex is faster.
//It is possible to release a semaphore multiple times, then wait multiple times.
//wait() is guaranteed to release the CPU after a while.
//Can be destroyed while fully locked, or while having a few releases, but not while anyone is waiting.
class semaphore : nomove {
#ifdef __unix__
	sem_t sem;
public:
	semaphore() { sem_init(&sem, false, 0); }
	void release() { sem_post(&sem); }
	void wait() { while (sem_wait(&sem)<0 && errno==EINTR) {} } // why on earth is THIS one interruptible
	~semaphore() { sem_destroy(&sem); }
#endif
#ifdef _WIN32
	HANDLE sem;
public:
	semaphore() { sem = CreateSemaphore(NULL, 0, 1000, NULL); }
	void release() { ReleaseSemaphore(sem, 1, NULL); }
	void wait() { WaitForSingleObject(sem, INFINITE); }
	~semaphore() { CloseHandle(sem); }
#endif
};


class runonce : nomove {
#if defined(__linux__)
	enum { st_uninit, st_busy, st_busy_waiters, st_done };
	int futex = st_uninit; // I don't need more than a byte, but futex is int only, so int it is
public:
	void run(function<void()> fn);
#elif defined(_WIN32) && _WIN32_WINNT >= _WIN32_WINNT_LONGHORN
	INIT_ONCE once = INIT_ONCE_STATIC_INIT;
	static BOOL CALLBACK wrap(INIT_ONCE* once, void* param, void** context)
	{
		(*(function<void()>*)param)();
		return TRUE;
	}
public:
	void run(function<void()> fn)
	{
		InitOnceExecuteOnce(&once, wrap, (void**)&fn, NULL); // parameter order is wrong in MSDN, check Wine source before refactoring
	}
#else
	enum { st_uninit, st_busy, st_done };
	uintptr_t m_st = st_uninit; // can be the above, or a casted pointer
	
public:
	void run(function<void()> fn);
#endif
};
// A simplified runonce that doesn't support the function<> class, just function pointers without userdata
// In exchange, it's simpler.
class runonce_simple : nomove {
#if defined(__unix__)
	// can't use pthread_once_t in the normal runonce, it has no userdata
	// but it's a perfect match for this one
	pthread_once_t once = PTHREAD_ONCE_INIT;
public:
	void run(funcptr fn) { pthread_once(&once, fn); }
#elif defined(_WIN32) && _WIN32_WINNT >= _WIN32_WINNT_LONGHORN
	INIT_ONCE once = INIT_ONCE_STATIC_INIT;
	static BOOL CALLBACK wrap(INIT_ONCE* once, void* param, void** context)
	{
		((funcptr)param)();
		return TRUE;
	}
public:
	void run(funcptr fn)
	{
		InitOnceExecuteOnce(&once, wrap, (void*)fn, NULL); // parameter order is wrong in MSDN, check Wine source before refactoring
	}
#else
	enum { st_uninit, st_busy, st_done };
	uintptr_t m_st = st_uninit; // can be the above, or a casted pointer
	// pthread_once() exists, but only takes a single void(*)(), no userdata; better reinvent it
	
public:
	void run(funcptr fn);
#endif
};
#define RUN_ONCE(fn) do { static runonce_simple once; once.run(fn); } while(0);
#define RUN_ONCE_FN(name) static void name##_core(); static void name() { RUN_ONCE(name##_core); } static void name##_core()


//void thread_sleep(unsigned int usec);

#ifdef __unix__
static inline size_t thread_get_id() { return pthread_self(); }
#endif
#ifdef _WIN32
static inline size_t thread_get_id() { return GetCurrentThreadId(); }
#endif

//This one creates 'count' threads, calls work() in each of them with 'id' from 0 to 'count'-1, and
// returns once each thread has returned.
//Unlike thread_create, thread_split is expected to be called often, for short-running tasks. The threads may be reused.
void thread_split(unsigned int count, function<void(unsigned int id)> work);


////It is permitted to define this as (e.g.) QThreadStorage<T> rather than compiler magic.
////However, it must support operator=(T) and operator T(), so QThreadStorage is not directly usable. A wrapper may be.
////An implementation must support all {u,}int{8,16,32,64}_t, all basic integral types (char, short, etc), and all pointers.
//#ifdef __GNUC__
//#define THREAD_LOCAL(t) __thread t
//#endif
//#ifdef _MSC_VER
//#define THREAD_LOCAL(t) __declspec(thread) t
//#endif


#ifdef __linux__
//#include <limits.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>

//spurious wakeups are possible
//return can tell if the wakeup is bogus, but it's better to check uaddr
static inline int futex_sleep_if_eq(int* uaddr, int val, const struct timespec * timeout = NULL)
{
	return syscall(__NR_futex, uaddr, FUTEX_WAIT_PRIVATE, val, timeout);
}
static inline int futex_wake(int* uaddr)
{
	return syscall(__NR_futex, uaddr, FUTEX_WAKE_PRIVATE, 1);
}
static inline int futex_wake_all(int* uaddr)
{
	return syscall(__NR_futex, uaddr, FUTEX_WAKE_PRIVATE, INT_MAX);
}
#endif

#else

//Some parts of Arlib want to work with threads disabled, but pretend threads exist.
class mutex : nomove {
public:
	void lock() {}
	bool try_lock() { return true; }
	void unlock() { }
};
class mutex_rec : public mutex {};
class semaphore : nomove {
public:
	void release() {}
	void wait() {}
};
#define RUN_ONCE(fn) do { static bool first=true; if (first) { first=false; fn(); } } while(0)
#define RUN_ONCE_FN(name) static void name##_core(); static void name() { RUN_ONCE(name##_core); } static void name##_core()
#define synchronized(mutex) if (true)
static inline size_t thread_get_id() { return 0; }
static inline unsigned int thread_num_cores() { return 1; }

#endif
