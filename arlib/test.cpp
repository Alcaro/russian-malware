#ifdef ARLIB_TESTRUNNER
#ifndef ARLIB_TEST
#define ARLIB_TEST
#endif
#endif

#include "test.h"

string tostringhex_dbg(const arrayview<uint8_t>& item)
{
	string ret;
	for (size_t outer=0;outer<item.size();outer+=16)
	{
		ret += tostringhex<8>(outer)+"  ";
		for (size_t inner=0;inner<16;inner++)
		{
			if (inner == 8) ret += " ";
			if (outer+inner >= item.size()) ret += "   ";
			else ret += tostringhex<2>(item[outer+inner])+" ";
		}
		ret += " ";
		for (size_t inner=0;inner<16;inner++)
		{
			if (outer+inner >= item.size()) ret += " ";
			else
			{
				char c = item[outer+inner];
				if (c >= 0x20 && c <= 0x7e) ret += c;
				else ret += '.';
			}
		}
		ret += "\n";
	}
	return ret;
}

#ifdef ARLIB_TESTRUNNER
#include "array.h"
#include "os.h"
#include "argparse.h"
#include "runloop2.h"

#ifdef __unix__
#define ESC_ERASE_LINE "\x1B[2K\r"
#else
#define ESC_ERASE_LINE "\r                                                                               \r"
#endif

struct testlist {
	void(*func)();
	
	const char * filename;
	int line;
	
	const char * name;
	
	const char * needs;
	const char * provides;
	testlist* next;
};

static testlist* g_testlist = NULL;

static testlist* cur_test;

// linked lists because this tests Arlib itself, relying on array<> to work when testing array<> is unwise
_testdecl::_testdecl(void(*func)(), const char * filename, int line, const char * name, const char * needs, const char * provides)
{
	testlist* next = xmalloc(sizeof(testlist));
	next->func = func;
	next->filename = filename;
	next->line = line;
	next->name = name;
	next->needs = needs;
	next->provides = provides;
	next->next = g_testlist;
	g_testlist = next;
}

static bool all_tests;

enum err_t {
	err_ok = 0,
	err_fail = 1,
	err_skip = 2,
	err_inconclusive = 3,
	err_expfail = 4,
	err_tooslow = 5,
	err_ntype
};
static err_t result;

static bool show_verbose;

struct stack_entry {
	cstring file;
	int line;
};
static array<stack_entry> callstack;
void _teststack_push(cstring file, int line) { callstack.append({ file, line }); }
void _teststack_pop() { callstack.resize(callstack.size()-1); }

static array<string> ctxstack;
void _teststack_pushstr(string text) { ctxstack.append(text); }
void _teststack_popstr() { ctxstack.resize(ctxstack.size()-1); }

static size_t n_malloc = 0;
static size_t n_free = 0;
static int n_malloc_block = 0;
void _test_malloc()
{
	if (UNLIKELY(n_malloc_block > 0))
	{
		n_malloc_block = 0; // failing usually allocates
		test_fail("can't malloc here");
	}
	n_malloc++;
}
void _test_free() { n_free++; }
void test_nomalloc_begin() { n_malloc_block++; }
void test_nomalloc_end() { n_malloc_block--; }

static string fmt_stackentry(bool verbose, cstring file, int line)
{
	if (file == cur_test->filename) return (verbose?"line ":"")+tostring(line);
	else return file+":"+tostring(line);
}
static string fmt_callstack(cstring file, int top)
{
	if (top<0) return "";
	
	string ret = " ("+fmt_stackentry(true, file, top);
	
	for (int i=callstack.size()-1;i>=0;i--)
	{
		ret += ", called from "+fmt_stackentry(false, callstack[i].file, callstack[i].line);
	}
	
	return ret+")";
}
static string stack(cstring file, int top)
{
	string ret = fmt_callstack(file, top);
	
	for (cstring ctx : ctxstack)
	{
		ret += " ("+ctx+")";
	}
	return ret;
}

int nothrow_level = 0;
static void test_throw(err_t why)
{
	result = why;
	if (nothrow_level <= 0)
	{
		throw why;
	}
}

void _test_nothrow(int add)
{
	nothrow_level += add;
}

static void _testfail(cstrnul why)
{
	n_malloc_block = 0;
	if (result == err_ok || (result==err_skip && !show_verbose))
		puts("");
	if (result != err_fail)
	{
		result = err_fail;
		puts(why);
		fflush(stdout);
		debug_break("test failed");
	}
	test_throw(err_fail);
}

void _testfail(cstring why, cstring file, int line)
{
	n_malloc_block = 0;
	_testfail(why+stack(file, line));
}

