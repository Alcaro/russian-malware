#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE //strdup, realpath, asprintf
#endif
#define _strdup strdup //and windows is being windows as usual

#if defined(__clang__)
#if __clang_major__ < 14
/*
$ echo '#include <coroutine>' | clang++-13 -xc++ -
In file included from <stdin>:1:
/usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/c++/11/coroutine:334:2: error: "the coroutine header requires -fcoroutines"
#error "the coroutine header requires -fcoroutines"
 ^
1 error generated.
$ echo '#include <coroutine>' | clang++-13 -xc++ - -fcoroutines
clang: error: unknown argument: '-fcoroutines'
*/
// silencing the #error with a -D__cpp_impl_coroutine returns a bunch of
// error: std::experimental::coroutine_traits type was not found; include <experimental/coroutine> before defining a coroutine
#error "unsupported clang version; if you're feeling brave, you're welcome to remove this check, but no complaining if it breaks"
#endif
#elif defined(__GNUC__)
#if __GNUC__ < 11
#error "unsupported gcc version; if you're feeling brave, you're welcome to remove this check, but no complaining if it breaks"
#endif
#else
#warning "unknown or unsupported compiler; feel free to try, but no complaining if it breaks"
#endif

// these shouldn't be needed with modern compilers, according to
// https://stackoverflow.com/questions/8132399/how-to-printf-uint64-t-fails-with-spurious-trailing-in-format
// but mingw needs it anyways for whatever reason (last checked on 11.2.0)
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS 1
#define __STDC_FORMAT_MACROS 1
#define __STDC_CONSTANT_MACROS 1 // why are they so many?
#endif
#define _USE_MATH_DEFINES // needed for M_PI on mingw

#ifdef _WIN32
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT _WIN32_WINNT_WIN7
#    define NTDDI_VERSION NTDDI_WIN7
#    define _WIN32_IE _WIN32_IE_WIN7
#  elif _WIN32_WINNT <= 0x0600 // don't replace with _WIN32_WINNT_LONGHORN, windows.h isn't included yet
#    undef _WIN32_WINNT
#    define _WIN32_WINNT _WIN32_WINNT_WS03 // _WIN32_WINNT_WINXP excludes SetDllDirectory, so I need to put it at 0x0502
#    define NTDDI_VERSION NTDDI_WS03 // actually NTDDI_WINXPSP2, but mingw sddkddkver.h gets angry about that
#    define _WIN32_IE _WIN32_IE_IE60SP2 // I don't know what this one controls
#  endif
#  define WIN32_LEAN_AND_MEAN
#  define strcasecmp _stricmp
#  define strncasecmp _strnicmp
#  ifdef _MSC_VER
#    define _CRT_NONSTDC_NO_DEPRECATE
#    define _CRT_SECURE_NO_WARNINGS
#  endif
#  define STRSAFE_NO_DEPRECATE
#  ifdef __MINGW32__
#    define _FILE_OFFSET_BITS 64
// mingw *really* wants to define its own printf/scanf, which adds ~20KB random stuff to the binary
// (on some 32bit mingw versions, it also adds a dependency on libgcc_s_sjlj-1.dll)
// extra kilobytes and dlls is the opposite of what I want, and my want is stronger, so here's some shenanigans
// comments say libstdc++ demands a POSIX printf, but I don't use libstdc++'s text functions, so I don't care
#    define __USE_MINGW_ANSI_STDIO 0 // trigger a warning if it's enabled already - probably wrong include order
#    include <cstdbool>              // include some random c++ header; they all include <bits/c++config.h>,
#    undef __USE_MINGW_ANSI_STDIO    // which ignores my #define above and sets this flag; re-clear it before including <stdio.h>
#    define __USE_MINGW_ANSI_STDIO 0 // (subsequent includes of c++config.h are harmless, there's an include guard)
#  endif
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <inttypes.h>
#include <utility>
#include <new>
#ifndef ARLIB_STANDALONE
#include <stdio.h>
#include "function.h"
#include "cpu.h"
#endif

int rand() __attribute__((deprecated("use g_rand instead")));
void srand(unsigned) __attribute__((deprecated("g_rand doesn't need this")));

#ifdef STDOUT_DELETE
#define puts(x) do{}while(0)
#define printf(x, ...) do{}while(0)
#endif

#ifdef STDOUT_ERROR
#define puts(x) cant_use_stdout_without_a_terminal
#define printf(x, ...) cant_use_stdout_without_a_terminal
#endif

#ifdef _MSC_VER
#pragma warning(disable:4800) // forcing value to bool 'true' or 'false' (performance warning)
#endif

#ifndef __has_include
#define __has_include(x) false
#endif

// in case something is technically undefined behavior, but works as long as compiler can't prove anything
template<typename T> T launder(T v)
{
	static_assert(std::is_trivial_v<T>);
#ifdef _MSC_VER
	_ReadWriteBarrier(); // documented deprecated, but none of the replacements look like a usable compiler barrier
#else
	__asm__("" : "+r"(v));
#endif
	return v;
}

// Returns whatever is currently in that register. The value can be passed around, but any math or deref is undefined behavior.
template<typename T> T nondeterministic()
{
	static_assert(std::is_trivial_v<T>);
#if __has_builtin(__builtin_nondeterministic_value)
	T out;
	return __builtin_nondeterministic_value(out);
#else
	T out;
	// optimizes better with volatile - without it, calling twice will insert the empty asm once and copy the result
	__asm__ volatile("" : "=r"(out));
	return out;
#endif
}

typedef void(*funcptr)();

#define using(obj) if(obj;true)
template<typename T> class defer_holder {
	T fn;
public:
	defer_holder(T&& fn) : fn(std::move(fn)) {}
	defer_holder(const defer_holder&) = delete;
	~defer_holder() { fn(); }
};
template<typename T>
defer_holder<T> dtor(T&& f)
{
	return f;
}
// Useful for implementing context manager macros like test_nomalloc, but should rarely if ever be used directly.
#define contextmanager(begin_expr,end_expr) using(auto DEFER=dtor(((begin_expr),[&](){ end_expr; })))

#define JOIN_(x, y) x ## y
#define JOIN(x, y) JOIN_(x, y)

#define STR_(x) #x
#define STR(x) STR_(x)

#ifdef __GNUC__
#define LIKELY(expr)    __builtin_expect(!!(expr), true)
#define UNLIKELY(expr)  __builtin_expect(!!(expr), false)
#define MAYBE_UNUSED __attribute__((__unused__)) // shut up, stupid warnings
#define KEEP_OBJECT __attribute__((__used__)) // for static unused variables that shouldn't be optimized out
#define forceinline inline __attribute__((always_inline))
#else
#define LIKELY(expr)    (expr)
#define UNLIKELY(expr)  (expr)
#define MAYBE_UNUSED
#define KEEP_OBJECT
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#if __has_builtin(__builtin_unpredictable)
#define UNPREDICTABLE(expr) __builtin_unpredictable(!!(expr))
#else
#define UNPREDICTABLE(expr) (expr)
#endif

