#include "global.h"
#include "endian.h"
#include "hash.h"
#include "simd.h"
#include "stringconv.h"
#include "os.h"
#include "test.h"
#include <new>

// trigger a warning if it doesn't stay disabled
#define __USE_MINGW_ANSI_STDIO 0


static void malloc_fail(size_t size)
{
	char buf[64];
	sprintf(buf, "malloc failed, size %" PRIuPTR "\n", size);
	debug_fatal(buf);
}

void* xmalloc_inner(size_t size)
{
	_test_malloc();
	if (size >= 0x80000000) malloc_fail(size);
	void* ret = try_malloc(size);
	if (size && !ret) malloc_fail(size);
	return ret;
}
void* xrealloc_inner(void* ptr, size_t size)
{
	if ((void*)ptr) _test_free();
	if (size) _test_malloc();
	if (size >= 0x80000000) malloc_fail(size);
	void* ret = try_realloc(ptr, size);
	if (size && !ret) malloc_fail(size);
	return ret;
}
void* xcalloc_inner(size_t size, size_t count)
{
	_test_malloc();
	void* ret = try_calloc(size, count);
	if (size && count && !ret) malloc_fail(size*count);
	return ret;
}

// Loads len bytes from the pointer, native endian. The high part is zero. load_small<uint64_t>("\x11\x22\x33", 3) is 0x112233 or 0x332211.
// Equivalent to T ret = 0; memcpy(&ret, ptr, len);, but faster.
// T must be an unsigned builtin integer type, and len must be <= sizeof(T).
template<typename T>
T load_small(const uint8_t * ptr, size_t len)
{
#if !defined(ARLIB_OPT)
	if (RUNNING_ON_VALGRIND)
	{
		// like memmem.cpp load_sse2_small_highundef, Valgrind does not like the below one
		T ret = 0;
		memcpy(&ret, ptr, len);
		return ret;
	}
#endif
	if (len == 0)
		return 0;
	
	if (uintptr_t(ptr) & sizeof(T))
	{
		// if the sizeof(T) bit is set, then extending the read upwards could potentially hit the next page
		// but extending downwards is safe, so do that
		T ret;
		memcpy(&ret, ptr-sizeof(T)+len, sizeof(T));
#if END_LITTLE
		ret >>= (sizeof(T)-len)*8;
#else
		ret &= ~(((T)-2) << (len*8-1)); // extra -1 on shift, and -2 on lhs, to avoid trouble if len == sizeof
#endif
		return ret;
	}
	else
	{
		// if the sizeof(T) bit is not set, then extending downwards could hit previous page, but extending upwards is safe
		// (in both cases, alignment is required)
		T ret;
		memcpy(&ret, ptr, sizeof(T));
#if END_LITTLE
		ret &= ~(((T)-2) << (len*8-1));
#else
		ret >>= (sizeof(T)-len)*8;
#endif
		return ret;
	}
}

size_t hash(const uint8_t * val, size_t n)
{
	if (n < sizeof(size_t))
		return load_small<size_t>(val, n);
	
	size_t hash = 5381;
	while (n > sizeof(size_t))
	{
		size_t tmp;
		memcpy(&tmp, val, sizeof(size_t));
		if constexpr (sizeof(size_t) == 8)
			hash = (hash^(hash>>45)^tmp) * 2546270801; // extra >>45 because otherwise bottom byte of output would
		else                                           //  only be affected by every 8th byte of input
			hash = (hash^(hash>>21)^tmp) * 2546270801; // the number is just a random prime between 2^31 and 2^32
		val += sizeof(size_t);
		n -= sizeof(size_t);
	}
	
	// do a final size_t load overlapping with the previous bytes (or not overlapping, if size is a multiple of 8)
	val -= (sizeof(size_t)-n);
	size_t tmp;
	memcpy(&tmp, val, sizeof(size_t));
	
	return hash + tmp;
}


