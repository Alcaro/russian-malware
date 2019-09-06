#include "global.h"
#include <new>

void malloc_fail(size_t size)
{
	if (size > 0) printf("malloc failed, size %" PRIuPTR "\n", size);
	else puts("malloc failed, size unknown");
	abort();
}

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
void operator delete(void* p) noexcept { free(p); }
#if __cplusplus >= 201402
//Valgrind 3.13 overrides operator delete(void*), but not delete(void*,size_t)
//do not inline into free(p) until it does
void operator delete(void* p, std::size_t n) noexcept { operator delete(p); }
#endif
#endif

#ifdef __MINGW32__
extern "C" void __cxa_pure_virtual(); // predeclaration for -Wmissing-declarations
extern "C" void __cxa_pure_virtual() { puts("__cxa_pure_virtual"); abort(); }
#endif

#include "test.h"

test("bitround", "", "")
{
	assert_eq(bitround((unsigned)1), 1);
	assert_eq(bitround((unsigned)2), 2);
	assert_eq(bitround((unsigned)3), 4);
	assert_eq(bitround((unsigned)4), 4);
	assert_eq(bitround((unsigned)640), 1024);
	assert_eq(bitround((unsigned)0x7FFFFFFF), 0x80000000);
	assert_eq(bitround((unsigned)0x80000000), 0x80000000);
	assert_eq(bitround((signed)1), 1);
	assert_eq(bitround((signed)2), 2);
	assert_eq(bitround((signed)3), 4);
	assert_eq(bitround((signed)4), 4);
	assert_eq(bitround((signed)640), 1024);
	assert_eq(bitround((signed)0x3FFFFFFF), 0x40000000);
	assert_eq(bitround((signed)0x40000000), 0x40000000);
}

test("test_nomalloc", "", "")
{
	test_skip("unfixable expected-failure");
	if (RUNNING_ON_VALGRIND) test_expfail("Valgrind doesn't catch new within test_nomalloc, rerun with gdb or standalone");
}