using std::nullptr_t;

// used to be a macro, hence the caps name (and it still behaves kinda macro-like)
template<typename T, size_t N> constexpr size_t ARRAY_SIZE(T(&)[N]) { return N; }
template<typename T>           constexpr size_t ARRAY_SIZE(T(&)[0]) { return 0; } // needs an extra overload for some reason

//just to make C++ an even bigger mess. based on https://github.com/swansontec/map-macro with some changes:
//- namespaced all child macros, renamed main one
//- merged https://github.com/swansontec/map-macro/pull/3
//- merged http://stackoverflow.com/questions/6707148/foreach-macro-on-macros-arguments#comment62878935_13459454, plus ifdef
//explanation on what this nonsense does: http://jhnet.co.uk/articles/cpp_magic
#define PPFE_EVAL0(...) __VA_ARGS__
#define PPFE_EVAL1(...) PPFE_EVAL0(PPFE_EVAL0(PPFE_EVAL0(__VA_ARGS__)))
#define PPFE_EVAL2(...) PPFE_EVAL1(PPFE_EVAL1(PPFE_EVAL1(__VA_ARGS__)))
#define PPFE_EVAL3(...) PPFE_EVAL2(PPFE_EVAL2(PPFE_EVAL2(__VA_ARGS__)))
#define PPFE_EVAL4(...) PPFE_EVAL3(PPFE_EVAL3(PPFE_EVAL3(__VA_ARGS__)))
#define PPFE_EVAL(...)  PPFE_EVAL4(PPFE_EVAL4(PPFE_EVAL4(__VA_ARGS__)))
#define PPFE_MAP_END(...)
#define PPFE_MAP_OUT
#define PPFE_MAP_GET_END2() 0, PPFE_MAP_END
#define PPFE_MAP_GET_END1(...) PPFE_MAP_GET_END2
#define PPFE_MAP_GET_END(...) PPFE_MAP_GET_END1
#define PPFE_MAP_NEXT0(test, next, ...) next PPFE_MAP_OUT
#ifdef _MSC_VER
//this version doesn't work on GCC, it makes PPFE_MAP0 not get expanded the second time
//but completely unknown guy says it's required on MSVC, so I'll trust that and ifdef it
//pretty sure one of them violate the C99/C++ specifications, but I have no idea which of them (Clang seems to follow GCC)
#define PPFE_MAP_NEXT1(test, next) PPFE_EVAL0(PPFE_MAP_NEXT0(test, next, 0))
#else
#define PPFE_MAP_NEXT1(test, next) PPFE_MAP_NEXT0(test, next, 0)
#endif
#define PPFE_MAP_NEXT(test, next)  PPFE_MAP_NEXT1(PPFE_MAP_GET_END test, next)
#define PPFE_MAP0(f, x, peek, ...) f(x) PPFE_MAP_NEXT(peek, PPFE_MAP1)(f, peek, __VA_ARGS__)
#define PPFE_MAP1(f, x, peek, ...) f(x) PPFE_MAP_NEXT(peek, PPFE_MAP0)(f, peek, __VA_ARGS__)
#define PPFE_MAP0A(f, arg, x, peek, ...) f(arg, x) PPFE_MAP_NEXT(peek, PPFE_MAP1A)(f, arg, peek, __VA_ARGS__)
#define PPFE_MAP1A(f, arg, x, peek, ...) f(arg, x) PPFE_MAP_NEXT(peek, PPFE_MAP0A)(f, arg, peek, __VA_ARGS__)

//usage:
//#define STRING(x) const char * x##_string = #x;
//PPFOREACH(STRING, foo, bar, baz)
//limited to 365 entries, but that's enough.
#define PPFOREACH(f, ...)        __VA_OPT__(PPFE_EVAL(PPFE_MAP1(f,       __VA_ARGS__, ()()(), ()()(), ()()(), 0)))
// Same as the above, but the given macro takes two arguments, of which the first is 'arg' here.
#define PPFOREACH_A(f, arg, ...) __VA_OPT__(PPFE_EVAL(PPFE_MAP1A(f, arg, __VA_ARGS__, ()()(), ()()(), ()()(), 0)))



//requirements:
//- static_assert(false) throws something at compile time
//- multiple static_assert(true) works
//- does not require unique names for each assertion
//- zero traces left in the object files, except maybe debug info
//- zero warnings under any compiler
//- static_assert(2+2 < 5); works at the global scope
//- static_assert(2+2 < 5); works as a class member
//- static_assert(2+2 < 5); works inside a function
//- static_assert(2+2 < 5); works in all of the above when templates are involved
//- works on all compilers
//optional:
//- (PASS) works in a template, even if the template isn't instantiated, if the condition isn't dependent on the types
//- (FAIL) works if compiled as C; I tried to design an alternate implementation and ifdef it, but nothing works inside structs
//         closest I can find in C is a size zero or negative array, which messes up initializers,
//         or a named inner struct, but that's a 'declaration does not declare anything' warning
//- (PASS) can name assertions, if desired (only under C++11)
#ifdef __GNUC__
#define TYPENAME_IF_GCC typename // gcc requires this, msvc requires its absense
#else
#define TYPENAME_IF_GCC
#endif

#if __cplusplus < 201703
#if __cplusplus < 201103
template<bool x> struct static_assert_t;
template<> struct static_assert_t<true> { struct STATIC_ASSERTION_FAILED {}; };
template<> struct static_assert_t<false> {};
#define static_assert_c(expr, name, ...) \
	enum { \
		JOIN(static_assertion_, __COUNTER__) = \
		sizeof(TYPENAME_IF_GCC static_assert_t<(bool)(expr)>::STATIC_ASSERTION_FAILED) \
	} MAYBE_UNUSED
#else
#define static_assert_c(expr, name, ...) static_assert(expr, name)
#endif

#define static_assert_name(x, ...) #x
#define static_assert(...) static_assert_c(__VA_ARGS__, static_assert_name(__VA_ARGS__))
#endif

//almost C version (fails inside structs)
//#define static_assert(expr) \
//	typedef char JOIN(static_assertion_, __COUNTER__)[(expr)?1:-1]


#ifdef __cplusplus
class anyptr {
	void* data;
public:
	anyptr(nullptr_t) { data = NULL; }
	template<typename T> anyptr(T* data_) { data = (void*)data_; }
	template<typename T> operator T*() { return (T*)data; }
	template<typename T> operator const T*() const { return (const T*)data; }
};
#else
typedef void* anyptr;
#endif


#ifdef ARLIB_TESTRUNNER // count mallocs and frees; memory leak means test failure
void _test_malloc();
void _test_free();
void free_test(void* ptr);
#define free free_test
#else
#define _test_malloc()
#define _test_free()
#endif