void _testcmpfail(cstring name, cstring file, int line, cstring lhs, cstring rhs)
{
	if (lhs || rhs)
		_testfail("Failed assertion "+name+stack(file, line)+"\nlhs: "+lhs+"\nrhs: "+rhs);
	else
		_testfail("Failed assertion "+name+stack(file, line));
}

void _test_skip(cstring why)
{
	if (result != err_ok) return;
	if (!all_tests)
	{
		if (show_verbose) puts("skipped: "+why);
		test_throw(err_skip);
	}
}

void _test_skip_force(cstring why)
{
	if (result != err_ok) return;
	if (show_verbose) puts("skipped: "+why);
	test_throw(err_skip);
}

void _test_inconclusive(cstring why)
{
	if (result != err_ok) return;
	puts("inconclusive: "+why);
	test_throw(err_inconclusive);
}

void _test_expfail(cstring why)
{
	if (result != err_ok) return;
	puts("expected-fail: "+why);
	test_throw(err_expfail);
}

bool test_skipped()
{
	return (result != err_ok);
}

bool test_rethrow()
{
	if (result != err_ok)
	{
		test_throw(result);
		return true;
	}
	return false;
}

namespace {
struct latrec {
	uint64_t us;
	const char * name;
	const char * filename;
	int line;
	
	bool operator<(const latrec& other) const { return us < other.us; }
};

latrec max_latencies_us[6];
}

void _test_runloop_latency(duration dur)
{
	if (!cur_test) return; // happens when global runloop is destructed at process exit
	max_latencies_us[0].us = dur.us();
	max_latencies_us[0].name = cur_test->name;
	max_latencies_us[0].filename = cur_test->filename;
	max_latencies_us[0].line = cur_test->line;
	arrayvieww<latrec>(max_latencies_us).sort();
}


static void err_print(testlist* err)
{
	printf("%s (at %s:%d, needs %s, provides %s)\n", err->name, err->filename, err->line, err->needs, err->provides);
}

//whether 'a' must be before 'b'; alternatively, whether 'a' provides something 'b' needs
//false is not conclusive, 'a' could require being before 'c' which is before 'b'
static bool test_needs(testlist* a, testlist* b)
{
	if (!*a->needs) return false;
	if (!*b->provides) return false;
	// kinda weird to use Arlib features before they're tested
	// but if it screws up, I can just set it to 'return false'
	return string(a->needs).split(",").contains(b->provides);
}

//whether 'a' must be before any test in 'list'
//true if 'a' must be before itself
//actually returns pointer to the preceding test
static testlist* test_needs_any(testlist* a, testlist* list)
{
	while (list)
	{
		if (test_needs(a, list)) return list;
		list = list->next;
	}
	return NULL;
}

static testlist* sort_tests(testlist* unsorted)
{
	testlist* sorted_head = NULL;
	testlist* * sorted_tail = &sorted_head; // points to where to attach the next test, usually &something->next
	
	//ideally, this would pick all tests in a file simultaneously, if possible
	//but that's annoying to implement even with full Arlib, and this thing doesn't assume Arlib is functional
	//for now, they're left separate
	
	while (unsorted)
	{
		bool any_here = false;
		testlist* * try_next_p = &unsorted;
		testlist* try_next = *try_next_p;
		
		while (try_next)
		{
			if (!test_needs_any(try_next, unsorted))
			{
				*try_next_p = try_next->next;
				
				*sorted_tail = try_next;
				sorted_tail = &try_next->next;
				
				try_next->next = NULL;
				
				any_here = true;
				
				try_next = *try_next_p;
			}
			else
			{
				try_next_p = &try_next->next;
				try_next = *try_next_p;
			}
		}
		if (!any_here)
		{
			puts("error: cyclical dependency");
			
			testlist* a = unsorted;
			testlist* b = unsorted;
			do
			{
				a = test_needs_any(a, unsorted);
				b = test_needs_any(b, unsorted);
				b = test_needs_any(b, unsorted);
			}
			while (a != b);
			do
			{
				err_print(a);
				a = test_needs_any(a, unsorted);
			}
			while (a != b);
			
			abort();
		}
	}
	
	return sorted_head;
}

static testlist* list_reverse(testlist* list)
{
	testlist* ret = NULL;
	while (list)
	{
		testlist* next = list->next;
		
		list->next = ret;
		ret = list;
		
		list = next;
	}
	return ret;
}

#ifdef __linux__
#include <sys/time.h>
#include <sys/resource.h>
#endif

