#ifdef ARLIB_TESTRUNNER
#ifndef ARLIB_TEST
#define ARLIB_TEST
#endif
#include "test.h"
#include "array.h"
#include "os.h"
#include "init.h"

#ifdef __linux__
#define HAVE_VALGRIND
#endif
#ifdef HAVE_VALGRIND
#include "deps/valgrind/memcheck.h"
#endif

struct testlist {
	void(*func)();
	const char * loc;
	const char * name;
	testlist* next;
};

static testlist* g_testlist;

_testdecl::_testdecl(void(*func)(), const char * loc, const char * name)
{
	testlist* next = malloc(sizeof(testlist));
	next->func = func;
	next->loc = loc;
	next->name = name;
	next->next = g_testlist;
	g_testlist = next;
}

static bool all_tests;
int _test_result; // 0 - pass or still running; 1 - fail; 2 - skipped

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
	if (_test_result == 0) puts(why.c_str()); // discard multiple failures from same test, they're probably caused by same thing
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

void _test_skip_force(cstring why)
{
	if (!_test_result)
	{
		puts("skipped: "+why);
		_test_result = 2;
	}
}

#undef main // the real main is #define'd to something stupid on test runs
int main(int argc, char* argv[])
{
if(argc>1)abort(); // TODO: argument parser
#ifndef ARGUI_NONE
	arlib_init(NULL, argv);
#endif
	
	all_tests = (argc>1);
	bool all_tests_twice = (argc>2);
	
	//flip list backwards
	//order of static initializers is implementation defined, but this makes output better under gcc
	testlist* test = g_testlist;
	g_testlist = NULL;
	while (test)
	{
		testlist* next = test->next;
		test->next = g_testlist;
		g_testlist = test;
		test = next;
	}
	
	for (int pass = 0; pass < (all_tests_twice ? 2 : 1); pass++)
	{
		int count[3]={0,0,0};
		
		test = g_testlist;
		while (test)
		{
			testlist* next = test->next;
			if (true)
			{
				if (test->name) printf("Testing %s (%s)... ", test->name, test->loc);
				else printf("Testing %s... ", test->loc);
				fflush(stdout);
				_test_result = 0;
				callstack.reset();
				test->func();
				count[_test_result]++;
				if (!_test_result) puts("pass");
			}
			else
			{
				if (test->name) printf("Skipping %s (%s)\n", test->name, test->loc);
				else printf("Skipping %s\n", test->loc);
				count[2]++;
			}
			test = next;
		}
		printf("Passed %i, failed %i", count[0], count[1]);
		if (count[2]) printf(", skipped %i", count[2]);
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
test() {}
test() {}
#endif
#endif
