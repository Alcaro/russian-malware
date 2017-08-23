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
void _testeqfail(cstring why, int line, cstring expected, cstring actual);

void _teststack_push(int line);
void _teststack_pop();

void _test_skip(cstring why);

template<typename T, typename T2>
void _assert_eq(const T&  actual,   const char * actual_exp,
                const T2& expected, const char * expected_exp,
                int line)
{
	if (actual != expected)
	{
		_testeqfail((string)actual_exp+" == "+expected_exp, line, tostring(expected), tostring(actual)); \
	}
}

//silence sign-comparison warning if lhs is size_t and rhs is integer constant
template<typename T>
void _assert_eq(const T& actual,   const char * actual_exp,
                int      expected, const char * expected_exp,
                int line)
{
	if ((std::is_unsigned<T>::value && expected<0) || (int)actual != expected || actual != (T)expected)
	{
		_testeqfail((string)actual_exp+" == "+expected_exp, line, tostring(expected), tostring(actual)); \
	}
}

//likewise, but for huge constants
template<typename T>
void _assert_eq(const T&  actual,   const char * actual_exp,
                long long expected, const char * expected_exp,
                int line)
{
	if ((std::is_unsigned<T>::value && expected<0) || (long long)actual != expected || actual != (T)expected)
	{
		_testeqfail((string)actual_exp+" == "+expected_exp, line, tostring(expected), tostring(actual)); \
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
#define assert_eq(actual,expected) do { \
		_assert_eq(actual, #actual, expected, #expected, __LINE__); \
		if (_test_result) return; \
	} while(0)
#define assert_fail(msg) do { _testfail((string)"\n"+msg, __LINE__); return; } while(0)
#define assert_fail_nostack(msg) do { _testfail((string)"\n"+msg, -1); return; } while(0)
#define testcall(x) do { _teststack_push(__LINE__); x; _teststack_pop(); if (_test_result) return; } while(0)
#define test_skip(x) do { _test_skip(x); if (_test_result) return; } while(0)
#define main not_quite_main

#else

#define test(...) static void MAYBE_UNUSED JOIN(_testfunc_, __LINE__)()
#define assert(x) ((void)(x))
#define assert_eq(x,y) ((void)(x==y))
#define testcall(x) x
#define test_skip(x) return

#endif