// These six act as their base functions if they return non-NULL, except they return anyptr and don't need explicit casts.
// On failure, try_ returns NULL, while xmalloc kills the process.

// Arlib recommends using xmalloc. It means a malloc call can terminate the process, but that's already the case - either directly,
//  via Linux OOM killer, or indirectly, if the machine gets stuck swapping and nothing else until user pokes taskmgr or the power button.
// OOM should be handled by regularly saving all valuable to data to disk. This also protects against program crashes, power outages, etc.

// On systems without swap or overcommit, the above won't happen, and malloc return value should be checked -
//  but such systems are usually microcontrollers where malloc isn't allowed anyways, or retrocomputers that Arlib doesn't target.
// It's also difficult to test, and difficult to figure out what to do instead. For Arlib's purposes, it's not worth the effort.

void* xmalloc_inner(size_t size); // gcc optimizes void* on object file boundary better than struct, so separate function it is
inline anyptr xmalloc(size_t size) { return xmalloc_inner(size); }
inline anyptr try_malloc(size_t size) { _test_malloc(); return malloc(size); }
void* malloc(size_t) __attribute__((deprecated("use xmalloc or try_malloc instead")));

void* xrealloc_inner(void* ptr, size_t size);
inline anyptr xrealloc(void* ptr, size_t size) { return xrealloc_inner(ptr, size); }
inline anyptr try_realloc(void* ptr, size_t size) { if ((void*)ptr) _test_free(); if (size) _test_malloc(); return realloc(ptr, size); }
void* realloc(void*, size_t) __attribute__((deprecated("use xrealloc or try_realloc instead")));

void* xcalloc_inner(size_t size, size_t count);
inline anyptr xcalloc(size_t size, size_t count) { return xcalloc_inner(size, count); }
inline anyptr try_calloc(size_t size, size_t count) { _test_malloc(); return calloc(size, count); }
void* calloc(size_t, size_t) __attribute__((deprecated("use xcalloc or try_calloc instead")));


template<typename T, typename T2> forceinline T transmute(T2 in)
{
	static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_copyable_v<T2> && sizeof(T) == sizeof(T2));
	T ret;
	memcpy(&ret, &in, sizeof(ret));
	return ret;
}


template<typename T> static constexpr T min(T a) { return a; }
template<typename T, typename... Args> static constexpr T min(T a, Args... args)
{
	T b = min(args...);
	if (a < b) return a;
	else return b;
}

template<typename T> static constexpr T max(T a) { return a; }
template<typename T, typename... Args> static constexpr T max(T a, Args... args)
{
	T b = max(args...);
	if (a < b) return b;
	else return a;
}



// Inherit from nocopy or nomove if possible, so you won't need a custom constructor.
#define NO_COPY(type) \
	type(const type&) = delete; \
	const type& operator=(const type&) = delete; \
	type(type&&) = default; \
	type& operator=(type&&) = default
class nocopy {
protected:
	nocopy() = default;
	NO_COPY(nocopy);
};

#define NO_MOVE(type) \
	type(const type&) = delete; \
	const type& operator=(const type&) = delete; \
	type(type&&) = delete; \
	type& operator=(type&&) = delete
class nomove {
protected:
	nomove() = default;
	NO_MOVE(nomove);
};

// For use in, for example, std::conditional. All usage of this class should be annotated with [[no_unique_address]].
struct empty_class {};

struct end_iterator {};

#ifndef ARLIB_STANDALONE
template<typename T>
class autoptr : nocopy {
	T* ptr = nullptr;
	void reset_to(T* newptr) // don't just delete ptr, could cause trouble if T's dtor resumes a coroutines which calls operator=
	{
		T* prev = ptr;
		ptr = newptr;
		delete prev;
	}
public:
	autoptr() = default;
	autoptr(T* ptr) : ptr(ptr) {}
	autoptr(autoptr<T>&& other) { reset_to(other.release()); }
	autoptr<T>& operator=(T* ptr) { reset_to(ptr); return *this; }
	autoptr<T>& operator=(autoptr<T>&& other) { reset_to(other.release()); return *this; }
	T* release() { T* ret = ptr; ptr = nullptr; return ret; }
	T* operator->() { return ptr; }
	T& operator*() { return *ptr; }
	const T* operator->() const { return ptr; }
	const T& operator*() const { return *ptr; }
	operator T*() { return ptr; }
	operator const T*() const { return ptr; }
	explicit operator bool() const { return ptr; }
	~autoptr() { reset_to(nullptr); }
};

template<typename T>
class autofree : nocopy {
	T* ptr = NULL;
public:
	autofree() = default;
	autofree(T* ptr) : ptr(ptr) {}
	autofree(autofree<T>&& other) { ptr = other.ptr; other.ptr = NULL; }
	autofree(anyptr other) { ptr = other; }
	autofree<T>& operator=(T* ptr) { free(this->ptr); this->ptr = ptr; return *this; }
	autofree<T>& operator=(autofree<T>&& other) { free(this->ptr); ptr = other.ptr; other.ptr = NULL; return *this; }
	autofree<T>& operator=(anyptr other) { free(this->ptr); ptr = other; return *this; }
	T* release() { T* ret = ptr; ptr = NULL; return ret; }
	T* operator->() { return ptr; }
	T& operator*() { return *ptr; }
	const T* operator->() const { return ptr; }
	const T& operator*() const { return *ptr; }
	operator T*() { return ptr; }
	operator const T*() const { return ptr; }
	explicit operator bool() const { return ptr; }
	~autofree() { free(ptr); }
};

// Contains one or none of its given types. Keeps track of which member is active, if any.
// Trying to examine a nonexistent object, or create something else into a nonempty variant, is a coding error;
//  it will be caught in debug builds, but not in release.
// void is allowed, but duplicate types are not.
template<typename... Ts>
class variant {
	template<int n, typename T, typename Tf, typename... Tsi>
	static constexpr int state_for_inner()
	{
		if constexpr (std::is_same_v<T, Tf>) return n;
		else return state_for_inner<n+1, T, Tsi...>();
	}
	
	template<typename T> static constexpr int state_for() { return state_for_inner<1, T, Ts...>(); }
	
	template<typename T> static constexpr size_t my_alignof()
	{
		if constexpr (std::is_same_v<T, void>)
			return 1;
		else
			return alignof(T);
	}
	template<typename T> static constexpr size_t my_padded_sizeof()
	{
		if constexpr (std::is_same_v<T, void>)
			return 1;
		else
			return alignof(T)+sizeof(T);
	}
	
	alignas(max(my_alignof<Ts>()...)) char buf[max(my_padded_sizeof<Ts>()...)];
	char& state_id() { return buf[0]; }
	template<typename T> T* buf_for() { return (T*)(buf+alignof(T)); }
	
	
	template<typename T>
	forceinline void assert_contains()
	{
#ifndef ARLIB_OPT
		constexpr int type_idx = state_for<T>();
		if (type_idx != state_id())
			abort();
#endif
	}
	
