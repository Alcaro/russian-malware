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
void debugc8(__m128i vals)
{
	char inner[sizeof(__m128i)/sizeof(char)];
	memcpy(inner, &vals, sizeof(__m128i));
	for (size_t i : range(ARRAY_SIZE(inner)))
		printf("%c%c", inner[i], (i == ARRAY_SIZE(inner)-1 ? '\n' : ' '));
}
void debugc8(const char * prefix, __m128i vals) { printf("%s ", prefix); debugc8(vals); }
#endif

//for windows:
//  these are the only libstdc++ functions I use. if I reimplement them, I don't need that library at all,
//   saving several hundred kilobytes and a DLL
//  (doesn't matter on linux where libstdc++ already exists)
//for tests:
//  need to override them, so they can be counted, leak checked, and rejected in test_nomalloc
//  Valgrind overrides my new/delete override with an LD_PRELOAD,  but Valgrind has its own leak checker,
//    and new is only used for classes with vtables that don't make sense to use in test_nomalloc anyways
//  (I can disable valgrind's override with -s, but that's obviously not worth it.)
#if defined(__MINGW32__) || defined(ARLIB_TESTRUNNER)
void* operator new(std::size_t n) _GLIBCXX_THROW(std::bad_alloc) { return malloc(n); }
//Valgrind 3.13 overrides operator delete(void*), but not delete(void*,size_t)
//do not inline into free(p) until valgrind is fixed
#ifndef __MINGW32__ // mingw marks it inline, which obviously can't be used with noinline (but mingw doesn't need valgrind workarounds)
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

#ifdef __x86_64__
// stamp out a jmp QWORD PTR [rip+12345678] in machine code
// extended asm isn't supported as a toplevel statement, and -masm=intel/att sets no preprocessor flag, so this is the best I can do
#define JMP_IMPORT(name) ".byte 0xFF,0x25; .int __imp_" #name "-" #name "-6"
#endif
#ifdef __i386__
#define JMP_IMPORT(name) ".byte 0xFF,0x25 : .int __imp_" #name
#endif
#define ASM_FORCE_IMPORT(name) __asm__(".section .text$" #name "; .globl " #name "; " #name ": " JMP_IMPORT(name) "; .text")

ASM_FORCE_IMPORT(sin);   ASM_FORCE_IMPORT(sinf);
ASM_FORCE_IMPORT(cos);   ASM_FORCE_IMPORT(cosf);
//ASM_FORCE_IMPORT(tan);   ASM_FORCE_IMPORT(tanf); // some of them get pulled in from mingwex, wasting space for no valid reason
//ASM_FORCE_IMPORT(sinh);  ASM_FORCE_IMPORT(sinhf); // some get errors if I try to override them
//ASM_FORCE_IMPORT(cosh);  ASM_FORCE_IMPORT(coshf); // I don't know which are mingwex and which are not
//ASM_FORCE_IMPORT(tanh);  ASM_FORCE_IMPORT(tanhf); // hopefully none are both mingwex and override-error simultaneously
//ASM_FORCE_IMPORT(asin);  ASM_FORCE_IMPORT(asinf);
//ASM_FORCE_IMPORT(acos);  ASM_FORCE_IMPORT(acosf);
//ASM_FORCE_IMPORT(atan);  ASM_FORCE_IMPORT(atanf);
//ASM_FORCE_IMPORT(atan2); ASM_FORCE_IMPORT(atan2f);
ASM_FORCE_IMPORT(exp);   ASM_FORCE_IMPORT(expf);
ASM_FORCE_IMPORT(log);   ASM_FORCE_IMPORT(logf);
//ASM_FORCE_IMPORT(log10); ASM_FORCE_IMPORT(log10f);
ASM_FORCE_IMPORT(pow);   ASM_FORCE_IMPORT(powf);
ASM_FORCE_IMPORT(sqrt);  ASM_FORCE_IMPORT(sqrtf);
ASM_FORCE_IMPORT(ceil);  ASM_FORCE_IMPORT(ceilf);
ASM_FORCE_IMPORT(floor); ASM_FORCE_IMPORT(floorf);
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
