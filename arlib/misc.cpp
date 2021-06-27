#include "global.h"
#include "endian.h"
#include "hash.h"
#include "simd.h"
#include "stringconv.h"
#include "os.h"
#include <new>

// trigger a warning if it doesn't stay disabled
#define __USE_MINGW_ANSI_STDIO 0


static void malloc_fail(size_t size)
{
	char buf[64];
	sprintf(buf, "malloc failed, size %" PRIuPTR "\n", size);
	debug_fatal(buf);
}

anyptr xmalloc(size_t size)
{
	_test_malloc();
	if (size >= 0x80000000) malloc_fail(size);
	void* ret = try_malloc(size);
	if (size && !ret) malloc_fail(size);
	return ret;
}
anyptr xrealloc(anyptr ptr, size_t size)
{
	if ((void*)ptr) _test_free();
	if (size) _test_malloc();
	if (size >= 0x80000000) malloc_fail(size);
	void* ret = try_realloc(ptr, size);
	if (size && !ret) malloc_fail(size);
	return ret;
}
anyptr xcalloc(size_t size, size_t count)
{
	_test_malloc();
	void* ret = try_calloc(size, count);
	if (size && count && !ret) malloc_fail(size*count);
	return ret;
}


size_t hash(const uint8_t * val, size_t n)
{
	size_t hash = 5381;
	while (n >= sizeof(size_t))
	{
		size_t tmp;
		memcpy(&tmp, val, sizeof(size_t));        // extra >>7 because otherwise bottom byte of output would
		hash = (hash^(hash>>7)^tmp) * 2546270801; //  only be affected by every 8th byte of input
		val += sizeof(size_t);                    // the number is just a random prime between 2^31 and 2^32
		n -= sizeof(size_t);
	}
	while (n)
	{
		hash = (hash ^ *val) * 31; // 31 is a quite common multiplier, don't know why
		val++;
		n--;
	}
	return hash;
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
//  (I can disable valgrind's override by compiling with -s, but that has the obvious side effects.)
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

#include "test.h"

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

static int x;
static int y()
{
	x = 0;
	contextmanager(x=1, x=2)
	{
		assert_eq(x, 1);
		return 42;
	}
}
test("contextmanager", "", "")
{
	x = 0;
	contextmanager(x=1, x=2)
	{
		assert_eq(x, 1);
	}
	assert_eq(x, 2);
	x = 0;
	assert_eq(y(), 42);
	assert_eq(x, 2);
}
test("endian", "", "")
{
	union { uint8_t a[2]; uint16_t b; } c;
	c.b = 0x0100;
	assert_eq(c.a[0], END_BIG);
}