	template<int n>
	void destruct_all()
	{
	}
	template<int n, typename T, typename... Tsi>
	void destruct_all()
	{
		if (state_id() == n)
		{
			if constexpr (!std::is_same_v<T, void>)
				buf_for<T>()->~T();
		}
		else
			destruct_all<n+1, Tsi...>();
	}
public:
	bool empty() { return state_id() == -1; }
	
	template<typename T>
	bool contains()
	{
		return state_id() == state_for<T>();
	}
	
	template<typename T>
	T& get() { assert_contains<T>(); return *buf_for<T>(); }
	
	template<typename T>
	T* try_get()
	{
		if (contains<T>())
			return buf_for<T>();
		else
			return nullptr;
	}
	
	template<typename T, typename... Tsi>
	void construct(Tsi&&... args)
	{
#ifndef ARLIB_OPT
		if (state_id() != -1)
			abort();
#endif
		state_id() = state_for<T>();
		if constexpr (!std::is_same_v<T, void>)
			new(buf_for<T>()) T(std::forward<Tsi>(args)...);
		else
			static_assert(sizeof...(Tsi) == 0);
	}
	
	template<typename T>
	void destruct()
	{
		assert_contains<T>();
		if constexpr (!std::is_same_v<T, void>)
			buf_for<T>()->~T();
		state_id() = -1;
	}
	
	// Extracts the given contents, then deletes it from this object.
	template<typename T>
	T get_destruct()
	{
		assert_contains<T>();
		T* obj = buf_for<T>();
		T ret = std::move(*obj);
		if constexpr (!std::is_same_v<T, void>)
			obj->~T();
		state_id() = -1;
		return ret;
	}
	
	// Clear out the object's contents, no matter what it is.
	void destruct_any()
	{
		destruct_all<0, Ts...>();
		state_id() = -1;
	}
	
	variant() { state_id() = -1; }
	variant(const variant&) = delete;
	variant(variant&& other)
	{
		memcpy(buf, other.buf, sizeof(buf));
		other.state_id() = -1;
	}
	variant& operator=(const variant& other) = delete;
	variant& operator=(variant&& other)
	{
		memcpy(buf, other.buf, sizeof(buf));
		other.state_id() = -1;
		return *this;
	}
	~variant()
	{
		destruct_all<0, Ts...>();
	}
};

// Alternate version of the above that uses indices, not types, as identifiers. As such, duplicates are legal.
template<typename... Ts>
class variant_idx {
	template<char idx, typename T, typename... Tsi>
	struct type_for_impl {
		using type = typename type_for_impl<idx-1, Tsi...>::type;
	};
	template<typename T, typename... Tsi>
	struct type_for_impl<0, T, Tsi...> {
		using type = T;
	};
	template<char idx> using type_for = typename type_for_impl<idx, Ts...>::type;
	
	template<typename T> static constexpr size_t my_alignof()
	{
		if constexpr (std::is_same_v<T, void>)
			return 1;
		else
			return alignof(T);
	}
	template<typename T> static constexpr size_t my_padded_sizeof()
	{
		if constexpr (std::is_same_v<T, void>)
			return 1;
		else
			return alignof(T)+sizeof(T);
	}
	
	alignas(max(my_alignof<Ts>()...)) char buf[max(my_padded_sizeof<Ts>()...)];
	char& state_id() { return buf[0]; }
	char state_id() const { return buf[0]; }
	template<typename T> T* buf_for() { return (T*)(buf+alignof(T)); }
	template<typename T> const T* buf_for() const { return (T*)(buf+alignof(T)); }
	template<char idx> type_for<idx>* buf_for_idx() { return buf_for<type_for<idx>>();; }
	template<char idx> const type_for<idx>* buf_for_idx() const { return buf_for<type_for<idx>>();; }
	
	
	template<char idx>
	forceinline void assert_contains() const
	{
#ifndef ARLIB_OPT
		if (idx != state_id())
			abort();
#endif
	}
	
	template<int n>
	void destruct_all()
	{
	}
	template<int n, typename T, typename... Tsi>
	void destruct_all()
	{
		if (state_id() == n)
		{
			if constexpr (!std::is_same_v<T, void>)
				buf_for<T>()->~T();
		}
		else
			destruct_all<n+1, Tsi...>();
	}
public:
	int type() const { return state_id(); }
	bool empty() const { return state_id() == -1; }
	
	template<char idx>
	bool contains() const
	{
		return state_id() == idx;
	}
	
	template<char idx>
	type_for<idx>& get() { assert_contains<idx>(); return *buf_for_idx<idx>(); }
	
	template<char idx>
	const type_for<idx>& get() const { assert_contains<idx>(); return *buf_for_idx<idx>(); }
	
	template<char idx>
	type_for<idx>* try_get()
	{
		if (contains<idx>())
			return buf_for_idx<idx>();
		else
			return nullptr;
	}
	
	template<char idx>
	const type_for<idx>* try_get() const
	{
		if (contains<idx>())
			return buf_for_idx<idx>();
		else
			return nullptr;
	}
	
	template<char idx, typename... Tsi>
	void construct(Tsi&&... args)
	{
#ifndef ARLIB_OPT
		if (state_id() != -1)
			abort();
#endif
		state_id() = idx;
		if constexpr (!std::is_same_v<type_for<idx>, void>)
			new(buf_for_idx<idx>()) type_for<idx>(std::forward<Tsi>(args)...);
		else
			static_assert(sizeof...(Tsi) == 0);
	}
	
	template<char idx>
	void destruct()
	{
		assert_contains<idx>();
		if constexpr (!std::is_same_v<type_for<idx>, void>)
			buf_for_idx<idx>()->~type_for<idx>();
		state_id() = -1;
	}
	
	// Extracts the given contents, then deletes it from this object.
	template<char idx>
	type_for<idx> get_destruct()
	{
		assert_contains<idx>();
		type_for<idx>* obj = buf_for_idx<idx>();
		type_for<idx> ret = std::move(*obj);
		obj->~type_for<idx>();
		state_id() = -1;
		return ret;
	}
	
	// Clear out the object's contents, no matter what it is.
	void destruct_any()
	{
		destruct_all<0, Ts...>();
		state_id() = -1;
	}
	
	variant_idx() { state_id() = -1; }
	variant_idx(const variant_idx&) = delete;
	variant_idx(variant_idx&& other)
	{
		memcpy(buf, other.buf, sizeof(buf));
		other.state_id() = -1;
	}
	variant_idx& operator=(const variant_idx& other) = delete;
	variant_idx& operator=(variant_idx&& other)
	{
		memcpy(buf, other.buf, sizeof(buf));
		other.state_id() = -1;
		return *this;
	}
	~variant_idx()
	{
		destruct_all<0, Ts...>();
	}
};

