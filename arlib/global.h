#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE //strdup, realpath, asprintf
#endif
#define _strdup strdup //and windows is being windows as usual

//these shouldn't be needed with modern compilers, according to
//https://stackoverflow.com/questions/8132399/how-to-printf-uint64-t-fails-with-spurious-trailing-in-format
//but mingw 8.1.0 needs it anyways for whatever reason
#define __STDC_LIMIT_MACROS 1
#define __STDC_FORMAT_MACROS 1
#define __STDC_CONSTANT_MACROS 1 // why are they so many?
#define _USE_MATH_DEFINES // needed for M_PI on windows

#ifdef _WIN32
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT _WIN32_WINNT_WIN7
#    define NTDDI_VERSION NTDDI_WIN7
#    define _WIN32_IE _WIN32_IE_WIN7
#  elif _WIN32_WINNT <= 0x0600 // don't replace with _WIN32_WINNT_LONGHORN, windows.h isn't included yet
#    undef _WIN32_WINNT
#    define _WIN32_WINNT _WIN32_WINNT_WS03 // _WIN32_WINNT_WINXP excludes SetDllDirectory, so I need to put it at 0x0502
#    define NTDDI_VERSION NTDDI_WS03 // actually NTDDI_WINXPSP2, but mingw sddkddkver.h gets angry about that
#    define _WIN32_IE _WIN32_IE_IE60SP2
#  endif
#  define WIN32_LEAN_AND_MEAN
#  define strcasecmp _stricmp
#  define strncasecmp _strnicmp
#  ifdef _MSC_VER
#    define _CRT_NONSTDC_NO_DEPRECATE
#    define _CRT_SECURE_NO_WARNINGS
#  endif
#  ifdef __MINGW32__
// mingw *really* wants to define its own printf/scanf, which adds ~20KB random stuff to the binary
// (on some 32bit mingw versions, it also adds a dependency on libgcc_s_sjlj-1.dll)
// extra kilobytes and dlls is the opposite of what I want, and my want is stronger, so here's some shenanigans
// comments say libstdc++ demands a POSIX printf, but I don't use libstdc++'s text functions, so I don't care
// msvcrt strtod also rounds wrong sometimes, but it's right for all plausible inputs, so I don't care about that either
#    define __USE_MINGW_ANSI_STDIO 0 // first, trigger a warning if it's enabled already - probably wrong include order
#    include <stdlib.h>              // second, include this specific header; it includes <bits/c++config.h>,
#    undef __USE_MINGW_ANSI_STDIO    // which sets this flag, which must be turned off
#    define __USE_MINGW_ANSI_STDIO 0 // (subsequent includes of c++config.h are harmless, there's an include guard)
#    undef strtof
#    define strtof strtof_arlib // third, redefine these functions, they pull in mingw's scanf
#    undef strtod               // this is why stdlib.h is chosen, rather than some random tiny c++ header like cstdbool
#    define strtod strtod_arlib // (strtod not acting like scanf is creepy, anyways)
#    undef strtold
#    define strtold strtold_arlib
float strtof_arlib(const char * str, char** str_end);
double strtod_arlib(const char * str, char** str_end);
long double strtold_arlib(const char * str, char** str_end);
#  endif
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <inttypes.h>
#include <utility>
#include "function.h"
#include "cpu.h"

#define rand use_g_rand_instead
#define srand g_rand_doesnt_need_this

#ifdef STDOUT_DELETE
#define puts(x) do{}while(0)
#define printf(x, ...) do{}while(0)
#endif

#ifdef STDOUT_ERROR
#define puts(x) cant_use_stdout_without_a_console
#define printf(x, ...) cant_use_stdout_without_a_console
#endif

#ifdef _MSC_VER
#pragma warning(disable:4800) // forcing value to bool 'true' or 'false' (performance warning)
#endif

#ifndef __has_include
#define __has_include(x) false
#endif

typedef void(*funcptr)();

