#pragma once
#include "global.h"
#include "stringconv.h"
#include "linq.h"

//Arlib test policy:
//- Tests are to be written alongside the implementation, to verify how comfortable that interface is.
//- Tests are subordinate to the interface, implementation, and callers.
//    The latter three should be as simple as possible; as much complexity as possible should be in the tests.
//- As a corollary, tests may do weird things in order to exercise deeply-buried code paths of the implementation.
//- All significant functionality should be tested.
//- Tests shall test one thing only, and shall assume that their dependencies are correct.
//- If a test failure in a module is actually due to a dependency, there's also a bug in dependency's tests. Fix that bug first.
//- Tests may assume that the implementation tries to do what it should.
//    There's no need to e.g. verify that a file object actually hits the disk. If it hardcodes what the tests expect, it's malicious.
//    As a corollary, if it's obviously correct, there's no need to test it.
//- Mocking should generally be avoided. Prefer testing against real services. Mocks can simplify configuration and speed things up,
//    but they're also nontrivial effort to write, can be buggy or inaccurate, can displace things that should be tested,
//    and injecting them usually involves adding complexity to implementation and/or interface.
//    For example, the HTTP client tests will poke the internet.
//Rare exceptions can be permitted, such as various ifdefs in the runloop implementations (they catch a significant amount of issues,
//    with zero additional header complexity or release-mode bloat), or UDP sockets being untested (DNS client covers both).

// Both ARLIB_TEST and ARLIB_TESTRUNNER are defined in the main program if compiling for tests.
// In Arlib itself, only ARLIB_TESTRUNNER is defined.

#undef assert

#ifdef ARLIB_TESTRUNNER
void _test_runloop_latency(uint64_t us);
#endif

#ifdef ARLIB_TEST
class _testdecl {
public:
	_testdecl(void(*func)(), const char * filename, int line, const char * name, const char * requires, const char * provides);
};

void _testfail(cstring why, cstring file, int line);
void _testcmpfail(cstring why, cstring file, int line, cstring expected, cstring actual);
void _test_nothrow(int add);

void _teststack_push(cstring file, int line);
void _teststack_pop();
void _teststack_pushstr(string text);
void _teststack_popstr();
void _test_blockmalloc();
void _test_unblockmalloc();

void _test_skip(cstring why);
void _test_skip_force(cstring why);
void _test_inconclusive(cstring why);
void _test_expfail(cstring why);

//undefined behavior if T is unsigned and T2 is negative
//I'd prefer making it compare properly, but that requires way too many conditionals.
template<typename T, typename T2>
bool _test_eq(const T& v, const T2& v2)
{
	return (v == (T)v2);
}
template<typename T, typename T2>
bool _test_lt(const T& v, const T2& v2)
{
	return (v < (T)v2);
}

template<typename T, typename T2>
void _assert_eq(const T&  actual,   const char * actual_exp,
                const T2& expected, const char * expected_exp,
                cstring file, int line)
{
	if (!_test_eq(actual, expected))
	{
		_testcmpfail((string)actual_exp+" == "+expected_exp, file, line, tostring_dbg(expected), tostring_dbg(actual));
	}
}

template<typename T, typename T2>
void _assert_ne(const T&  actual,   const char * actual_exp,
                const T2& expected, const char * expected_exp,
                cstring file, int line)
{
	if (!!_test_eq(actual, expected)) // a!=b implemented as !(a==b)
	{
		_testcmpfail((string)actual_exp+" != "+expected_exp, file, line, tostring_dbg(expected), tostring_dbg(actual));
	}
}

template<typename T, typename T2>
void _assert_lt(const T&  actual,   const char * actual_exp,
                const T2& expected, const char * expected_exp,
                cstring file, int line)
{
	if (!_test_lt(actual, expected))
	{
		_testcmpfail((string)actual_exp+" < "+expected_exp, file, line, tostring_dbg(expected), tostring_dbg(actual));
	}
}

template<typename T, typename T2>
void _assert_lte(const T&  actual,   const char * actual_exp,
                 const T2& expected, const char * expected_exp,
                 cstring file, int line)
{
	if (!!_test_lt(expected, actual)) // a<=b implemented as !(b<a)
	{
		_testcmpfail((string)actual_exp+" <= "+expected_exp, file, line, tostring_dbg(expected), tostring_dbg(actual));
	}
}