// Like the above, but does not track its content type; caller has to do that.
// Can be called storage_for if used with exactly one template argument.
template<typename... Ts>
class variant_raw {
	template<typename T>
	static constexpr bool can_contain() { return (std::is_same_v<T, Ts> || ...); }
	
	alignas(max(alignof(Ts)...)) char buf[max(sizeof(Ts)...)];
public:
	template<typename T>
	T* get_ptr() { static_assert(can_contain<T>()); return (T*)buf; }
	template<typename T>
	T& get() { return *get_ptr<T>(); }
	
	template<typename T, typename... Tsi>
	T* construct(Tsi&&... args)
	{
		T* ret = get_ptr<T>();
		new(ret) T(std::forward<Tsi>(args)...);
		return ret;
	}
	template<typename T>
	void destruct() { get_ptr<T>()->~T(); }
	template<typename T>
	T get_destruct()
	{
		T& obj = get<T>();
		T ret = std::move(obj);
		obj.~T();
		return ret;
	}
};

template<typename T>
class refcount {
	struct inner_t {
		T item; // item first so operator T* returns 0 and not 4 if null, it optimizes better
		uint32_t count;
	};
	inner_t* inner;
	
public:
	refcount() : inner(NULL) {}
	template<typename... Ts> void init(Ts... args) { if (inner) abort(); inner = new inner_t({ { args... }, 1 }); }
	refcount(const refcount<T>& other) { inner = other.inner; if (inner) inner->count++; }
	refcount(refcount<T>&& other) { inner = other.inner; other.inner = NULL; }
	refcount<T>& operator=(T* ptr) = delete;
	refcount<T>& operator=(autofree<T>&& other) = delete;
	refcount<T>& operator=(refcount<T>&& other) { inner = other.inner; other.inner = NULL; return *this; }
	T* operator->() { return inner ? &inner->item : NULL; }
	T& operator*() { return inner->item; }
	const T* operator->() const { return inner ? &inner->item : NULL; }
	const T& operator*() const { return inner->item; }
	operator T*() { return inner ? &inner->item : NULL; }
	operator const T*() const { return inner ? &inner->item : NULL; }
	bool exists() const { return inner; }
	bool unique() const { return inner->count == 1; }
	~refcount() { if (inner && --inner->count == 0) delete inner; }
};



//if an object should contain callbacks that can destroy the object, you should use the macros below these classes
class destructible {
	friend class destructible_lock;
	bool* pb = NULL;
public:
	~destructible() { if (pb) *pb = true; }
};
class destructible_lock {
	bool b = false;
	destructible* parent;
	bool* prev_pb;
public:
	destructible_lock(destructible& other)
	{
		parent = &other;
		prev_pb = parent->pb;
		parent->pb = &b;
	}
	bool destroyed() { return b; }
	~destructible_lock()
	{
		if (!b) parent->pb = prev_pb;
		else if (prev_pb) *prev_pb = true; // if 'b' is true, 'parent' is dangling
	}
};
#define MAKE_DESTRUCTIBLE_FROM_CALLBACK() destructible destructible_i
#define RETURN_IF_CALLBACK_DESTRUCTS(op, ...) \
	do { \
		destructible_lock destructible_lock_i(this->destructible_i); \
		{ op; } \
		if (destructible_lock_i.destroyed()) \
			return __VA_ARGS__; \
	} while(0)
//why? easier than doing it manually, and supports exceptions
//(hopefully c++20 coroutines will replace most usecases for this one, they're easy to forget)




#if defined(_WIN32) || defined(runtime__SSE4_2__) // Windows doesn't have memmem; Linux does, but it's rare and poorly optimized
void* memmem_arlib(const void * haystack, size_t haystacklen, const void * needle, size_t needlelen) __attribute__((pure));
#define memmem memmem_arlib
#endif
#endif



template<typename T> inline T memxor_t(const uint8_t * a, const uint8_t * b)
{
	T an; memcpy(&an, a, sizeof(T));
	T bn; memcpy(&bn, b, sizeof(T));
	return an ^ bn;
}
// memeq is small after optimization, but looks big to the inliner, so forceinline it
// if caller doesn't know size, caller should also be forceinlined too
forceinline bool memeq(const void * a, const void * b, size_t len)
{
#ifdef __GNUC__
	if (!__builtin_constant_p(len))
		return !memcmp(a, b, len);
#endif
	
#if defined(__i386__) || defined(__x86_64__)
	// several ideas borrowed from clang !memcmp(variable, constant, constant) output
	// its codegen is often better than anything I could think of
	
	const uint8_t * a8 = (uint8_t*)a;
	const uint8_t * b8 = (uint8_t*)b;
	
	if (len == 0)
		return true;
	
	if (len == 1)
		return *a8 == *b8;
	
	if (len == 2) return memxor_t<uint16_t>(a8, b8) == 0;
	if (len == 3) return (memxor_t<uint16_t>(a8, b8) | (a8[2]^b8[2])) == 0;
	
	if (len == 4) return memxor_t<uint32_t>(a8, b8) == 0;
	if (len == 5) return (memxor_t<uint32_t>(a8, b8) | (a8[4]^b8[4])) == 0;
	if (len == 6) return (memxor_t<uint32_t>(a8, b8) | memxor_t<uint16_t>(a8+4, b8+4)) == 0;
	if (len <  8) return (memxor_t<uint32_t>(a8, b8) | memxor_t<uint32_t>(a8+len-4, b8+len-4)) == 0;
	
	if (len == 8)  return memxor_t<uint64_t>(a8, b8) == 0;
	if (len == 9)  return (memxor_t<uint64_t>(a8, b8) | (a8[8]^b8[8])) == 0;
	if (len == 10) return (memxor_t<uint64_t>(a8, b8) | memxor_t<uint16_t>(a8+8, b8+8)) == 0;
	if (len <= 12) return (memxor_t<uint64_t>(a8, b8) | memxor_t<uint32_t>(a8+len-4, b8+len-4)) == 0;
	if (len <= 16) return (memxor_t<uint64_t>(a8, b8) | memxor_t<uint64_t>(a8+len-8, b8+len-8)) == 0;
	
	// no point doing an SSE version, large constant sizes are rare
#else
#warning enable the above on platforms where unaligned mem access is fast
#endif
	
	return !memcmp(a, b, len);
}


