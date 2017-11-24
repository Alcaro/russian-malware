#ifdef ARLIB_TESTRUNNER
#ifndef ARLIB_TEST
#define ARLIB_TEST
#endif
#include "test.h"
#include "array.h"
#include "os.h"
#include "init.h"
#include "runloop.h"

struct testlist {
	void(*func)();
	const char * loc;
	const char * name;
	const char * requires;
	const char * provides;
	testlist* next;
};

static testlist* g_testlist = NULL;

_testdecl::_testdecl(void(*func)(), const char * loc, const char * name, const char * requires, const char * provides)
{
	testlist* next = malloc(sizeof(testlist));
	next->func = func;
	next->loc = loc;
	next->name = name;
	next->requires = requires;
	next->provides = provides;
	next->next = g_testlist;
	g_testlist = next;
}

static bool all_tests;
static int _test_result; // 0 - pass or still running; 1 - fail; 2 - skipped; 3 - inconclusive

static array<int> callstack;
void _teststack_push(int line) { callstack.append(line); }
void _teststack_pop() { callstack.resize(callstack.size()-1); }
static string stack(int top)
{
	if (top<0) return "";
	
	string ret = " (line "+tostring(top);
	
	for (int i=callstack.size()-1;i>=0;i--)
	{
		ret += ", called from "+tostring(callstack[i]);
	}
	
	return ret+")";
}

static void _testfail(cstring why)
{
	if (_test_result != 1) puts(why.c_str()); // discard multiple failures from same test, they're probably caused by same thing
	if (_test_result != 1) debug_or_ignore();
	_test_result = 1;
}

void _testfail(cstring why, int line)
{
	_testfail(why+stack(line));
}

void _testcmpfail(cstring name, int line, cstring expected, cstring actual)
{
	if (expected.contains("\n") || actual.contains("\n") || name.length()+expected.length()+actual.length()>240)
	{
		_testfail("\nFailed assertion "+name+stack(line)+"\nexpected:\n"+expected+"\nactual:\n"+actual);
	}
	else
	{
		_testfail("\nFailed assertion "+name+stack(line)+": expected "+expected+", got "+actual);
	}
}

void _test_skip(cstring why)
{
	if (!all_tests && !_test_result)
	{
		puts("skipped: "+why);
		_test_result = 2;
	}
}

void _test_inconclusive(cstring why)
{
	if (!_test_result)
	{
		puts("inconclusive: "+why);
		_test_result = 3;
	}
}

bool _test_should_exit()
{
	return _test_result != 0 && arlib_runloop_depth == 0;
}


static void fatal(testlist* err, const char * desc)
{
	printf("test %s (at %s, requires %s, provides %s): %s\n", err->name, err->loc, err->requires, err->provides, desc);
	abort();
}

//whether 'a' must be before 'b'; alternatively, whether 'a' provides something 'b' requires
//false is not conclusive, 'a' could require being before 'c' which is before 'b'
static bool test_requires(testlist* a, testlist* b)
{
	if (!*a->requires) return false;
	//kinda funny code to avoid using Arlib features before they're tested
	//except strtoken, but even in the worst case, I can just revert it or set this to 'return false'
	return strtoken(a->requires, b->provides, ',');
}

//whether 'a' must be before any test in 'list'
//true if 'a' must be before itself
static bool test_requires_any(testlist* a, testlist* list)
{
	while (list)
	{
		if (test_requires(a, list)) return true;
		list = list->next;
	}
	return false;
}

static testlist* sort_tests(testlist* unsorted)
{
	testlist* sorted_head = NULL;
	testlist* * sorted_tail = &sorted_head; // points to where to attach the next test, usually &something->next
	
	while (unsorted)
	{
		bool any_here = false;
		testlist* * try_next_p = &unsorted;
		testlist* try_next = *try_next_p;
		
		while (try_next)
		{
			if (!test_requires_any(try_next, unsorted))
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
			fatal(unsorted, "cyclical dependency");
		}
	}
	
	return sorted_head;
}

#undef main // the real main is #define'd to something stupid on test runs
int main(int argc, char* argv[])
{
	puts("Initializing Arlib...");
//if(argc>1)abort(); // TODO: argument parser
#ifndef ARGUI_NONE
	arlib_init(NULL, argv);
#endif
	
	all_tests = (argc>1);
	bool all_tests_twice = (argc>2);
	
	puts("Sorting tests...");
	testlist* alltests = sort_tests(g_testlist);
	
	//if running Arlib's own tests, this runs before string/array tests
	//don't care, strings/arrays work; worst case, I comment it out
	for (testlist* outer = alltests; outer; outer = outer->next)
	{
		array<cstring> required = cstring(outer->requires).csplit(",");
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
				fatal(outer, "dependency on nonexistent feature");
			}
		}
	}
	
	for (int pass = 0; pass < (all_tests_twice ? 2 : 1); pass++)
	{
		int count[4]={0,0,0};
		
		testlist* test = alltests;
		while (test)
		{
			testlist* next = test->next;
			if (test->name) printf("Testing %s (%s)... ", test->name, test->loc);
			else printf("Testing %s... ", test->loc);
			fflush(stdout);
			_test_result = 0;
			callstack.reset();
			test->func();
			count[_test_result]++;
			if (!_test_result) puts("pass");
			test = next;
		}
		printf("Passed %i, failed %i", count[0], count[1]);
		if (count[2]) printf(", skipped %i", count[2]);
		if (count[3]) printf(", inconclusive %i", count[3]);
		puts("");
		
#ifdef HAVE_VALGRIND
		if (all_tests_twice)
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
	
	return 0;
}

#ifdef ARLIB_TEST_ARLIB
static int testnum = 0;
//funny order to ensure their initializers run in a funny order
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
	testnum = 8;
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

test("x", "", "") { assert(!"use an argument parser"); }
#endif
#endif