#define using(obj) if(obj;true)
template<typename T> class defer_holder {
	T fn;
	bool doit;
public:
	defer_holder(T fn) : fn(std::move(fn)), doit(true) {}
	defer_holder(const defer_holder&) = delete;
	defer_holder(defer_holder&& other) : fn(std::move(other.fn)), doit(true) { other.doit = false; }
	~defer_holder() { if (doit) fn(); }
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
#define __GNUC__ 0
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

//some magic stolen from http://blogs.msdn.com/b/the1/archive/2004/05/07/128242.aspx
//C++ can be so messy sometimes...
template<typename T, size_t N> char(&ARRAY_SIZE_CORE(T(&x)[N]))[N];
template<typename T> typename std::enable_if<sizeof(T)==0, T&>::type ARRAY_SIZE_CORE(T& x); // size-zero arrays are a special case
#define ARRAY_SIZE(x) (sizeof(ARRAY_SIZE_CORE(x)))

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
#define PPFOREACH(f, ...)        PPFE_EVAL(PPFE_MAP1(f,       __VA_ARGS__, ()()(), ()()(), ()()(), 0))
// Same as the above, but the given macro takes two arguments, of which the first is 'arg' here.
#define PPFOREACH_A(f, arg, ...) PPFE_EVAL(PPFE_MAP1A(f, arg, __VA_ARGS__, ()()(), ()()(), ()()(), 0))



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
//- (FAIL) works if compiled as C; tried to design an alternate implementation and ifdef it, but nothing works inside structs
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



#ifdef __GNUC__
#define ALIGN(n) __attribute__((aligned(n)))
#endif
#ifdef _MSC_VER
#define ALIGN(n) __declspec(align(n))
#endif




#ifdef __cplusplus
class anyptr {
	void* data;
public:
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

#ifdef runtime__SSE2__
#include <mm_malloc.h> // contains an inline function that calls malloc
#endif

// These six act as their base functions if they return non-NULL, except they return anyptr and don't need explicit casts.
// On NULL, try_ returns NULL, while xmalloc kills the process.

// Arlib recommends using xmalloc. It means a malloc call can terminate the process, but that's already the case - either directly,
//  via Linux OOM killer, or indirectly, via the machine being bogged down with infinite swap until user reboots it.
// OOM should be handled by regularly saving all valuable to data to disk. This also protects against program crashes, power outage, etc.

// On systems without swap or overcommit, malloc return value should of course be checked - but such systems are usually
//  microcontrollers where malloc isn't allowed anyways, or retrocomputers that Arlib doesn't target.

anyptr xmalloc(size_t size);
inline anyptr try_malloc(size_t size) { _test_malloc(); return malloc(size); }
#define malloc(x) use_xmalloc_or_try_malloc_instead(x)

anyptr xrealloc(anyptr ptr, size_t size);
inline anyptr try_realloc(anyptr ptr, size_t size) { if ((void*)ptr) _test_free(); if (size) _test_malloc(); return realloc(ptr, size); }
#define realloc(x,y) use_xrealloc_or_try_realloc_instead(x,y)

anyptr xcalloc(size_t size, size_t count);
inline anyptr try_calloc(size_t size, size_t count) { _test_malloc(); return calloc(size, count); }
#define calloc(x,y) use_xcalloc_or_try_calloc_instead(x,y)


//cast to void should be enough to shut up warn_unused_result, but...
template<typename T> static inline void ignore(T t) {}


template<typename T> static T min(T a) { return a; }
template<typename T, typename... Args> static T min(T a, Args... args)
{
	T b = min(args...);
	if (a < b) return a;
	else return b;
}

template<typename T> static T max(T a) { return a; }
template<typename T, typename... Args> static T max(T a, Args... args)
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

template<typename T>
class autoptr : nocopy {
	T* ptr = NULL;
public:
	autoptr() = default;
	autoptr(T* ptr) : ptr(ptr) {}
	autoptr(autoptr<T>&& other) { ptr = other.ptr; other.ptr = NULL; }
	autoptr<T>& operator=(T* ptr) { delete this->ptr; this->ptr = ptr; return *this; }
	autoptr<T>& operator=(autoptr<T>&& other) { delete this->ptr; ptr = other.ptr; other.ptr = NULL; return *this; }
	T* release() { T* ret = ptr; ptr = NULL; return ret; }
	T* operator->() { return ptr; }
	T& operator*() { return *ptr; }
	const T* operator->() const { return ptr; }
	const T& operator*() const { return *ptr; }
	operator T*() { return ptr; }
	operator const T*() const { return ptr; }
	explicit operator bool() const { return ptr; }
	~autoptr() { delete ptr; }
};

template<typename T>
class autofree : nocopy {
	T* ptr = NULL;
public:
	autofree() = default;
	autofree(T* ptr) : ptr(ptr) {}
	autofree(autofree<T>&& other) { ptr = other.ptr; other.ptr = NULL; }
	autofree<T>& operator=(T* ptr) { free(this->ptr); this->ptr = ptr; return *this; }
	autofree<T>& operator=(autofree<T>&& other) { free(this->ptr); ptr = other.ptr; other.ptr = NULL; return *this; }
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

template<typename T>
class refcount {
	struct inner_t {
		T item; // item first so operator T* returns 0 and not 4 if null
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
	T* operator->() { return &inner->item; }
	T& operator*() { return inner->item; }
	const T* operator->() const { return &inner->item; }
	const T& operator*() const { return inner->item; }
	operator T*() { return &inner->item; }
	operator const T*() const { return &inner->item; }
	bool exists() const { return inner; }
	bool unique() const { return inner->count == 1; }
	~refcount() { if (inner && --inner->count == 0) delete inner; }
};

template<typename T>
class iterwrap {
	T b;
	T e;
	
public:
	iterwrap(T b, T e) : b(b), e(e) {}
	template<typename T2> iterwrap(T2& c) : b(c.begin()), e(c.end()) {}
	template<typename T2> iterwrap(const T2& c) : b(c.begin()), e(c.end()) {}
	
	T begin() { return b; }
	T end() { return e; }
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



template<typename T> inline T memxor_t(const uint8_t * a, const uint8_t * b)
{
	T an; memcpy(&an, a, sizeof(T));
	T bn; memcpy(&bn, b, sizeof(T));
	return an^bn;
}
// memeq is small after optimization, but looks big to the inliner, so forceinline it
// if caller doesn't know size, caller should also be forceinlined
forceinline bool memeq(const void * a, const void * b, size_t len)
{
	if (!__builtin_constant_p(len)) return !memcmp(a, b, len);
	
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
#error enable the above on platforms where unaligned mem access is fast
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
	// however, it always gives correct results in 'reasonable' speed, so no point adding conditionals
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
// TODO: test
//#elif defined(_MSC_VER)
//	__movsb(dest, src, count);
//	dest += count;
//	src += count;
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
	in -= (bool)in; // so bitround(0) becomes 1, rather than integer overflow and back to 0
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
//undefined behavior if 'in' is negative or zero
template<typename T> static inline T ilog2(T in)
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
	if (sizeof(T) == 8) // this is probably slower than an if+shift and a 32bit mul... don't care about that either...
		return MultiplyDeBruijnBitPosition64[((uint64_t)in * 0x03F08A4C6ACB9DBD) >> 58];
	
	abort();
#endif
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

// Linux kernel has a macro for this, but non-expressions (like member names) in macros look wrong
template<typename Tc, typename Ti> Tc* container_of(Ti* ptr, Ti Tc:: * memb)
{
	// null math is technically UB, but every known compiler will do the right thing
	// https://wg21.link/P0908 proposes a better solution, but it was forgotten and not accepted
	Tc* fake_object = NULL;
	size_t offset = (uintptr_t)&(fake_object->*memb) - (uintptr_t)fake_object;
	return (Tc*)((uint8_t*)ptr - offset);
}
template<auto memb, typename Ti> auto container_of(Ti* ptr) { return container_of(ptr, memb); }

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
//     (dllexported non-functions are rare anyways, so no big deal for most APIs.)
// - The EXE/DLL startup code does a lot of stuff I don't understand, or never investigated; I probably forgot something important.
// - DllMain is completely ignored and never called. It won't even be referenced by anything, it will be garbage colleted.
// - Global objects' destructors are often implemented via atexit(), which is process-global in EXEs, and in things GCC thinks is an EXE.
//     As such, globals containing dynamic allocations are memory leaks at best; DLL paths may not touch them.
//     (Statically allocated globals are fine. For example, BearSSL caches the system certificate store into a global buffer.)
//     Note that parts of Arlib use global constructors or global variables. In particular, the following are memory leaks or worse:
//     - runloop::global()
//     - socket::create_ssl() SChannel backend (BearSSL is safe; OpenSSL is not supported on Windows)
//     - window_*
//     - mutex, if ARXPSUPPORT
//     - semaphore
//     - WuTF (not supported in DLLs at all)
// - To avoid crashes if atexit() calls an unloaded DLL, and to allow globals in EXE paths, globals' constructors are not run either.
//     Constant-initialized variables are safe, or if you need to (e.g.) put your process ID in a global, you can use oninit_static().
// - Some libc functions and compiler intrinsics that are implemented via global variables.
//     Most of them are safe; msvcrt is a normal DLL, and most things MinGW statically links are stateless.
//     The only useful exception I'm aware of, __builtin_cpu_supports, should be avoided anyways.
// - DLLs used by this program will never be unloaded. If your program only links against system libraries, this is fine,
//     something else is probably already using them; if your program ships its own DLLs (for example libstdc++-6.dll),
//     this is equivalent to a memory leak. (But if you're shipping DLLs, your program is already multiple files, and you
//     should put all shareable logic in another DLL and use a normal EXE.)
//     A hybrid DLL calling LoadLibrary and FreeLibrary is safe.
// - If a normal DLL imports a symbol that doesn't exist from another DLL, LoadLibrary's caller gets an error.
//     If a hybrid DLL imports a symbol that doesn't exist, it will remain as NULL, and will crash if called. You can't even
//     NULL check them, compiler will optimize it out. If you need that feature, use LoadLibrary.
// - Compiler-supported thread local storage in DLLs is unlikely to work. (TlsAlloc is fine.)
// - It uses a couple of GCC extensions, with no MSVC equivalent. (Though the rest of Arlib isn't tested under MSVC either.)
// - Not tested on anything except x86_64, and likely to crash. It has to relocate itself, and the relocator must be position
//     independent; PIC on i386 is hard.
// - It does various weird things; antivirus programs may react.
// On Linux, none of the above applies; it's a perfectly normal program in both respects.
#ifdef ARLIB_HYBRID_DLL
void arlib_hybrid_dll_init();
#else
static inline void arlib_hybrid_dll_init() {}
#endif

//If an interface defines a function to set some state, and a callback for when this state changes,
// calling that function will not trigger the state callback.
//An implementation may, at its sole discretion, choose to define any implementation of undefined
// behaviour, including reasonable ones. The user may, of course, not rely on that.

//This file, and many other parts of Arlib, uses a weird mix between Windows- and Linux-style
// filenames and paths. This is intentional; the author prefers Linux-style paths and directory
// structures, but Windows file extensions. .exe is less ambigous than no extension, and 'so' is a
// word while 'dll' is not; however, Windows' insistence on overloading the escape character is
// irritating. Since this excludes following any single OS, the rest is personal preference.

//Documentation is mandatory: if any question about the object's usage is not answered by reading
// the header, there's a bug (either more docs are needed, or the thing is badly designed). However,
// 'documentation' includes the function and parameter names, not just comments; there is only one
// plausible behavior for cstring::length(), so additional comments would just be noise.
// https://i.redd.it/3adwp98dswi21.jpg

#ifdef __MINGW32__
// force these to be imported from msvcrt, not mingwex, saves a bunch of kilobytes (pow is 2.5KB for whatever reason)
#define _GLIBCXX_MATH_H 1 // disable any subsequent #include <math.h>, it's full of using std::sin; that conflicts with my overloads
#include <cmath>

#define MATH_FN(name) \
	extern "C" __attribute__((dllimport)) double name(double); \
	extern "C" __attribute__((dllimport)) float name##f(float); \
	static inline float name(float a) { return name##f(a); } /* sinf would be better, but it's harmless, and useful in templates */
#define MATH_FN_2(name) \
	extern "C" __attribute__((dllimport)) double name(double,double); \
	extern "C" __attribute__((dllimport)) float name##f(float,float); \
	static inline float name(float a, float b) { return name##f(a, b); }

MATH_FN(sin) MATH_FN(cos)
MATH_FN(exp) MATH_FN(log) MATH_FN(log10)
MATH_FN_2(pow) MATH_FN(sqrt)
MATH_FN(ceil) MATH_FN(floor)

using std::isinf;
using std::isnan;
using std::isnormal;
using std::signbit;
#endif
