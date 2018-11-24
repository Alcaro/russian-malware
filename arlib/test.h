#pragma once
#include "global.h"
#include "stringconv.h"
#include "linq.h"

#undef assert

#ifdef ARLIB_TEST

template<typename T>
string tostring_dbg(const T& item) { return tostring(item); }
template<typename T>
string tostring_dbg(const arrayview<T>& item)
{
	return item.join((string)",", [](const T& i){ return tostring_dbg(i); });
}
template<typename T> string tostring_dbg(const arrayvieww<T>& item) { return tostring_dbg((arrayview<T>)item); }
template<typename T> string tostring_dbg(const array<T>& item) { return tostring_dbg((arrayview<T>)item); }
template<typename Tkey, typename Tvalue>
string tostring_dbg(const map<Tkey,Tvalue>& item)
{
	return "{"+
		item
			.select([](const typename map<Tkey,Tvalue>::node& n){ return tostring_dbg(n.key)+" => "+tostring_dbg(n.value); })
			.as_array()
			.join(", ")
		+"}";
}

/*
template<typename T> using has_to_string = decltype(declval<T&>().to_string());
	template<typename T> using has_to_string_free = decltype(to_string(declval<T&>()));

	template<typename T>
	string convert(T const& t)
	{
		if constexpr(is_detected_exact_v<string, has_to_string, T>){
			return t.to_string();
		}else if constexpr(is_detected_exact_v<string, has_to_string_free, T>){
			return to_string(t);
		}else if constexpr(is_static_castable_v<T, string>){
			return static_cast<string>(t);
		}
		
		//give up no valid conversion exists, so dump the bytes and maybe that helps debug
		return dump_bytes(t);
	}
*/

class _testdecl {
public:
	_testdecl(void(*func)(), const char * filename, int line, const char * name, const char * requires, const char * provides);
};

void _testfail(cstring why, int line);
void _testcmpfail(cstring why, int line, cstring expected, cstring actual);
void _test_nothrow(int add);

void _teststack_push(int line);
void _teststack_pop();
int _teststack_pushstr(string text); // Returns 1.
int _teststack_popstr(); // Returns 0.

void _test_skip(cstring why);
void _test_skip_force(cstring why);
void _test_inconclusive(cstring why);
void _test_expfail(cstring why);

void _test_runloop_latency(uint64_t us);

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
		_testcmpfail((string)actual_exp+" == "+expected_exp, line, tostring_dbg(expected), tostring_dbg(actual));
	}
}

template<typename T, typename T2>
void _assert_neq(const T&  actual,   const char * actual_exp,
                 const T2& expected, const char * expected_exp,
                 int line)
{
	if (!!_test_eq(actual, expected)) // a!=b implemented as !(a==b)
	{
		_testcmpfail((string)actual_exp+" != "+expected_exp, line, tostring_dbg(expected), tostring_dbg(actual));
	}
}

template<typename T, typename T2>
void _assert_lt(const T&  actual,   const char * actual_exp,
                const T2& expected, const char * expected_exp,
                int line)
{
	if (!_test_lt(actual, expected))
	{
		_testcmpfail((string)actual_exp+" < "+expected_exp, line, tostring_dbg(expected), tostring_dbg(actual));
	}
}

template<typename T, typename T2>
void _assert_lte(const T&  actual,   const char * actual_exp,
                 const T2& expected, const char * expected_exp,
                 int line)
{
	if (!!_test_lt(expected, actual)) // a<=b implemented as !(b<a)
	{
		_testcmpfail((string)actual_exp+" <= "+expected_exp, line, tostring_dbg(expected), tostring_dbg(actual));
	}
}

template<typename T, typename T2>
void _assert_gt(const T&  actual,   const char * actual_exp,
                const T2& expected, const char * expected_exp,
                int line)
{
	if (!_test_lt(expected, actual)) // a>b implemented as b<a
	{
		_testcmpfail((string)actual_exp+" > "+expected_exp, line, tostring_dbg(expected), tostring_dbg(actual));
	}
}

template<typename T, typename T2>
void _assert_gte(const T&  actual,   const char * actual_exp,
                 const T2& expected, const char * expected_exp,
                 int line)
{
	if (!!_test_lt(actual, expected)) // a>=b implemented as !(a<b)
	{
		_testcmpfail((string)actual_exp+" >= "+expected_exp, line, tostring_dbg(expected), tostring_dbg(actual));
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
		_testcmpfail((string)actual_exp+" in ["+min_exp+".."+max_exp+"]", line,
		             "["+tostring_dbg(min)+".."+tostring_dbg(max)+"]", tostring_dbg(actual));
	}
}

#define _test_return(...) if (_test_should_exit()) return __VA_ARGS__;
#define TESTFUNCNAME JOIN(_testfunc, __LINE__)
//'name' is printed to the user, but does nothing.
//'provides' is what feature this test is for.
//'requires' is which features this test requires to function correctly, comma separated;
// if not set correctly, this test could be blamed for an underlying fault.
//If multiple tests provide the same feature, all of them must run before anything depending on it can run
// (however, the test will run even if the prior one fails).
//Requiring a feature that no test provides, or cyclical dependencies, causes a test failure.
#define test(name, requires, provides) \
	static void TESTFUNCNAME(); \
	static KEEP_OBJECT _testdecl JOIN(_testdecl, __LINE__)(TESTFUNCNAME, __FILE__, __LINE__, name, requires, provides); \
	static void TESTFUNCNAME()