template<typename T, typename T2>
void _assert_gt(const T&  actual,   const char * actual_exp,
                const T2& expected, const char * expected_exp,
                cstring file, int line)
{
	if (!_test_lt(expected, actual)) // a>b implemented as b<a
	{
		_testcmpfail((string)actual_exp+" > "+expected_exp, file, line, tostring_dbg(expected), tostring_dbg(actual));
	}
}

template<typename T, typename T2>
void _assert_gte(const T&  actual,   const char * actual_exp,
                 const T2& expected, const char * expected_exp,
                 cstring file, int line)
{
	if (!!_test_lt(actual, expected)) // a>=b implemented as !(a<b)
	{
		_testcmpfail((string)actual_exp+" >= "+expected_exp, file, line, tostring_dbg(expected), tostring_dbg(actual));
	}
}

template<typename T, typename T2>
void _assert_range(const T&  actual, const char * actual_exp,
                   const T2& min,    const char * min_exp,
                   const T2& max,    const char * max_exp,
                   cstring file, int line)
{
	if (_test_lt(actual, min) || _test_lt(max, actual))
	{
		_testcmpfail((string)actual_exp+" in ["+min_exp+".."+max_exp+"]", file, line,
		             "["+tostring_dbg(min)+".."+tostring_dbg(max)+"]", tostring_dbg(actual));
	}
}

#define TESTFUNCNAME JOIN(_testfunc, __LINE__)
//'name' is printed to the user, and can be used for test filtering.
//'provides' is what feature this test is for.
//'requires' is which features this test requires to function correctly, comma separated; if not set correctly,
// this test could be blamed for an underlying fault. (Though incomplete testing of underlying components yield that result too.)
//If multiple tests provide the same feature, all of them must run before anything depending on it can run
// (however, the test will run even if the prior one fails).
//Requiring a feature that no test provides, or cyclical dependencies, causes a test failure. Providing something nothing needs is fine.
#define test(name, requires, provides) \
	static void TESTFUNCNAME(); \
	static KEEP_OBJECT _testdecl JOIN(_testdecl, __LINE__)(TESTFUNCNAME, __FILE__, __LINE__, name, requires, provides); \
	static void TESTFUNCNAME()
#define assert(x) do { if (!(x)) { _testfail("Failed assertion " #x, __FILE__, __LINE__); } } while(0)
#define assert_msg(x, msg) do { if (!(x)) { _testfail((string)"Failed assertion " #x ": "+msg, __FILE__, __LINE__); } } while(0)
#define _assert_fn(fn,actual,expected,ret) do { \
		fn(actual, #actual, expected, #expected, __FILE__, __LINE__); \
	} while(0)
#define assert_eq(actual,expected) _assert_fn(_assert_eq,actual,expected,ret)
#define assert_ne(actual,expected) _assert_fn(_assert_ne,actual,expected,ret)
#define assert_lt(actual,expected) _assert_fn(_assert_lt,actual,expected,ret)
#define assert_lte(actual,expected) _assert_fn(_assert_lte,actual,expected,ret)
#define assert_gt(actual,expected) _assert_fn(_assert_gt,actual,expected,ret)
#define assert_gte(actual,expected) _assert_fn(_assert_gte,actual,expected,ret)
#define assert_range(actual,min,max) do { \
		_assert_range(actual, #actual, min, #min, max, #max, __FILE__, __LINE__); \
	} while(0)
#define assert_unreachable() do { _testfail("assert_unreachable() wasn't unreachable", __FILE__, __LINE__); } while(0)
#define test_nomalloc contextmanager(_test_blockmalloc(), _test_unblockmalloc())
#define testctx(x) contextmanager(_teststack_pushstr(x), _teststack_popstr())
#define testcall(x) do { contextmanager(_teststack_push(__FILE__, __LINE__), _teststack_pop()) { x; } } while(0)
#define test_skip(x) do { _test_skip(x); } while(0)
#define test_skip_force(x) do { _test_skip_force(x); } while(0)
#define test_fail(msg) do { _testfail(msg, __FILE__, __LINE__); } while(0)
#define test_inconclusive(x) do { _test_inconclusive(x); } while(0)
#define test_expfail(x) do { _test_expfail(x); } while(0)
#define test_nothrow contextmanager(_test_nothrow(+1), _test_nothrow(-1))

#if defined(__clang__)
// I am not proud of this code.
struct assert_reached_t {
	assert_reached_t * link;
	const char * file;
	int lineno;
};