// updates dest/src; does NOT update count, since it's just zero on exit
// must behave properly both if dest/src are nonoverlapping, if src is slightly before dest,
//  and if src is equal to or slightly after dest
forceinline void rep_movsb(uint8_t * & dest, const uint8_t * & src, size_t count)
{
#if defined(__i386__) || defined(__x86_64__)
	// rep movsb is slow for large buffers if not ERMS (intel 2013, amd 2020), and small if not FSRM (intel 2009, amd 2020)
	// however, it always gives correct results in "reasonable" speed, so no point adding conditionals
	const uint8_t * rsi = src;
	uint8_t * rdi = dest;
	__asm__("rep movsb" : "+S"(rsi), "+D"(rdi), "+c"(count)
#ifdef __clang__
	                    : : "memory" // https://bugs.llvm.org/show_bug.cgi?id=47866
#else
	                    , "+m"(*(uint8_t(*)[])rdi) : "m"(*(const uint8_t(*)[])rsi)
#endif
	);
	src = rsi;
	dest = rdi;
#elif defined(_MSC_VER)
#error test
	__movsb(dest, src, count);
	dest += count;
	src += count;
#else
	if (count & 2) { *dest++ = *src++; *dest++ = *src++; }
	if (count & 1) { *dest++ = *src++; }
	
	count >>= 2;
	while (count--)
	{
		*dest++ = *src++;
		*dest++ = *src++;
		*dest++ = *src++;
		*dest++ = *src++;
	}
#endif
}


//undefined behavior if 'in' is negative, or if the output would be out of range (signed input types are fine, but reduce valid input range)
//returns 1 if input is 0
template<typename T> static inline T bitround(T in)
{
#if defined(__GNUC__)
	static_assert(sizeof(unsigned) == 4); // __builtin_clzl(5) is 29 on modern windows and 61 on modern linux. disgusting
	static_assert(sizeof(unsigned long long) == 8); // at least these two have constant size on every modern platform... for now...
	
	if (in <= 1) return 1;
	if (sizeof(T) <= 4) return 2u << (__builtin_clz(in - 1) ^ 31); // clz on x86 is bsr^31, so this compiles to bsr^31^31,
	if (sizeof(T) == 8) return 2ull<<(__builtin_clzll(in-1) ^ 63); // which optimizes better than the more obvious 1<<(32-clz) bit_ceil
	
	abort();
#else
	// MSVC has a _BitScanReverse, could use that
	
	in -= (bool)in; // so bitround(0) becomes 1, rather than integer underflow and back to 0
	in |= in >> 1;
	in |= in >> 2;
	in |= in >> 4;
	if constexpr (sizeof(in) > 1) in |= in >> 8; // silly ifs to avoid 'shift amount out of range' warnings
	if constexpr (sizeof(in) > 2) in |= in >> 16;
	if constexpr (sizeof(in) > 4) in |= in >> 32;
	in++;
	return in;
#endif
}

//undefined behavior if 'in' is negative or zero; to make zero return 0, add |1 to input
template<typename T> static inline uint8_t ilog2(T in)
{
#if defined(__GNUC__)
	static_assert(sizeof(unsigned) == 4);
	static_assert(sizeof(unsigned long long) == 8);
	
	if (sizeof(T) <= 4) return __builtin_clz(in) ^ 31;
	if (sizeof(T) == 8) return __builtin_clzll(in)^63;
	
	abort();
#else
	in |= in >> 1; // first round down[sic] to one less than a power of 2 
	in |= in >> 2;
	in |= in >> 4;
	if constexpr (sizeof(in) > 1) in |= in >> 8;
	if constexpr (sizeof(in) > 2) in |= in >> 16;
	if constexpr (sizeof(in) > 4) in |= in >> 32;
	
	// this probably duplicates the tables in the binary if called with multiple types or from multiple compilation units... don't care...
	static const uint8_t MultiplyDeBruijnBitPosition32[32] = 
	{
	  0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
	  8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
	};
	static const uint8_t MultiplyDeBruijnBitPosition64[64] = 
	{
	  0, 11, 1, 12, 16, 29, 2, 13, 22, 17, 41, 25, 30, 48, 3, 61,
	  14, 20, 23, 18, 34, 36, 42, 26, 38, 31, 53, 44, 49, 56, 4, 62,
	  10, 15, 28, 21, 40, 24, 47, 60, 19, 33, 35, 37, 52, 43, 55, 9,
	  27, 39, 46, 59, 32, 51, 54, 8, 45, 58, 50, 7, 57, 6, 5, 63,
	};
	
	// there are 1024 possible 32bit constants here (each with a different bit position table)
	// all start with bits 0000 0111 110, 0111 1000 001, 1000 0111 110 or 1111 1000 001, and all are odd
	// in 64bit, there are 4194304 each starting with 0000 0011 1111 0, 0111 1100 0000 1, 1000 0011 1111 0 or 1111 1100 0000 1, still odd
	// the chosen two are the numerically lowest
	if (sizeof(T) <= 4)
		return MultiplyDeBruijnBitPosition32[((uint32_t)in * 0x07C4ACDD) >> 27];
	if (sizeof(T) == 8) // this is probably slower than an if+shift and a 32bit mul, will fix if I find a compiler where it's relevant
		return MultiplyDeBruijnBitPosition64[((uint64_t)in * 0x03F08A4C6ACB9DBD) >> 58];
	
	abort();
#endif
}

//undefined behavior if 'in' is negative or zero; to make zero return 0, add |1 to input
template<typename T> static inline uint8_t ilog10(T in);
class ilog10_tab // implementation detail, class ensures the binary doesn't contain multiple copies of these tables
{
	template<typename T> friend uint8_t ilog10(T in);
	static constexpr const uint64_t thresholds[] = {
		( 1ll<<56) - (10>>0),
		( 2ll<<56) - (100>>1),
		( 3ll<<56) - (1000>>2),
		( 4ll<<56) - (10000>>3),
		( 5ll<<56) - (100000>>4),
		( 6ll<<56) - (1000000>>5),
		( 7ll<<56) - (10000000>>6),
		( 8ll<<56) - (100000000>>7),
		( 9ll<<56) - (1000000000>>8),
		(10ll<<56) - (10000000000>>9),
		(11ll<<56) - (100000000000>>10),
		(12ll<<56) - (1000000000000>>11),
		(13ll<<56) - (10000000000000>>12),
		(14ll<<56) - (100000000000000>>13),
		(15ll<<56) - (1000000000000000>>14),
		(16ll<<56) - (10000000000000000>>15),
		(17ll<<56) - (100000000000000000>>16),
		(18ll<<56) - (1000000000000000000>>17),
		(19ll<<56) - (10000000000000000000u>>18),
	};
	static constexpr const uint32_t thresholds32[] = { // 64bit math is annoying on 32bit
		(1<<24) - (10>>0),
		(2<<24) - (100>>1),
		(3<<24) - (1000>>2),
		(4<<24) - (10000>>3),
		(5<<24) - (100000>>4),
		(6<<24) - (1000000>>5),
		(7<<24) - (10000000>>6),
		(8<<24) - (100000000>>7),
		(9<<24) - (1000000000>>8),
	};
};
template<typename T> static inline uint8_t ilog10(T in)
{
	// implementation based on ideas from https://lemire.me/blog/2021/06/03/computing-the-number-of-digits-of-an-integer-even-faster/
	
	// *9/32 and *19/64 are approximations of /log2(10); rather poor approximations, but good enough here
	// the only requirement is that, for each ilog2 output, it returns
	//  0,0,0,0,+0,+0,1,+1,+1,2,+2,+2,+2,3,+3,+3,4,+4,+4,5,+5,+5,+5,6,+6,+6,7,+7,+7,8,8,8,
	//  +8,9,+9,+9,10,+10,+10,11,+11,+11,+11,12,+12,+12,13,+13,+13,14,+14,+14,+14,15,+15,+15,16,+16,+16,17,+17,+17,+17,18
	// where + prefix means it can be either that value, or 1 more; for example, for input 4, both 0 and 1 are fine
	// (the two middle 8s can be considered to have + on 64bit, but not on 32bit)
	// *19/64 works for every size <= 8, but *9 optimizes better than *19, so sizeof conditional it is
	size_t digits = (sizeof(T)<=4 ? ilog2(in)*9/32 : ilog2(in)*19/64);
	
	// the extra >>digits on in ensures the top table entries don't collide with the return value
	// the bottom bits of in don't affect the output anyways
	if (sizeof(size_t) <= 4 && sizeof(T) <= 4)
		return (ilog10_tab::thresholds32[digits] + (in>>digits)) >> 24;
	else
		return (ilog10_tab::thresholds[digits] + (in>>digits)) >> 56;
}