#undef main // the real main is #define'd to something else on test runs
int main(int argc, char* argv[])
{
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("Initializing Arlib...");
	bool run_twice = false;
	
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
	struct rlimit rlim_as = { 4096ull*1024*1024, RLIM_INFINITY };
	struct rlimit rlim_data = { 512ull*1024*1024, RLIM_INFINITY };
	setrlimit(RLIMIT_AS, &rlim_as);
	setrlimit(RLIMIT_DATA, &rlim_data);
#endif
	
	int n_filtered_tests = 0;
	
#if 1 // set to 0 if string or array is misbehaving
	argparse args;
	
	string filter;
	args.add("all", &all_tests);
	args.add("twice", &run_twice);
	args.add("filter", &filter);
	args.parse(argv);
#ifdef ARLIB_GUI
	void arlib_init();
	arlib_init();
#endif
	
	printf(ESC_ERASE_LINE "Sorting tests...");
	g_testlist = list_reverse(g_testlist); // with gcc's initializer run order, this makes them better ordered
	
	testlist* alltests = sort_tests(g_testlist);
	
	for (testlist* outer = alltests; outer; outer = outer->next)
	{
		if (outer->needs[0] == '\0') continue;
		array<cstring> required = cstring(outer->needs).csplit(",");
		for (size_t i=0;i<required.size();i++)
		{
			bool found = false;
			for (testlist* inner = alltests; inner; inner = inner->next)
			{
				if (required[i] == inner->provides)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				puts("error: dependency on nonexistent feature "+required[i]);
				err_print(outer);
			}
		}
	}
	
	if (filter)
	{
		testlist** tp = &alltests;
		while (*tp)
		{
			testlist* t = *tp;
			cstring tn = t->name;
			if (!tn.icontains(filter))
			{
				*tp = t->next; // discard test
				n_filtered_tests++;
				free(t);
			}
			else
			{
				tp = &t->next;
			}
		}
	}
	g_testlist = alltests;
#else
	arlib_init(NULL, argv);
	testlist* alltests = g_testlist;
#endif
	
	int numtests = 0;
	testlist* numtests_iter = alltests;
	while (numtests_iter)
	{
		numtests++;
		numtests_iter = numtests_iter->next;
	}
	printf(ESC_ERASE_LINE);
	
	show_verbose = (all_tests || numtests < 8);
	for (int pass = 0; pass < (run_twice ? 2 : 1); pass++)
	{
		int count[err_ntype]={0};
		
		memset(max_latencies_us, 0, sizeof(max_latencies_us));
		
		int testnum = 0;
		
		cur_test = alltests;
		while (cur_test)
		{
			testnum++;
			testlist* next = cur_test->next;
			
			if (show_verbose)
				printf("Testing %s (%s:%d)... ", cur_test->name, cur_test->filename, cur_test->line);
			else
				printf(ESC_ERASE_LINE "Test %d/%d (%s)... ", testnum, numtests, cur_test->name);
			fflush(stdout);
			result = err_ok;
			callstack.reset();
			ctxstack.reset();
			nothrow_level = 0;
			n_malloc = 0;
			n_free = 0;
			n_malloc_block = 0;
			try {
				timer t;
				runloop2::test_begin();
				cur_test->func();
				runloop2::test_end();
				if (pass == 1)
					assert_eq(n_malloc, n_free);
				uint64_t time_us = t.us();
				uint64_t time_lim = (all_tests ? 5000*1000 : 500*1000);
				assert_eq(n_malloc_block, 0);
				if (time_us > time_lim)
				{
					printf("too slow: max %uus, got %uus\n", (unsigned)time_lim, (unsigned)time_us);
					throw err_tooslow;
				}
			} catch (err_t e) {
				result = e;
			}
			
			if (show_verbose && result == err_ok) puts("pass");
			count[result]++;
			cur_test = next;
		}
		
		printf(ESC_ERASE_LINE);
		for (size_t i=ARRAY_SIZE(max_latencies_us)-1;i;i--)
		{
			uint64_t max_latency_us = (RUNNING_ON_VALGRIND ? 100 : 3) * 1000;
			if (max_latencies_us[i].us > max_latency_us || i == ARRAY_SIZE(max_latencies_us)-1)
			{
				if (!max_latencies_us[i].name) continue; // happens if the runloop is never used
				printf("Latency %luus at %s (%s:%d)\n",
				       (unsigned long)max_latencies_us[i].us,
				       max_latencies_us[i].name,
				       max_latencies_us[i].filename,
				       max_latencies_us[i].line);
			}
		}
		
		printf("Passed %d, failed %d", count[err_ok], count[err_fail]);
		if (count[err_tooslow]) printf(", too-slow %d", count[err_tooslow]);
		if (count[err_skip]) printf(", skipped %d", count[err_skip]);
		if (count[err_inconclusive]) printf(", inconclusive %d", count[err_inconclusive]);
		if (count[err_expfail]) printf(", expected-fail %d", count[err_expfail]);
		if (n_filtered_tests) printf(", filtered %d", n_filtered_tests);
		puts("");
		
#ifdef HAVE_VALGRIND
		if (run_twice)
		{
			if (pass==0)
			{
				//discard everything leaked in first pass, it's probably gtk+ setup and whatever
				VALGRIND_DISABLE_ERROR_REPORTING;
				VALGRIND_DO_LEAK_CHECK;
				VALGRIND_ENABLE_ERROR_REPORTING;
			}
			else
			{
				VALGRIND_DO_CHANGED_LEAK_CHECK;
			}
		}
#endif
	}
	
	testlist* free_them = g_testlist;
	while (free_them)
	{
		testlist* next = free_them->next;
		free(free_them);
		free_them = next;
	}
	
	return 0;
}

