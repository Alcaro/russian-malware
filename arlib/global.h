#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE //strdup, realpath, asprintf
#endif
#define _strdup strdup //and windows is being windows as usual

//these shouldn't be needed with modern compilers, according to
//https://stackoverflow.com/questions/8132399/how-to-printf-uint64-t-fails-with-spurious-trailing-in-format
//but mingw 8.1.0 needs it anyways for whatever reason
#define __STDC_LIMIT_MACROS
#define __STDC_FORMAT_MACROS
#define __STDC_CONSTANT_MACROS // why are they so many?
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
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
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
// msvcrt strtod also rounds wrong sometimes, but only for unrealistic inputs, so I don't care about that either
#    define __USE_MINGW_ANSI_STDIO 0 // first, trigger a warning if it's enabled already - probably wrong include order
#    include <stdlib.h>              // second, include this specific header; it includes <bits/c++config.h>,
#    undef __USE_MINGW_ANSI_STDIO    // which sets this flag, which must be turned off
#    define __USE_MINGW_ANSI_STDIO 0 // (subsequent includes of c++config.h are harmless, there's an include guard)
#    undef strtof
#    define strtof strtof_arlib // third, redefine these functions, they pull in mingw's scanf
#    undef strtod               // (strtod not acting like scanf is creepy, anyways)
#    define strtod strtod_arlib // this is why stdlib.h is chosen, rather than some random tiny c++ header like cstdbool
#    undef strtold
#    define strtold strtold_arlib
float strtof_arlib(const char * str, char** str_end);
double strtod_arlib(const char * str, char** str_end);
long double strtold_arlib(const char * str, char** str_end);
#  endif
#  define STRICT
//the namespace pollution this causes is massive, but without it, there's a bunch of functions that
// just tail call kernel32.dll. With it, they can be inlined.
#  include <windows.h>
#  undef STRICT
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <inttypes.h>
#include <utility>
#include "function.h"

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
defer_holder<T> defer_create(T&& f)
{
	return f;
}
// Useful for implementing context manager macros like test_nomalloc, but should not be used directly.
#define contextmanager(begin_expr,end_expr) using(auto DEFER=defer_create(((begin_expr),[&](){ end_expr; })))

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
//this version doesn't work on GCC, it makes PPFE_MAP0 not get expanded the second time and quite effectively stops everything.
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
#define PPFOREACH(f, ...)        PPFE_EVAL(PPFE_MAP1(f,        __VA_ARGS__, ()()(), ()()(), ()()(), 0))
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
//- (FAIL) works if compiled as C (tried to design an alternate implementation and ifdef it, but nothing works inside structs)
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
//#define static_assert(expr)
//	typedef TYPENAME_IF_NEEDED static_assert_t<(bool)(expr)>::STATIC_ASSERTION_FAILED
//	JOIN(static_assertion_, __COUNTER__) MAYBE_UNUSED;
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


#ifdef ARLIB_TESTRUNNER
void _test_malloc();
void _test_free();
void free_test(void* ptr);
#define free free_test
#else
#define _test_malloc()
#define _test_free()
#endif

void malloc_fail(size_t size);

inline anyptr malloc_check(size_t size)
{
	_test_malloc();
	if (size >= 0x80000000) malloc_fail(size);
	void* ret = malloc(size);
	if (size && !ret) malloc_fail(size);
	return ret;
}
inline anyptr try_malloc(size_t size) { _test_malloc(); return malloc(size); }
#define malloc malloc_check

inline anyptr realloc_check(anyptr ptr, size_t size)
{
	if ((void*)ptr) _test_free();
	if (size) _test_malloc();
	if (size >= 0x80000000) malloc_fail(size);
	void* ret = realloc(ptr, size);
	if (size && !ret) malloc_fail(size);
	return ret;
}
inline anyptr try_realloc(anyptr ptr, size_t size) { if ((void*)ptr) _test_free(); if (size) _test_malloc(); return realloc(ptr, size); }
#define realloc realloc_check

inline anyptr calloc_check(size_t size, size_t count)
{
	_test_malloc();
	void* ret = calloc(size, count);
	if (size && count && !ret) malloc_fail(size*count);
	return ret;
}
inline anyptr try_calloc(size_t size, size_t count) { _test_malloc(); _test_free(); return calloc(size, count); }
#define calloc calloc_check

inline void malloc_assert(bool cond) { if (!cond) malloc_fail(0); }


//if I cast it to void, that means I do not care, so shut up about warn_unused_result
template<typename T> static inline void ignore(T t) {}

template<typename T> static T min(const T& a) { return a; }
template<typename T, typename... Args> static T min(const T& a, Args... args)
{
	const T& b = min(args...);
	if (a < b) return a;
	else return b;
}

template<typename T> static T max(const T& a) { return a; }
template<typename T, typename... Args> static T max(const T& a, Args... args)
{
	const T& b = max(args...);
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
	nocopy() = default; // do not use {}, it optimizes poorly
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
	T* ptr;
public:
	autoptr() : ptr(NULL) {}
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
	T* ptr;
public:
	autofree() : ptr(NULL) {}
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
		uint32_t refcount;
		T item;
	};
	inner_t* inner;
	