#define ALLINTS(x) \
	x(signed char) \
	x(unsigned char) \
	x(signed short) \
	x(unsigned short) \
	x(signed int) \
	x(unsigned int) \
	x(signed long) \
	x(unsigned long) \
	x(signed long long) \
	x(unsigned long long)

#ifndef COMMON_INST
#define COMMON_INST(T) extern template class T
#endif

//For cases where gcc thinks a variable is used uninitialized, but it isn't in practice.
//Usage: int foo KNOWN_INIT(0)
#define KNOWN_INIT(x) = x

//Attach this attribute to the tail loop after a SIMD loop, so the compiler won't try to vectorize something with max 4 iterations.
//(Neither compiler seems to acknowledge 'don't unroll' as 'don't vectorize', and gcc doesn't have a 'don't vectorize' at all.)
#ifdef __clang__
#define SIMD_LOOP_TAIL _Pragma("clang loop unroll(disable) vectorize(disable)")
#elif __GNUC__ >= 8
#define SIMD_LOOP_TAIL _Pragma("GCC unroll 0")
#else
#define SIMD_LOOP_TAIL // nop
#endif

#ifndef ARLIB_STANDALONE
#ifdef __GNUC__
#define oninit() __attribute__((constructor)) static void JOIN(oninit,__LINE__)()
#define oninit_early() __attribute__((constructor(101))) static void JOIN(oninit,__LINE__)()
#define ondeinit() __attribute__((destructor)) static void JOIN(ondeinit,__LINE__)()
#else
class initrunner {
public:
	initrunner(void(*fn)()) { fn(); }
};
#define oninit() static void JOIN(oninit,__LINE__)(); \
                 static MAYBE_UNUSED initrunner JOIN(initrun,__LINE__)(JOIN(oninit,__LINE__)); \
                 static void JOIN(oninit,__LINE__)()
template<void(*fn)()>
class deinitrunner {
public:
	~deinitrunner() { fn(); }
};
#define ondeinit() static void JOIN(ondeinit,__LINE__)(); \
                   static MAYBE_UNUSED deinitrunner<JOIN(ondeinit,__LINE__)> JOIN(deinitrun,__LINE__); \
                   static void JOIN(ondeinit,__LINE__)()
#endif

// oninit_static is like oninit, but promises to only write directly to global variables, no malloc.
// In exchange, it is usable in hybrid DLLs. On non-hybrid or non-Windows, it's same as normal oninit.
// The only way to affect ordering is _early. Static early runs before both nonearly; early nonstatic runs before normal.
// Other than that, no guarantees; there are no guarantees on order within a class, between static and nonstatic,
// between static and early nonstatic, or between global objects and oninit.
#ifdef ARLIB_HYBRID_DLL
#define oninit_static_section(sec) static void JOIN(oninit,__LINE__)(); \
                        static const funcptr JOIN(initrun,__LINE__) \
                            __attribute__((section(".ctors.arlibstatic" sec), used)) = JOIN(oninit,__LINE__); \
                        static void JOIN(oninit,__LINE__)()
#define oninit_static() oninit_static_section("2")
#define oninit_static_early() oninit_static_section("3") // it's processed backwards, high-numbered sections run first
#else
#define oninit_static oninit
#define oninit_static_early oninit_early
#endif
#endif

// inspired by the Linux kernel macro, but using a member pointer looks cleaner; non-expressions (like member names) in macros look wrong
template<typename Tc, typename Ti>
Tc* container_of(Ti* ptr, Ti Tc:: * memb)
{
	// https://wg21.link/P0908 proposes a better implementation, but it was forgotten and not accepted
	Tc* fake_object = (Tc*)0x12345678;  // doing math on a fake pointer is UB, but good luck proving it's bogus
	fake_object = launder(fake_object); // especially across an asm (both gcc and clang will optimize out the fake pointer)
	size_t offset = (uintptr_t)&(fake_object->*memb) - (uintptr_t)fake_object;
	return (Tc*)((uint8_t*)ptr - offset);
}
template<typename Tc, typename Ti>
const Tc* container_of(const Ti* ptr, Ti Tc:: * memb)
{
	return container_of<Tc, Ti>((Ti*)ptr, memb);
}
template<auto memb, typename Ti>
auto container_of(Ti* ptr) { return container_of(ptr, memb); }

class range_iter_t {
	size_t n;
	size_t step;
public:
	range_iter_t(size_t n, size_t step) : n(n), step(step) {}
	forceinline bool operator!=(const range_iter_t& other) { return n < other.n; }
	forceinline void operator++() { n += step; }
	forceinline size_t operator*() { return n; }
};
class range_t {
	size_t start;
	size_t stop;
	size_t step;
public:
	range_t(size_t start, size_t stop, size_t step) : start(start), stop(stop), step(step) {}
	range_iter_t begin() const { return { start, step }; }
	range_iter_t end() const { return { stop, 0 }; }
};
static inline range_t range(size_t stop) { return range_t(0, stop, 1); }
static inline range_t range(size_t start, size_t stop, size_t step = 1) { return range_t(start, stop, step); }

#ifdef __MINGW32__
// force these to be imported from msvcrt, not mingwex, to save a bunch of kilobytes
#define _GLIBCXX_MATH_H 1 // disable any subsequent #include <math.h>, it's full of using std::sin that conflicts with my overloads
#include <cmath>