#ifdef runtime__SSE2__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpsabi" // doesn't work, but...
#define SIMD_DEBUG_INNER(suffix, sse_type, inner_type, fmt) \
	void debug##suffix(const sse_type& vals) \
	{ \
		inner_type inner[sizeof(sse_type)/sizeof(inner_type)]; \
		memcpy(inner, &vals, sizeof(sse_type)); \
		for (size_t i : range(ARRAY_SIZE(inner))) \
			printf("%s%c", (const char*)fmt(inner[i]), (i == ARRAY_SIZE(inner)-1 ? '\n' : ' ')); \
	} \
	void debug##suffix(const char * prefix, const sse_type& vals) { printf("%s ", prefix); debug##suffix(vals); }
#define SIMD_DEBUG_OUTER(bits) \
	SIMD_DEBUG_INNER(d##bits, __m128i, int##bits##_t, tostring) \
	SIMD_DEBUG_INNER(u##bits, __m128i, uint##bits##_t, tostring) \
	SIMD_DEBUG_INNER(x##bits, __m128i, uint##bits##_t, tostringhex<bits/4>)
SIMD_DEBUG_OUTER(8)
SIMD_DEBUG_OUTER(16)
SIMD_DEBUG_OUTER(32)
SIMD_DEBUG_OUTER(64)
SIMD_DEBUG_INNER(f32, __m128, float, tostring)
SIMD_DEBUG_INNER(f64, __m128d, double, tostring)
#undef SIMD_DEBUG_INNER
#undef SIMD_DEBUG_OUTER
void debugc8(const __m128i& vals)
{
	char inner[sizeof(__m128i)/sizeof(char)];
	memcpy(inner, &vals, sizeof(__m128i));
	for (size_t i : range(ARRAY_SIZE(inner)))
		printf("%c%c", inner[i], (i == ARRAY_SIZE(inner)-1 ? '\n' : ' '));
}
void debugc8(const char * prefix, const __m128i& vals) { printf("%s ", prefix); debugc8(vals); }
#pragma GCC diagnostic pop
#endif

//for windows:
//  these are the only libstdc++ functions I use. if I reimplement them, I don't need that library at all,
//   saving several hundred kilobytes and a DLL
//  (doesn't matter on linux where libstdc++ already exists)
//for tests:
//  need to override them, so they can be counted, leak checked, and rejected in test_nomalloc
//  Valgrind overrides my new/delete override with an LD_PRELOAD, but Valgrind has its own leak checker,
//   and new is only used for classes with vtables that don't make sense to use in test_nomalloc anyways
//  (I can confuse and disable valgrind's override by compiling with -s, but that has the obvious side effects.)
#if defined(__MINGW32__) || defined(ARLIB_TESTRUNNER)
void* operator new(std::size_t n) _GLIBCXX_THROW(std::bad_alloc) { return try_malloc(n); }
//Valgrind 3.13 overrides operator delete(void*), but not delete(void*,size_t)
//do not inline into free(p) until valgrind is fixed
#ifndef __MINGW32__ // mingw marks it inline, which obviously can't be used with noinline, but mingw doesn't need valgrind workarounds
__attribute__((noinline))
#endif
void operator delete(void* p) noexcept { free(p); }
#if __cplusplus >= 201402
void operator delete(void* p, std::size_t n) noexcept { operator delete(p); }
#endif
#endif

#ifdef __MINGW32__
extern "C" void __cxa_pure_virtual(); // predeclaration for -Wmissing-declarations
extern "C" void __cxa_pure_virtual() { __builtin_trap(); }

// pseudo relocs are disabled, and its handler takes ~1KB; better stub it out
extern "C" void _pei386_runtime_relocator();
extern "C" void _pei386_runtime_relocator() {}

/*
int asprintf(char ** strp, const char * fmt, ...)
{
	char buf[1200];
	va_list args;
	int len;
	va_start(args, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	*strp = malloc(len+1);
	if (!*strp) return -1;
	if (len < sizeof(buf))
	{
		memcpy(*strp, buf, len+1);
	}
	else
	{
		va_start(args, fmt);
		len = vsnprintf(*strp, len+1, fmt, args);
		va_end(args);
	}
	return 0;
}
*/
#endif

test("bitround", "", "")
{
	assert_eq(bitround((unsigned)0), 1);
	assert_eq(bitround((unsigned)1), 1);
	assert_eq(bitround((unsigned)2), 2);
	assert_eq(bitround((unsigned)3), 4);
	assert_eq(bitround((unsigned)4), 4);
	assert_eq(bitround((unsigned)640), 1024);
	assert_eq(bitround((unsigned)0x7FFFFFFF), 0x80000000);
	assert_eq(bitround((unsigned)0x80000000), 0x80000000);
	assert_eq(bitround((signed)0), 1);
	assert_eq(bitround((signed)1), 1);
	assert_eq(bitround((signed)2), 2);
	assert_eq(bitround((signed)3), 4);
	assert_eq(bitround((signed)4), 4);
	assert_eq(bitround((signed)640), 1024);
	assert_eq(bitround((signed)0x3FFFFFFF), 0x40000000);
	assert_eq(bitround((signed)0x40000000), 0x40000000);
	assert_eq(bitround<uint8_t>(0), 1);
	assert_eq(bitround<uint8_t>(1), 1);
	assert_eq(bitround<uint8_t>(2), 2);
	assert_eq(bitround<uint8_t>(3), 4);
	assert_eq(bitround<uint8_t>(4), 4);
	assert_eq(bitround<uint16_t>(0), 1);
	assert_eq(bitround<uint16_t>(1), 1);
	assert_eq(bitround<uint16_t>(2), 2);
	assert_eq(bitround<uint16_t>(3), 4);
	assert_eq(bitround<uint16_t>(4), 4);
	assert_eq(bitround<uint32_t>(0), 1);
	assert_eq(bitround<uint32_t>(1), 1);
	assert_eq(bitround<uint32_t>(2), 2);
	assert_eq(bitround<uint32_t>(3), 4);
	assert_eq(bitround<uint32_t>(4), 4);
	assert_eq(bitround<uint64_t>(0), 1);
	assert_eq(bitround<uint64_t>(1), 1);
	assert_eq(bitround<uint64_t>(2), 2);
	assert_eq(bitround<uint64_t>(3), 4);
	assert_eq(bitround<uint64_t>(4), 4);
	
	//assert_eq(ilog2(0), -1);
	assert_eq(ilog2(1), 0);
	assert_eq(ilog2(2), 1);
	assert_eq(ilog2(3), 1);
	assert_eq(ilog2(4), 2);
	assert_eq(ilog2(7), 2);
	assert_eq(ilog2(8), 3);
	assert_eq(ilog2(15), 3);
	assert_eq(ilog2(16), 4);
	assert_eq(ilog2(31), 4);
	assert_eq(ilog2(32), 5);
	assert_eq(ilog2(63), 5);
	assert_eq(ilog2(64), 6);
	assert_eq(ilog2(127), 6);
	assert_eq(ilog2(128), 7);
	assert_eq(ilog2(255), 7);
	
	auto test1 = [](uint64_t n)
	{
		testctx(tostring(n))
		{
			if (n)
				assert_eq(ilog10(n), snprintf(NULL,0, "%" PRIu64, n)-1);
			if ((uint32_t)n)
				assert_eq(ilog10((uint32_t)n), snprintf(NULL,0, "%" PRIu32, (uint32_t)n)-1);
		}
	};
	for (uint64_t i=1;i!=0;i*=2)
	{
		test1(i-1);
		test1(i);
	}
	for (uint64_t i=1;i!=7766279631452241920;i*=10) // 7766279631452241920 = (uint64_t)100000000000000000000
	{
		test1(i-1);
		test1(i);
	}
}

test("test_nomalloc", "", "")
{
	test_skip("unfixable expected-failure");
	if (RUNNING_ON_VALGRIND) test_expfail("Valgrind doesn't catch new within test_nomalloc, rerun with gdb or standalone");
}

static int g_x;
static int y()
{
	g_x = 0;
	contextmanager(g_x=1, g_x=2)
	{
		assert_eq(g_x, 1);
		return 42;
	}
	assert_unreachable(); // both gcc and clang think this is reachable and throw warnings
	return -1;
}
test("contextmanager", "", "")
{
	g_x = 0;
	contextmanager(g_x=1, g_x=2)
	{
		assert_eq(g_x, 1);
	}
	assert_eq(g_x, 2);
	g_x = 0;
	assert_eq(y(), 42);
	assert_eq(g_x, 2);
}
test("endian", "", "")
{
	union { uint8_t a[2]; uint16_t b; } c;
	c.b = 0x0100;
	assert_eq(c.a[0], END_BIG);
}

test("array_size", "", "")
{
	int a[5];
	static_assert(ARRAY_SIZE(a) == 5);
	int b[0];
	static_assert(ARRAY_SIZE(b) == 0);
}

#ifdef __unix__
#include <sys/mman.h>
#include <unistd.h>
#endif

test("load_small", "", "")
{
#ifdef __unix__
	size_t pagesize = sysconf(_SC_PAGESIZE);
	uint8_t * pages = (uint8_t*)mmap(nullptr, pagesize*3, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	mprotect(pages, pagesize, PROT_NONE);
	mprotect(pages+pagesize*2, pagesize, PROT_NONE);
#else
	size_t pagesize = 64; // just pick something random
	uint8_t * pages = xmalloc(pagesize*3);
#endif
	
	memset(pages+pagesize, 0xA5, pagesize);
	
	assert_eq(load_small<uint32_t>(pages+pagesize/2, 0), 0);
	
	auto test2 = [&](uint8_t* ptr, const char * bytes, size_t len) {
		uint32_t expect;
		memcpy(&expect, bytes, sizeof(uint32_t));
		if (END_BIG)
			expect >>= (sizeof(uint32_t)-len)*8;
		memcpy(ptr, bytes, len);
		assert_eq(load_small<uint32_t>(ptr, len), expect);
	};
	auto test1 = [&](const char * bytes, size_t len) {
		test2(pages+pagesize, bytes, len);
		test2(pages+pagesize*2-len, bytes, len);
	};
	
	test1("\x11\x00\x00\x00", 1);
	test1("\x11\x22\x00\x00", 2);
	test1("\x11\x22\x33\x00", 3);
	test1("\xC1\x22\x33\x89", 4);
	test1("\x40\x22\x33\x08", 4);
	
#ifdef __unix__
	munmap(pages, pagesize*3);
#else
	free(pages);
#endif
}

#if 0
static void bench(const uint8_t * buf, int len)
{
	benchmark b;
	while (b)
	{
		b.launder(hash(bytesr(buf, b.launder(len))));
	}
	printf("size %d - %f/s - %fGB/s\n", len, b.per_second(), b.per_second()*len/1024/1024/1024);
}

test("hash", "", "crc32")
{
	assert(!RUNNING_ON_VALGRIND);
	
	uint8_t buf[65536];
	uint32_t k = ~0;
	for (size_t i : range(65536))
	{
		k = (-(k&1) & 0xEDB88320) ^ (k>>1);
		buf[i] = k;
	}
	
	bench(buf, 65536);
	bench(buf, 1024);
	bench(buf, 256);
	bench(buf, 64);
	bench(buf, 32);
	bench(buf, 31);
	bench(buf, 24);
	bench(buf, 16);
	bench(buf, 8);
	bench(buf, 7);
	bench(buf, 6);
	bench(buf, 5);
	bench(buf, 4);
	bench(buf, 3);
	bench(buf, 2);
	bench(buf, 1);
	bench(buf, 0);
}
#endif