#undef free
void free_test(void* ptr) { _test_free(); free(ptr); }

void _test_run_coro(async<void> inner)
{
	waiter<void> wait_co;
	waiter<void> wait_time;
	inner.then(&wait_co);
	runloop2::await_timeout(timestamp::now()+duration::ms(10000)).then(&wait_time);
	while (wait_co.is_waiting() && wait_time.is_waiting())
		runloop2::step();
	assert(wait_time.is_waiting()); // timeout means failure
}

void _test_coro_exception()
{
	test_rethrow();
	assert_unreachable();
}

#ifdef ARLIB_TEST_ARLIB
static int testnum = 0;
//install them in a weird order, to ensure the sorter works
test("tests themselves (1/8)", "", "test1")
{
	assert_eq(testnum, 0);
	testnum = 1;
}
test("tests themselves (2/8)", "test1", "test2")
{
	assert_eq(testnum, 1);
	testnum = 2;
}
test("tests themselves (8/8)", "test6,test7", "test8")
{
	assert_eq(testnum, 7);
	testnum = 0; // for test-all-twice
}
test("tests themselves (7/8)", "test6", "test7")
{
	assert_eq(testnum, 6);
	testnum = 7;
}
test("tests themselves (6/8)", "test5", "test6")
{
	assert_eq(testnum, 5);
	testnum = 6;
}
test("tests themselves (5/8)", "test4", "test5")
{
	assert_eq(testnum, 4);
	testnum = 5;
}
test("tests themselves (3/8)", "test2", "test3")
{
	assert_eq(testnum, 2);
	testnum = 3;
}
test("tests themselves (4/8)", "test2,test3", "test4")
{
	assert_eq(testnum, 3);
	testnum = 4;
}

/*
template<typename T> class test_wrapper;
template<typename T>
inline void assert_bottom(const char * text, const char * file, int line, test_wrapper<T> var)
{
	if ((var.root && !var.val) || !var.success)
		_testcmpfail(text, file, line, var.lhs, var.rhs);
}

template<>
class test_wrapper<void> {
public:
	template<typename T>
	test_wrapper<T> operator<<(T other)
	{
		return test_wrapper<T>(other);
	}
};

template<typename T>
class test_wrapper {
	template<typename T2> friend class test_wrapper;
	template<typename T2> friend inline void assert_bottom(const char * text, const char * file, int line, test_wrapper<T2> var);
	
	T val;
	bool success;
	bool root;
	string lhs;
	string rhs;
	
	test_wrapper(T val) : val(val), success(true), root(true) {}
	
	template<typename T2>
	test_wrapper(test_wrapper<T2>* prev, bool success, auto lhs, T rhs) : val(rhs), success(success), root(false)
	{
		if (prev && !prev->success)
		{
			this->success = false;
			this->lhs = prev->lhs;
			this->rhs = prev->rhs;
		}
		else if (!success)
		{
			this->lhs = tostring_dbg(lhs);
			this->rhs = tostring_dbg(rhs);
		}
	}
	
public:
	template<typename T2> test_wrapper<T2> operator==(const T2& other) { return { this, val == other, val, other }; }
	template<typename T2> test_wrapper<T2> operator!=(const T2& other) { return { this, val != other, val, other }; }
	template<typename T2> test_wrapper<T2> operator<(const T2& other) { return { this, val < other, val, other }; }
	template<typename T2> test_wrapper<T2> operator>(const T2& other) { return { this, val > other, val, other }; }
	template<typename T2> test_wrapper<T2> operator<=(const T2& other) { return { this, val <= other, val, other }; }
	template<typename T2> test_wrapper<T2> operator>=(const T2& other) { return { this, val >= other, val, other }; }
};

#define assert2(...) assert_bottom(#__VA_ARGS__, __FILE__, __LINE__, (test_wrapper<void>() << __VA_ARGS__))
test("xtest1", "", "") { assert2(true); }
test("xtest2", "", "") { assert2(false); }
test("xtest3", "", "") { assert2(2+2 == 5); }
test("xtest4", "", "") { assert2(1 < 2 < 3); }
test("xtest5", "", "") { assert2(1 < 0 < 3); }
test("xtest6", "", "") { assert2(1 < 4 < 3); }
*/
#endif
#endif