#ifdef __i386__
// for some reason, the ##f functions don't exist on i386. I suspect it's somehow related to x87 automatic long double.
#define MATH_FN(name) \
	extern "C" __attribute__((dllimport)) double name(double); \
	static inline float name##f(float a) { return name((double)a); } \
	static inline float name(float a) { return name##f(a); }
#define MATH_FN_2(name) \
	extern "C" __attribute__((dllimport)) double name(double,double); \
	static inline float name##f(float a, float b) { return name((double)a, (double)b); } \
	static inline float name(float a, float b) { return name##f(a, b); }
// only way to get rid of the extern float __cdecl sinf(float) from the headers - they don't exist in msvcrt
#define sinf   dummy_sinf
#define cosf   dummy_cosf
#define expf   dummy_expf
#define logf   dummy_logf
#define log10f dummy_log10f
#define powf   dummy_powf
#define sqrtf  dummy_sqrtf
#define ceilf  dummy_ceilf
#define floorf dummy_floorf
#else
#define MATH_FN(name) \
	extern "C" __attribute__((dllimport)) double name(double); \
	extern "C" __attribute__((dllimport)) float name##f(float); \
	static inline float name(float a) { return name##f(a); }
#define MATH_FN_2(name) \
	extern "C" __attribute__((dllimport)) double name(double,double); \
	extern "C" __attribute__((dllimport)) float name##f(float,float); \
	static inline float name(float a, float b) { return name##f(a, b); }
#endif

MATH_FN(sin) MATH_FN(cos)
MATH_FN(exp) MATH_FN(log) MATH_FN(log10)
MATH_FN_2(pow) MATH_FN(sqrt)
MATH_FN(ceil) MATH_FN(floor)

using std::isinf;
using std::isnan;
using std::isnormal;
using std::signbit;
#endif

// WARNING: Hybrid EXE/DLL is not supported by Windows. If it's ran as an EXE, it's a perfectly normal EXE, other than
//  its nonempty exports section; however, if used as a DLL, it has to reimplement some OS facilities, yielding several limitations:
// - It's not supported by the OS; it relies on a bunch of implementation details and ugly tricks that may break in newer OSes.
// - The program must call arlib_hybrid_dll_init() at the top of every DLLEXPORTed function.
//     It must be at the very top; constructors and exception handling setup are not permitted. (All calling conventions are safe.)
//     It's safe to call it multiple times, including multithreaded.
//     It's also safe to call it if it's ran as an EXE, and on OSes other than Windows. It'll just do nothing.
//     Only the first call to arlib_hybrid_dll_init() does anything, so it's safe to omit it in
//       exported functions guaranteed to not be the first one called.
// - ARLIB_THREAD must be enabled, even if you don't use any global nonconstants or other threading operations;
//     arlib_hybrid_dll_init() itself uses globals. (Exception: If your API includes a single-thread-only global init function,
//     you may omit ARLIB_THREAD.)
// - Global variables won't necessarily have the right value until arlib_hybrid_dll_init() is called.
//     (Exported non-functions are rare anyways, so no big deal for most APIs.)
// - The EXE/DLL startup code does a lot of stuff I don't understand, or never investigated; I probably forgot something important.
// - DllMain is completely ignored and never called. It won't even be referenced by anything, it will be garbage colleted.
// - Global objects' destructors are often implemented via atexit(), which is process-global in EXEs, and in things GCC thinks is an EXE.
//     As such, globals containing dynamic allocations are memory leaks at best; DLL paths may not touch them.
//     (Statically allocated globals are fine. For example, BearSSL caches the system certificate store into a global buffer.)
//     Note that parts of Arlib use global constructors or global variables. In particular, the following are memory leaks or worse:
//     - runloop::global()
//     - socket::create_ssl() SChannel backend (BearSSL is safe; OpenSSL is not supported on Windows)
//     - window_*
//     - mutex as a global variable, if ARXPSUPPORT (safe if class member, if XP support is disabled, or both)
//     - semaphore as a global variable
//     - WuTF (not supported in DLLs at all)
// - To avoid crashes if atexit() calls an unloaded DLL, and to allow globals in EXE paths, globals' constructors are not run either.
//     Constant-initialized variables are safe, or if you need to (e.g.) put your process ID in a global, you can use oninit_static().
// - Some libc functions and compiler intrinsics that are implemented via global variables.
//     Most of them are safe; msvcrt is a normal DLL, and most things MinGW statically links are stateless.
//     The only useful exception I'm aware of, __builtin_cpu_supports, is deprecated in Arlib anyways, for unrelated reasons.
// - DLLs used by this program will never be unloaded. If your program only links against system libraries, this is fine,
//     something else is probably already using them; if your program ships its own DLLs (for example libstdc++-6.dll),
//     this is equivalent to a memory leak. (But if you're shipping DLLs, your program is already multiple files, and you
//     should put all shareable logic in another DLL and use a normal EXE.)
//     A hybrid DLL calling LoadLibrary is safe, as long as it also calls FreeLibrary.
// - If a normal DLL imports a symbol that doesn't exist from another DLL, LoadLibrary's caller gets an error.
//     If a hybrid DLL imports a symbol that doesn't exist, it will remain as NULL, and will crash if called. You can't even
//     NULL check them, compiler will optimize it out. If you need that feature, use LoadLibrary or an appropriate wrapper.
// - Compiler-supported thread local storage in DLLs paths is unlikely to work. (TlsAlloc is fine.)
// - It uses a couple of GCC extensions, with no MSVC equivalent. (Though the rest of Arlib isn't tested under MSVC either.)
// - Not tested outside i386 and x86_64, and likely to misbehave arbitrarily.
// - It does various weird things; antivirus programs may react.
// On Linux, none of the above applies; it's a perfectly normal program in both respects.
// Depending on what the EXE path does, it may end up loading itself as a DLL.
//  This is safe, on both Linux and Windows, unless both EXE and DLL paths use global variables in a dubious way.
#ifdef ARLIB_HYBRID_DLL
void arlib_hybrid_dll_init();
#else
static inline void arlib_hybrid_dll_init() {}
#endif

// An implementation may, at its sole discretion, choose to define any implementation of undefined
// behaviour, including reasonable ones. The user may, of course, not rely on that.

// This file, and many other parts of Arlib, uses a weird mix between Windows- and Linux-style
// filenames and paths. This is intentional; the author prefers Linux-style paths and directory
// structures, but Windows file extensions. .exe is less ambigous than no extension, and 'so' is a
// word while 'dll' is not; however, Windows' insistence on overloading the escape character is
// irritating. Since this excludes following any single OS, the rest is personal preference.

// Documentation is mandatory: if any question about the object's usage is not answered by reading
// the header, there's a bug (either more docs are needed, or the thing is badly designed). However,
// documentation includes the function and parameter names, not just comments; there is only one
// plausible behavior for cstring::length(), so additional comments would just be noise.
// https://i.redd.it/3adwp98dswi21.jpg
