#pragma once
#include "global.h"
#include "stringconv.h"

#undef assert

#ifdef ARLIB_TEST

class _test_maybeptr {
	const char * data;
public:
	_test_maybeptr() : data(NULL) {}
	_test_maybeptr(const char * data) : data(data) {}
	
	operator const char *() { return data; }
};
class _testdecl {
public:
	_testdecl(void(*func)(), const char * loc, const char * name);
};

extern int _test_result;

void _testfail(cstring why, int line);
void _testcmpfail(cstring why, int line, cstring expected, cstring actual);

void _teststack_push(int line);
void _teststack_pop();

void _test_skip(cstring why);
void _test_inconclusive(cstring why);

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
                int line)
{
	if (!_test_eq(actual, expected))
	{
		_testcmpfail((string)actual_exp+" == "+expected_exp, line, tostring(expected), tostring(actual));
	}
}

template<typename T, typename T2>
void _assert_neq(const T&  actual,   const char * actual_exp,
                const T2& expected, const char * expected_exp,
                int line)
{
	if (!!_test_eq(actual, expected)) // a!=b implemented as !(a==b)
	{
		_testcmpfail((string)actual_exp+" != "+expected_exp, line, tostring(expected), tostring(actual));
	}
}

template<typename T, typename T2>
void _assert_lt(const T&  actual,   const char * actual_exp,
                const T2& expected, const char * expected_exp,
                int line)
{
	if (!_test_lt(actual, expected))
	{
		_testcmpfail((string)actual_exp+" < "+expected_exp, line, tostring(expected), tostring(actual));
	}
}

template<typename T, typename T2>
void _assert_lte(const T&  actual,   const char * actual_exp,
                 const T2& expected, const char * expected_exp,
                 int line)
{
	if (!!_test_lt(expected, actual)) // a<=b implemented as !(b<a)
	{
		_testcmpfail((string)actual_exp+" <= "+expected_exp, line, tostring(expected), tostring(actual));
	}
}

template<typename T, typename T2>
void _assert_gt(const T&  actual,   const char * actual_exp,
                 const T2& expected, const char * expected_exp,
                 int line)
{
	if (!_test_lt(expected, actual)) // a>b implemented as b<a
	{
		_testcmpfail((string)actual_exp+" > "+expected_exp, line, tostring(expected), tostring(actual));
	}
}

template<typename T, typename T2>
void _assert_gte(const T&  actual,   const char * actual_exp,
                 const T2& expected, const char * expected_exp,
                 int line)
{
	if (!!_test_lt(actual, expected)) // a>=b implemented as !(a<b)
	{
		_testcmpfail((string)actual_exp+" >= "+expected_exp, line, tostring(expected), tostring(actual));
	}
}

template<typename T, typename T2>
void _assert_range(const T&  actual, const char * actual_exp,
                   const T2& min,    const char * min_exp,
                   const T2& max,    const char * max_exp,
                   int line)
{
	if (_test_lt(actual, min) || _test_lt(max, actual))
	{
		_testcmpfail((string)actual_exp+" in ["+min_exp+".."+max_exp+"]", line, "["+tostring(min)+".."+tostring(max)+"]", tostring(actual));
	}
}

#define TESTFUNCNAME JOIN(_testfunc, __LINE__)
#define test(...) \
	static void TESTFUNCNAME(); \
	static KEEP_OBJECT _testdecl JOIN(_testdecl, __LINE__)(TESTFUNCNAME, __FILE__ ":" STR(__LINE__), _test_maybeptr(__VA_ARGS__)); \
	static void TESTFUNCNAME()
#define assert_ret(x, ret) do { if (!(x)) { _testfail("\nFailed assertion " #x, __LINE__); return ret; } } while(0)
#define assert(x) assert_ret(x,)
#define assert_msg_ret(x, msg, ret) do { if (!(x)) { _testfail((string)"\nFailed assertion " #x ": "+msg, __LINE__); return ret; } } while(0)
#define assert_msg(x, msg) assert_msg_ret(x,msg,)
#define assert_eq_ret(actual,expected,ret) do { \
		_assert_eq(actual, #actual, expected, #expected, __LINE__); \
		if (_test_result) return ret; \
	} while(0)
#define assert_eq(actual,expected) assert_eq_ret(actual,expected,)
#define assert_neq_ret(actual,expected,ret) do { \
		_assert_neq(actual, #actual, expected, #expected, __LINE__); \
		if (_test_result) return ret; \
	} while(0)
#define assert_neq(actual,expected) assert_neq_ret(actual,expected,)
#define assert_lt_ret(actual,expected,ret) do { \
		_assert_lt(actual, #actual, expected, #expected, __LINE__); \
		if (_test_result) return ret; \
	} while(0)
#define assert_lt(actual,expected) assert_lt_ret(actual,expected,)
#define assert_lte_ret(actual,expected,ret) do { \
		_assert_lte(actual, #actual, expected, #expected, __LINE__); \
		if (_test_result) return ret; \
	} while(0)
#define assert_lte(actual,expected) assert_lte_ret(actual,expected,)
#define assert_gt_ret(actual,expected,ret) do { \
		_assert_gt(actual, #actual, expected, #expected, __LINE__); \
		if (_test_result) return ret; \
	} while(0)
#define assert_gt(actual,expected) assert_gt_ret(actual,expected,)
#define assert_gte_ret(actual,expected,ret) do { \
		_assert_gte(actual, #actual, expected, #expected, __LINE__); \
		if (_test_result) return ret; \
	} while(0)
#define assert_gte(actual,expected) assert_gte_ret(actual,expected,)
#define assert_range_ret(actual,min,max,ret) do { \
		_assert_range(actual, #actual, min, #min, max, #max, __LINE__); \
		if (_test_result) return ret; \
	} while(0)
#define assert_range(actual,min,max) assert_range_ret(actual,min,max,)
#define assert_fail(msg) do { _testfail((string)"\n"+msg, __LINE__); return; } while(0)
#define assert_fail_nostack(msg) do { _testfail((string)"\n"+msg, -1); return; } while(0)
#define testcall(x) do { _teststack_push(__LINE__); x; _teststack_pop(); if (_test_result) return; } while(0)
#define test_skip(x) do { _test_skip(x); if (_test_result) return; } while(0)
#define test_inconclusive(x) do { _test_inconclusive(x); return; } while(0)

#else

#define test(...) static void MAYBE_UNUSED JOIN(_testfunc_, __LINE__)()
#define assert_ret(x, ret) ((void)(x))
#define assert(x) ((void)(x))
#define assert_msg(x, msg) ((void)(x),(void)(msg))
#define assert_eq_ret(x,y,r) ((void)(x==y))
#define assert_eq(x,y) ((void)(x==y))
#define assert_neq_ret(x,y,r) ((void)(x==y))
#define assert_neq(x,y) ((void)(x==y))
#define assert_lt_ret(x,y,r) ((void)(x<y))
#define assert_lt(x,y) ((void)(x<y))
#define assert_lte_ret(x,y,r) ((void)(x<y))
#define assert_lte(x,y) ((void)(x<y))
#define assert_gt_ret(x,y,r) ((void)(x<y))
#define assert_gt(x,y) ((void)(x<y))
#define assert_gte_ret(x,y,r) ((void)(x<y))
#define assert_gte(x,y) ((void)(x<y))
#define assert_range(x,y,z) ((void)(x<y))
#define testcall(x) x
#define test_skip(x) return
#define test_inconclusive(x) return

#endif
