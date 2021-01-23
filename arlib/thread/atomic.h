#pragma once
#include <type_traits>

#ifdef ARLIB_THREAD
// This header defines several functions for atomically operating on integers or pointers.
// The following functions exist:
// T lock_read(T*)
// void lock_write(T*, T)
// T lock_xchg(T*, T)
// T lock_cmpxchg(T*, T, T)
// T lock_incr(T*) (increment; integers only, because adding 1 to a pointer is ambiguous, enum math should be explicit, and atomic float is just silly)
// T lock_decr(T*)
// All (except write) return the value before the operation. (cmp)xchg/read obviously do, so to ease memorization, the others do too.
// If your chosen platform doesn't support atomic operations on that size, you'll get a compile error.

// If you don't know what memory ordering to use, use acq-rel for rmw ops, acq for read-only, and rel for write-only.
// Most guides recommend seq cst for everything, but I have not been able to find any realistic usecase for that,
//  or even any situation where it matters, other than at least three threads doing something dubious with at least two variables.
// cmpxchg takes two orderings; first on success, second on failure (read only).

// If doing cmpxchg on pointers and array indices, make sure you're not vulnerable to ABA problems.

#if defined(__GNUC__)
// https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html

enum lockorder_t {
	lock_seqcst = __ATOMIC_SEQ_CST,
	lock_acqrel = __ATOMIC_ACQ_REL,
	lock_acq = __ATOMIC_ACQUIRE,
	lock_rel = __ATOMIC_RELEASE,
	lock_loose = __ATOMIC_RELAXED
};

template<lockorder_t order, typename T>
T lock_read(T* val) { return __atomic_load_n(val, order); }
template<lockorder_t order, typename T, typename T2>
void lock_write(T* val, T2 newval) { __atomic_store_n(val, newval, order); }
template<lockorder_t order, typename T>
T lock_incr(T* val) { static_assert(std::is_integral_v<T>); return __atomic_fetch_add(val, 1, order); }
template<lockorder_t order, typename T>
T lock_decr(T* val) { static_assert(std::is_integral_v<T>); return __atomic_fetch_add(val, 1, order); }
template<lockorder_t order, typename T, typename T2>
T lock_xchg(T* val, T2 newval) { return __atomic_exchange_n(val, newval, order); }
template<lockorder_t order, lockorder_t orderfail, typename T, typename T2>
T lock_cmpxchg(T* val, T2 oldval, T2 newval)
{
	T oldvalcast = oldval;
	__atomic_compare_exchange_n(val, &oldvalcast, newval, true, order, orderfail);
	return oldvalcast;
}

#elif defined(_MSC_VER)
#include "../global.h"
#include <windows.h>

// ordering is ignored under MSVC; it's separate functions, too annoying to parameterize
// (and most of them only exist on windows 8+ sdk, though they map to instructions that exist for everything)
enum lockorder_t { lock_seqcst, lock_acqrel, lock_acq, lock_rel, lock_loose };

// in older MSVC, volatile access was guaranteed to always fence; in newer, that's deprecated, and off by default on ARM (/volatile:ms)
// instead, let's rely on https://docs.microsoft.com/en-us/windows/win32/sync/interlocked-variable-access, which says
// "Simple reads and writes to properly-aligned 32-bit variables are atomic operations." (as of september 2020)
// however, I believe that's only NoTear / Unordered atomic; lock_read_loose needs at least Relaxed / Monotonic, so more manual fences
// (though it's unclear to me whether NoTear/Relaxed are the same for read-only or write-only operations)
// it's unambiguously a race condition and UB per the C++ standard, hope MSVC doesn't try to loosen that guarantee on future platforms...
template<lockorder_t order, typename T>
T lock_read(T* val) { static_assert(sizeof(T)==4 || sizeof(T)==sizeof(void*));
	T ret = *val; MemoryBarrier(); return ret; }

template<lockorder_t order, typename T>
T lock_write(T* val, T newval) { static_assert(sizeof(T)==4 || sizeof(T)==sizeof(void*));
	MemoryBarrier(); *val = newval; }

template<lockorder_t order, typename T>
T lock_incr(T* val)
{
	static_assert(std::is_integral_v<T>);
	if constexpr (sizeof(T)==4) return InterlockedIncrement((volatile LONG*)val) - 1;
#ifdef _WIN64
	else if constexpr (sizeof(T)==8) return InterlockedIncrement64((volatile LONG64*)val)-1;
#endif
	else static_assert(sizeof(T) < 0);
}

template<lockorder_t order, typename T>
T lock_decr(T* val)
{
	static_assert(std::is_integral_v<T>);
	if constexpr (sizeof(T)==4) return InterlockedDecrement((volatile LONG*)val) + 1;
#ifdef _WIN64
	else if constexpr (sizeof(T)==8) return InterlockedDecrement64((volatile LONG64*)val) + 1;
#endif
	else static_assert(sizeof(T) < 0);
}

template<lockorder_t order, typename T, typename T2>
T lock_xchg(T* val, T2 newval)
{
	static_assert(sizeof(T) == sizeof(T2));
	if constexpr (sizeof(T)==4) return InterlockedExchange((volatile LONG*)val, newval);
#ifdef _WIN64
	else if constexpr (sizeof(T)==8) return InterlockedExchange64((volatile LONG64*)val, newval);
#endif
	else static_assert(sizeof(T) < 0);
}

template<lockorder_t order, lockorder_t orderfail, typename T, typename T2>
T lock_cmpxchg(T* val, T2 oldval, T2 newval)
{
	static_assert(sizeof(T) == sizeof(T2));
	if constexpr (sizeof(T)==4) return InterlockedCompareExchange((volatile LONG*)val, oldval, newval);
#ifdef _WIN64
	else if constexpr (sizeof(T)==8) return InterlockedCompareExchange64((volatile LONG64*)val, oldval, newval);
#endif
	else static_assert(sizeof(T) < 0);
}

#endif

#else

// define the functions even if single threaded, so same code can work in both cases
// consistency models are obviously not relevant in this case
enum lockorder_t { lock_seqcst, lock_acqrel, lock_acq, lock_rel, lock_loose };

template<lockorder_t order, typename T> T lock_read(T* val) { return *val; }
template<lockorder_t order, typename T, typename T2> void lock_write(T* val, T2 newval) { *val = newval; }
template<lockorder_t order, typename T> T lock_incr(T* val) { return (*val)++; }
template<lockorder_t order, typename T> T lock_decr(T* val) { return (*val)--; }
template<lockorder_t order, typename T, typename T2> T lock_xchg(T* val, T2 newval) { T ret = *val; *val = newval; return ret; }
template<lockorder_t order, lockorder_t orderfail, typename T, typename T2>
T lock_cmpxchg(T* val, T2 oldval, T2 newval) { T ret = *val; if (*val == oldval) *val = newval; return ret; }

#endif
