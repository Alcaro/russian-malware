#pragma once
#include <type_traits>

#ifdef ARLIB_THREAD
// This header defines several functions for atomically operating on integers or pointers.
// The following functions exist:
// T lock_read(T*)
// void lock_write(T*, T)
// T lock_xchg(T*, T)
// T lock_cmpxchg(T*, T, T)
// T lock_incr(T*) (increment; integers only, because adding 1 to a pointer is ambiguous, and anything else is weird)
// T lock_decr(T*)
// All of them (except write) return the value before the operation.
// (cmp)xchg obviously does, so to ease memorization, the others do too.
// If your chosen platform doesn't support atomic operations on that size, you'll get a compile error.

// All them use acquire-release ordering (except read/write, where only one of acq/rel means anything).
// If you know what you're doing, you can append use lock_xchg<lock_acq>, lock_rel, or lock_loose.
// There are also lock_xchg_loose shortcuts, but only for loose.
// Sequential consistency is not supported; I have not been able to find any usecase for that,
//  or even any situation where it matters, other than at least three threads doing something dubious with at least two variables.
// cmpxchg takes two orderings; one on success, one on failure. By default, it uses same ordering on both.

// If doing cmpxchg on pointers and array indices, make sure you're not vulnerable to ABA problems.

#ifdef __GNUC__
// https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html

#define lock_seqcst __ATOMIC_SEQ_CST
#define lock_acqrel __ATOMIC_ACQ_REL
#define lock_acq __ATOMIC_ACQUIRE
#define lock_rel __ATOMIC_RELEASE
#define lock_loose __ATOMIC_RELAXED

template<int order, typename T>
T lock_read(T* val) { return __atomic_load_n(val, order); }
template<int order, typename T, typename T2>
void lock_write(T* val, T2 newval) { __atomic_store_n(val, newval, order); }
template<int order, typename T>
T lock_incr(T* val) { static_assert(std::is_integral_v<T>); return __atomic_fetch_add(val, 1, order); }
template<int order, typename T>
T lock_decr(T* val) { static_assert(std::is_integral_v<T>); return __atomic_fetch_add(val, 1, order); }
template<int order, typename T, typename T2>
T lock_xchg(T* val, T2 newval) { return __atomic_exchange_n(val, newval, order); }
template<int order, int orderfail = order, typename T, typename T2>
T lock_cmpxchg(T* val, T2 oldval, T2 newval)
{
	T oldvalcast = oldval;
	if constexpr (orderfail == lock_rel) __atomic_compare_exchange_n(val, &oldvalcast, newval, true, order, lock_loose);
	else if constexpr (orderfail == lock_acqrel) __atomic_compare_exchange_n(val, &oldvalcast, newval, true, order, lock_acq);
	else __atomic_compare_exchange_n(val, &oldvalcast, newval, true, order, orderfail);
	return oldvalcast;
}

#elif defined(_MSC_VER)
#include "../global.h"
#include <windows.h>

// forget memory ordering on MSVC, too annoying to parameterize
#define lock_seqcst 0
#define lock_acqrel 0
#define lock_acq 0
#define lock_rel 0
#define lock_loose 0

// in older MSVC, volatile access was guaranteed to always fence; in newer, that's deprecated, and off by default on ARM (/volatile:ms)
// instead, let's rely on https://docs.microsoft.com/en-us/windows/win32/sync/interlocked-variable-access, which says
// "Simple reads and writes to properly-aligned 32-bit variables are atomic operations." (as of september 2020)
// however, I believe that's only NoTear / Unordered atomic; lock_read_loose needs at least Relaxed / Monotonic, so more manual fences
// (though it's unclear to me whether they're the same for read-only or write-only operations)
// it's unambiguously a race condition and UB per the C++ standard, hope MSVC doesn't try to loosen that guarantee on future platforms...
template<int order, typename T>
T lock_read(T* val) { static_assert(sizeof(T)==4 || sizeof(T)==sizeof(void*));
	T ret = *val; MemoryBarrier(); return ret; }

template<int order, typename T>
T lock_write(T* val, T newval) { static_assert(sizeof(T)==4 || sizeof(T)==sizeof(void*));
	MemoryBarrier(); *val = newval; }

template<int order, typename T>
T lock_incr(T* val)
{
	static_assert(std::is_integral_v<T>);
	if constexpr (sizeof(T)==4) return InterlockedIncrement((volatile LONG*)val) - 1;
#ifdef _WIN64
	else if constexpr (sizeof(T)==8) return InterlockedIncrement64((volatile LONG64*)val)-1;
#endif
	else static_assert(sizeof(T) < 0);
}

template<int order, typename T>
T lock_decr(T* val)
{
	static_assert(std::is_integral_v<T>);
	if constexpr (sizeof(T)==4) return InterlockedDecrement((volatile LONG*)val) + 1;
#ifdef _WIN64
	else if constexpr (sizeof(T)==8) return InterlockedDecrement64((volatile LONG64*)val) + 1;
#endif
	else static_assert(sizeof(T) < 0);
}

template<int order, typename T, typename T2>
T lock_xchg(T* val, T2 newval)
{
	static_assert(sizeof(T) == sizeof(T2));
	if constexpr (sizeof(T)==4) return InterlockedExchange((volatile LONG*)val, newval);
#ifdef _WIN64
	else if constexpr (sizeof(T)==8) return InterlockedExchange64((volatile LONG64*)val, newval);
#endif
	else static_assert(sizeof(T) < 0);
}

template<int order, int orderfail = order, typename T, typename T2>
T lock_cmpxchg(T* val, T2 oldval, T2 newval)
{
	static_assert(sizeof(T) == sizeof(T2));
	if constexpr (sizeof(T)==4) return InterlockedCompareExchange((volatile LONG*)val, oldval, newval);
#ifdef _WIN64
	else if constexpr (sizeof(T)==8) return InterlockedCompareExchange((volatile LONG64*)val, oldval, newval);
#endif
	else static_assert(sizeof(T) < 0);
}

#endif

#else

// define the functions even if single threaded, so same code can work in both cases
// if single threaded, all consistency models are identical
#define lock_seqcst 0
#define lock_acqrel 0
#define lock_acq 0
#define lock_rel 0
#define lock_loose 0

template<int order, typename T> T lock_read(T* val) { return *val; }
template<int order, typename T, typename T2> void lock_write(T* val, T2 newval) { *val = newval; }
template<int order, typename T> T lock_incr(T* val) { return (*val)++; }
template<int order, typename T> T lock_decr(T* val) { return (*val)--; }
template<int order, typename T, typename T2> T lock_xchg(T* val, T2 newval) { T ret = *val; *val = newval; return ret; }
template<int order, int order2 = 0, typename T, typename T2>
T lock_cmpxchg(T* val, T2 oldval, T2 newval) { T ret = *val; if (*val == oldval) *val = newval; return ret; }

#endif