__attribute__((unused))
static assert_reached_t* assert_reached_impl(assert_reached_t* n)
{
	static assert_reached_t* root = NULL;
	if (!n) return root;
	n->link = root;
	root = n;
	return NULL;
}

#define assert_reached()                                             \
	do {                                                             \
		static assert_reached_t node = { NULL, __FILE__, __LINE__ }; \
		struct x { __attribute__((constructor)) static void y() {    \
			assert_reached_impl(&node); } };                         \
		node.file = NULL;                                            \
	} while(0)

__attribute__((unused))
static void assert_all_reached()
{
	assert_reached_t* node = assert_reached_impl(NULL);
	test_nothrow {
		while (node) {
			if (node->file) _testfail("assert_reached() wasn't", node->file, node->lineno);
			node = node->link;
		}
	}
}

#elif defined(__GNUC__)
// and this one is even worse, but I couldn't find anything better that GCC supports
//  (other than gcov, which doesn't integrate with my testing framework, and interacts poorly with same-line if-return).
// Both implementations give false negatives if compiler deletes the code as provably unreachable,
//  and I don't know what happens if the function is inlined, a template, or otherwise duplicated.
#define assert_reached()                                \
	do {                                                \
		__asm__ volatile(                               \
			".pushsection .data\n"                      \
			".subsection 2\n"                           \
			".LCreached%=: .int " STR(__LINE__) "\n"    \
			".popsection\n"                             \
			"{movl $0, .LCreached%=(%%rip)"             \
			"|mov dword ptr [.LCreached%=+%%rip], 0}\n" \
			:); /* happy code */                        \
	} while(0) // (%= and {|} only exist in extended asm, which needs a :)
#define assert_all_reached()                                               \
	do {                                                                   \
		int* iter;                                                         \
		int* end;                                                          \
		__asm__ volatile(                                                  \
			".pushsection .data\n"                                         \
			".subsection 1\n"                                              \
			".LCreached_init:\n"                                           \
			".subsection 3\n"                                              \
			".LCreached_fini:\n"                                           \
			".popsection\n"                                                \
			"lea {.LCreached_init(%%rip), %0"                              \
			    "|%0, [.LCreached_init+%%rip]}\n"                          \
			"lea {.LCreached_fini(%%rip), %1"                              \
			    "|%1, [.LCreached_fini+%%rip]}\n"                          \
			: "=r"(iter), "=r"(end));                                      \
		test_nothrow {                                                     \
			while (iter < end)                                             \
			{                                                              \
				if (*iter)                                                 \
					_testfail("assert_reached() wasn't", __FILE__, *iter); \
				iter++;                                                    \
			}                                                              \
		}                                                                  \
	} while(0)
#else
// is this even possible without gcc extensions or a custom build step?
#endif

#define main not_quite_main
int not_quite_main(int argc, char** argv);

#else

#define test(...) static void MAYBE_UNUSED JOIN(_testfunc_, __LINE__)()
#define assert(x) ((void)(x))
#define assert_msg(x, msg) ((void)(x),(void)(msg))
#define assert_eq(x,y) ((void)((x)==(y)))
#define assert_ne(x,y) ((void)((x)==(y)))
#define assert_lt(x,y) ((void)((x)<(y)))
#define assert_lte(x,y) ((void)((x)<(y)))
#define assert_gt(x,y) ((void)((x)<(y)))
#define assert_gte(x,y) ((void)((x)<(y)))
#define assert_range(x,y,z) ((void)((x)<(y)))
#define test_nomalloc
#define testctx(x)
#define testcall(x) x
#define test_skip(x)
#define test_skip_force(x)
#define test_fail(msg)
#define test_inconclusive(x)
#define test_expfail(x)
#define assert_reached()
#define assert_all_reached()
#define assert_unreachable()
#define test_nothrow

#endif

#ifdef ARLIB_SOCKET
class socket; class runloop;
//Ensures that the given socket is usable and speaks HTTP. Socket is not usable afterwards, but caller is responsible for closing it.
void socket_test_http(socket* sock, runloop* loop);
//Ensures the socket does not return an answer if the client tries to speak HTTP.
void socket_test_fail(socket* sock, runloop* loop);
#endif

#if __has_include(<valgrind/memcheck.h>)
# include <valgrind/memcheck.h>
#elif defined(__unix__)
# include "deps/valgrind/memcheck.h"
#else
# define RUNNING_ON_VALGRIND false
# define VALGRIND_PRINTF_BACKTRACE(...) abort()
#endif
