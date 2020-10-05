#include "global.h"
#include "endian.h"
#include "hash.h"
#include "simd.h"
#include "stringconv.h"
#include <new>

// trigger a warning if it doesn't stay disabled
#define __USE_MINGW_ANSI_STDIO 0


static void malloc_fail(size_t size)
{
#ifdef ARLIB_EXCEPTIONS
	throw std::bad_alloc();
#else
	if (size > 0) printf("malloc failed, size %" PRIuPTR "\n", size);
	else puts("malloc failed, size unknown");
	abort();
#endif
}

#undef malloc
anyptr xmalloc(size_t size)
{
	_test_malloc();
	if (size >= 0x80000000) malloc_fail(size);
	void* ret = malloc(size);
	if (size && !ret) malloc_fail(size);
	return ret;
}
#undef realloc
anyptr xrealloc(anyptr ptr, size_t size)
{
	if ((void*)ptr) _test_free();
	if (size) _test_malloc();
	if (size >= 0x80000000) malloc_fail(size);
	void* ret = realloc(ptr, size);
	if (size && !ret) malloc_fail(size);
	return ret;
}
#undef calloc
anyptr xcalloc(size_t size, size_t count)
{
	_test_malloc();
	void* ret = calloc(size, count);
	if (size && count && !ret) malloc_fail(size*count);
	return ret;
}


size_t hash(const uint8_t * val, size_t n)
{
	size_t hash = 5381;
	while (n >= sizeof(size_t))
	{
		size_t tmp;
		memcpy(&tmp, val, sizeof(size_t));        // extra >>7 because memcpy, xor and multiply means
		hash = (hash^(hash>>7)^tmp) * 2546270801; // bottom byte of output is only affected by every 8th byte
		val += sizeof(size_t);                    // the number is just a random prime between 2^31 and 2^32
		n -= sizeof(size_t);
	}
	while (n)
	{
		hash = (hash ^ *val) * 31;
		val++;
		n--;
	}
	return hash;
}


#ifdef __SSE2__
#define SIMD_DEBUG_INNER(suffix, sse_type, inner_type, fmt) \
	void debug##suffix(sse_type vals) \
	{ \
		inner_type inner[sizeof(sse_type)/sizeof(inner_type)]; \
		memcpy(inner, &vals, sizeof(sse_type)); \
		for (size_t i : range(ARRAY_SIZE(inner))) \
			printf("%s%c", (const char*)fmt(inner[i]), (i == ARRAY_SIZE(inner)-1 ? '\n' : ' ')); \
	} \
	void debug##suffix(const char * prefix, sse_type vals) { printf("%s ", prefix); debug##suffix(vals); }
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
#endif

//for windows:
//  these are the only libstdc++ functions I use. if I reimplement them, I don't need that library at all,
//   saving several hundred kilobytes and a DLL
//  (doesn't matter on linux where libstdc++ already exists)
//for tests:
//  need to override them, so they can be counted and leak checked
//  unfortunately, Valgrind overrides my override with an LD_PRELOAD, and I haven't found a reasonable way to override its override
//  (the unreasonable one is -s, which confuses Valgrind enough that it leaves new alone)
//  and valgrind's malloc/delete mismatch detector is also quite valuable
#if defined(__MINGW32__) || defined(ARLIB_TESTRUNNER)
void* operator new(std::size_t n) _GLIBCXX_THROW(std::bad_alloc) { return malloc(n); }
//Valgrind 3.13 overrides operator delete(void*), but not delete(void*,size_t)
//do not inline into free(p) until valgrind is fixed
#ifndef __MINGW32__ // however, mingw marks it inline, which obviously can't be used with noinline
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