public:
	refcount() { inner = new inner_t(); inner->refcount = 1; }
	refcount(nullptr_t) { inner = NULL; }
	refcount(const refcount<T>& other) { inner = other.inner; inner->refcount++; }
	refcount(refcount<T>&& other) { inner = other.inner; other.inner = NULL; }
	refcount<T>& operator=(T* ptr) = delete;
	refcount<T>& operator=(autofree<T>&& other) = delete;
	T* operator->() { return &inner->item; }
	T& operator*() { return inner->item; }
	const T* operator->() const { return &inner->item; }
	const T& operator*() const { return inner->item; }
	operator T*() { return &inner->item; }
	operator const T*() const { return &inner->item; }
	bool unique() const { return inner->refcount == 1; }
	~refcount() { if (inner && --inner->refcount == 0) delete inner; }
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




//#if defined(__linux__) || GCC_VERSION >= 40900
//#define asprintf(...) malloc_assert(asprintf(__VA_ARGS__) >= 0)
//#else
//void asprintf(char * * ptr, const char * fmt, ...);
//#endif

//Acts like strstr, with the obvious difference.
#if defined(_WIN32) || defined(__x86_64__) // Windows doesn't have this; Linux does, but libc is poorly optimized on x64.
void* memmem_arlib(const void * haystack, size_t haystacklen, const void * needle, size_t needlelen) __attribute__((pure));
#define memmem memmem_arlib
#endif

//Returns distance to first difference, or 'len' if that's smaller.
size_t memcmp_d(const void * a, const void * b, size_t len) __attribute__((pure));


//msvc:
//typedef unsigned long uint32_t;
//typedef unsigned __int64 uint64_t;
//typedef unsigned int size_t;

//undefined behavior if 'in' is negative, or if the output would be out of range (signed input types are fine)
//returns 1 if input is 0
template<typename T> static inline T bitround(T in)
{
#if defined(__GNUC__)
	static_assert(sizeof(unsigned) == 4);
	static_assert(sizeof(unsigned long long) == 8);
	
	if (in <= 1) return 1;
	if (sizeof(T) <= 4) return 2u << (__builtin_clz(in - 1) ^ 31); // clz on x86 is bsr^31, so this compiles to bsr^31^31,
	if (sizeof(T) == 8) return 2ull<<(__builtin_clzll(in-1) ^ 63); // which optimizes better than the usual 1<<(32-clz) bit_ceil
	
	abort();
#endif
	
	in -= (bool)in; // so bitround(0) becomes 1, rather than integer overflow and back to 0
	in |= in>>1;
	in |= in>>2;
	in |= in>>4;
	if constexpr (sizeof(in) > 1) in |= in>>8; // silly ifs to avoid 'shift amount out of range' warnings
	if constexpr (sizeof(in) > 2) in |= in>>16;
	if constexpr (sizeof(in) > 4) in |= in>>32;
	in++;
	return in;
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

#define ALLNUMS(x) \
	ALLINTS(x) \
	x(float) \
	x(double) \

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

#define container_of(ptr, outer_t, member) ((outer_t*)((uint8_t*)(ptr) - (offsetof(outer_t,member))))

class range_iter_t {
	size_t n;
	size_t step;
public:
	range_iter_t(size_t n, size_t step) : n(n), step(step) {}
	bool operator!=(const range_iter_t& other) { return n < other.n; }
	forceinline void operator++() { n += step; }
	size_t operator*() { return n; }
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

// WARNING: Hybrid EXE/DLL is not supported by the OS. If it's ran as an EXE, it's a perfectly normal EXE,
//  other than its nonempty exports section; however, if used as a DLL, it's subject to several limitations:
// - It's not supported by the OS; it relies on a bunch of implementation details and ugly tricks that may break in newer OSes.
// - The program must call arlib_hybrid_dll_init() at the top of every DLLEXPORT function.
//     (It's safe to call it multiple times, including multithreaded, as long as the first one has returned before the second enters.)
//     (It's also safe to call it if it's ran as an EXE, and on OSes other than Windows. It'll just do nothing.)
//     (Only the first call to arlib_hybrid_dll_init() does anything, so it's safe to omit it in
//       exported functions guaranteed to not be the first one called, though not recommended.)
// - It is poorly tested. __builtin_cpu_supports does not function properly, and I don't know what else is broken.
// - DLLEXPORTed variables may not have constructors - the ctors are only called in arlib_hybrid_dll_init().
// - On XP, global variables' destructors will never run, and dependent DLLs are never unloaded. It's a memory leak.
//     On Vista and higher, it's untested.
//     Note that parts of Arlib contains global constructors.
// - If a normal DLL imports a symbol that doesn't exist, LoadLibrary's caller gets an error.
//     If this one loads a symbol that doesn't exist, it'll crash.
// - The program may not use compiler-supported thread-local variables from a DLL path.
//     TlsAlloc()/etc is fine, but since destructors are unreliable, it's not recommended.
#ifdef ARLIB_HYBRID_DLL
void arlib_hybrid_dll_init();
#else
#define arlib_hybrid_dll_init() // null
#endif

//If an interface defines a function to set some state, and a callback for when this state changes,
// calling that function will not trigger the state callback.
//An implementation may, at its sole discretion, choose to define any implementation of undefined
// behaviour, including reasonable ones. The user may, of course, not rely on that.
//Any function that starts with an underscore may only be called by the module that implements that
// function. ("Module" is defined as "anything whose compilation is controlled by the same #ifdef,
// or the file implementing an interface, whichever makes sense"; for example, window-win32-* is the
// same module.) The arguments and return values of these private functions may change meaning
// between modules, and the functions are not guaranteed to exist at all, or closely correspond to
// their name. For example, _window_init_misc on GTK+ instead initializes a component needed by the
// listboxes.

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
