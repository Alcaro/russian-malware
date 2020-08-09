#include "global.h"
#include "endian.h"
#include <new>

// trigger a warning if it doesn't stay disabled
#define __USE_MINGW_ANSI_STDIO 0

void malloc_fail(size_t size)
{
	if (size > 0) printf("malloc failed, size %" PRIuPTR "\n", size);
	else puts("malloc failed, size unknown");
	abort();
}

#if defined(__MINGW32__)
float strtof_arlib(const char * str, char** str_end)
{
	int n = 0;
	float ret;
	sscanf(str, "%f%n", &ret, &n);
	if (str_end) *str_end = (char*)str+n;
	return ret;
}
double strtod_arlib(const char * str, char** str_end)
{
	int n = 0;
	double ret;
	sscanf(str, "%lf%n", &ret, &n);
	if (str_end) *str_end = (char*)str+n;
	return ret;
}
// gcc doesn't acknowledge scanf("%Lf") as legitimate
// I can agree that long double is creepy, I'll just leave it commented out until (if) I use ld
//long double strtold_arlib(const char * str, char** str_end)
//{
//	int n;
//	long double ret;
//	sscanf(str, "%Lf%n", &ret, &n);
//	if (str_end) *str_end = (char*)str+n;
//	return ret;
//}
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