#define assert_ret(x, ret) do { if (!(x)) { _testfail("\nFailed assertion " #x, __LINE__); } } while(0)
#define assert(x) assert_ret(x,)
#define assert_msg_ret(x, msg, ret) do { if (!(x)) { _testfail((string)"\nFailed assertion " #x ": "+msg, __LINE__); } } while(0)
#define assert_msg(x, msg) assert_msg_ret(x,msg,)
#define _assert_fn_ret(fn,actual,expected,ret) do { \
		fn(actual, #actual, expected, #expected, __LINE__); \
	} while(0)
#define assert_eq_ret(actual,expected,ret) _assert_fn_ret(_assert_eq,actual,expected,ret)
#define assert_eq(actual,expected) assert_eq_ret(actual,expected,)
#define assert_neq_ret(actual,expected,ret) _assert_fn_ret(_assert_neq,actual,expected,ret)
#define assert_neq(actual,expected) assert_neq_ret(actual,expected,)
#define assert_lt_ret(actual,expected,ret) _assert_fn_ret(_assert_lt,actual,expected,ret)
#define assert_lt(actual,expected) assert_lt_ret(actual,expected,)
#define assert_lte_ret(actual,expected,ret) _assert_fn_ret(_assert_lte,actual,expected,ret)
#define assert_lte(actual,expected) assert_lte_ret(actual,expected,)
#define assert_gt_ret(actual,expected,ret) _assert_fn_ret(_assert_gt,actual,expected,ret)
#define assert_gt(actual,expected) assert_gt_ret(actual,expected,)
#define assert_gte_ret(actual,expected,ret) _assert_fn_ret(_assert_gte,actual,expected,ret)
#define assert_gte(actual,expected) assert_gte_ret(actual,expected,)
#define assert_range_ret(actual,min,max,ret) do { \
		_assert_range(actual, #actual, min, #min, max, #max, __LINE__); \
	} while(0)
#define assert_range(actual,min,max) assert_range_ret(actual,min,max,)
#define assert_fail(msg) do { _testfail((string)"\n"+msg, __LINE__); } while(0)
#define assert_unreachable() do { _testfail("\nassert_unreachable() wasn't unreachable", __LINE__); } while(0)
#define assert_fail_nostack(msg) do { _testfail((string)"\n"+msg, -1); } while(0)
#define testctx(x) for (int i=_teststack_pushstr(x); i; i = _teststack_popstr())
#define testcall(x) do { _teststack_push(__LINE__); x; _teststack_pop(); } while(0)
#define test_skip(x) do { _test_skip(x); } while(0)
#define test_skip_force(x) do { _test_skip_force(x); } while(0)
#define test_inconclusive(x) do { _test_inconclusive(x); } while(0)
#define test_expfail(x) do { _test_expfail(x); } while(0)
#define test_nothrow(x) do { _test_nothrow(+1); x; _test_nothrow(-1); } while(0)

#define main not_quite_main
int not_quite_main(int argc, char** argv);

#else

#define test(...) static void MAYBE_UNUSED JOIN(_testfunc_, __LINE__)()
#define assert_ret(x, ret) ((void)(x))
#define assert(x) ((void)(x))
#define assert_msg(x, msg) ((void)(x),(void)(msg))
#define assert_eq_ret(x,y,r) ((void)((x)==(y)))
#define assert_eq(x,y) ((void)((x)==(y)))
#define assert_neq_ret(x,y,r) ((void)((x)==(y)))
#define assert_neq(x,y) ((void)((x)==(y)))
#define assert_lt_ret(x,y,r) ((void)((x)<(y)))
#define assert_lt(x,y) ((void)((x)<(y)))
#define assert_lte_ret(x,y,r) ((void)((x)<(y)))
#define assert_lte(x,y) ((void)((x)<(y)))
#define assert_gt_ret(x,y,r) ((void)((x)<(y)))
#define assert_gt(x,y) ((void)((x)<(y)))
#define assert_gte_ret(x,y,r) ((void)((x)<(y)))
#define assert_gte(x,y) ((void)((x)<(y)))
#define assert_range(x,y,z) ((void)((x)<(y)))
#define testcall(x) x
#define testctx(x)
#define test_skip(x) return
#define test_skip_force(x) return
#define test_inconclusive(x) return
#define test_expfail(x) return
#define assert_unreachable() return
#define test_nothrow(x) x

#endif

#ifdef ARLIB_SOCKET
class socket; class runloop;
//Ensures that the given socket is usable and speaks HTTP. Socket is not usable afterwards, but caller is responsible for closing it.
void socket_test_http(socket* sock, runloop* loop);
//Ensures the socket does not return an answer if the client tries to speak HTTP.
void socket_test_fail(socket* sock, runloop* loop);
#endif

#ifdef __linux__
# define HAVE_VALGRIND
#endif
#ifdef HAVE_VALGRIND
# include "deps/valgrind/memcheck.h"
#else
# define RUNNING_ON_VALGRIND false
#endif
