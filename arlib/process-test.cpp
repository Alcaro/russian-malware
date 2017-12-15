#include "process.h"
#include "test.h"

#ifdef ARLIB_THREAD
#ifndef _WIN32
#include <unistd.h> // usleep
#define TRUE "/bin/true"
#define ECHO "/bin/echo"
#define YES "/usr/bin/yes"
#define LF "\n"
#define ECHO_END LF
#define CAT_FILE "/bin/cat"
#define CAT_STDIN "/bin/cat"
#define CAT_STDIN_END ""
#else
#undef TRUE // go away windows, true !== 1 is just stupid. and so is TRUE
#define TRUE "cmd", "/c", "type NUL" // windows has no /bin/true, fake it
#define ECHO "cmd", "/c", "echo"
#define YES "cmd", "/c", "tree /f c:" // not actually infinite, but close enough
#define LF "\r\n"
#define ECHO_END LF
#define CAT_FILE "cmd", "/c", "type"
#define CAT_STDIN "find", "/v", "\"COPY_THE_INPUT_UNCHANGED\""
#define CAT_STDIN_END LF
#define usleep(n) Sleep((n)/1000)
#endif

test("process", "array,string,runloop", "process")
{
	test_skip("kinda slow");
	//there are a couple of race conditions here, but I believe none of them will cause trouble
	{
		process p;
		assert(p.launch(TRUE));
		assert_eq(p.wait(), 0);
	}
	
	{
		process p;
		process::output* out = p.set_stdout(process::output::create_buffer());
		assert(p.launch(ECHO, "foo"));
		int status = p.wait();
		assert_eq(out->read(), "foo" ECHO_END);
		assert_eq(status, 0);
	}
	
	{
		process p;
		p.set_stdin(process::input::create_buffer("foo"));
		process::output* out = p.set_stdout(process::output::create_buffer());
		assert(p.launch(CAT_STDIN));
		p.wait();
		assert_eq(out->read(), "foo" CAT_STDIN_END);
	}
	
	{
		process p;
		process::input* in = p.set_stdin(process::input::create_pipe());
		process::output* out = p.set_stdout(process::output::create_buffer());
		assert(p.launch(CAT_STDIN));
		in->write("foo");
		in->close();
		p.wait();
		usleep(100000);
		assert_eq(out->read(), "foo" CAT_STDIN_END);
	}
	
	{
		process p;
		process::input* in = p.set_stdin(process::input::create_pipe());
		process::output* out = p.set_stdout(process::output::create_buffer());
		assert(p.launch(CAT_STDIN));
		for (int i=0;i<5;i++)
		{
			in->write("foo" LF);
			string str;
			while (!str) str = out->read();
			assert_eq(str, "foo" LF);
		}
	}
	
	{
		process p;
		process::output* out = p.set_stdout(process::output::create_buffer());
		process::output* err = p.set_stderr(process::output::create_buffer());
		assert(p.launch(CAT_FILE, "nonexist.ent"));
		p.wait();
		assert_eq(out->read(), "");
		assert(err->read() != "");
	}
	
	{
		process p;
		process::output* out = p.set_stdout(process::output::create_buffer());
		out->limit(1024);
		assert(p.launch(YES));
		p.wait();
		string outstr = out->read();
		assert(outstr.length() >= 1024); // it can read a bit more than 1K if it wants to, buffer size is 4KB
		assert(outstr.length() <= 8192); // on windows, limit is honored exactly
	}
	
	{
		process p;
		process::output* out = p.set_stdout(process::output::create_buffer());
		assert(p.launch(ECHO, "foo"));
		assert_eq(out->read(), ""); // RACE
	}
	
	{
		string lots_of_data = "a" LF;
		while (lots_of_data.length() < 256*1024) lots_of_data += lots_of_data;
		
		process p;
		p.set_stdin(process::input::create_buffer(lots_of_data));
		process::output* out = p.set_stdout(process::output::create_buffer());
		
		assert(p.launch(CAT_STDIN));
		p.wait();
		assert_eq(out->read().length(), lots_of_data.length());
	}
	
	{
		process p;
		process::input* in = p.set_stdin(process::input::create_pipe());
		process::output* out = p.set_stdout(process::output::create_buffer());
		
		assert(p.launch(CAT_STDIN));
		in->write("foo" LF);
		usleep(50*1000); // RACE
		assert_eq(out->read(), "foo" LF);
		assert(p.running());
		in->close();
		usleep(50*1000); // RACE (this gets interrupted by SIGCHLD, but it's resumed)
		usleep(50*1000); // oddly enough, I need two usleeps to make sure it works
		assert(!p.running());
	}
	
	//no real way to test this
	//{
	//	process p;
	//	p.set_stdout(process::output::create_stdout());
	//	assert(p.launch(ECHO, "test"));
	//	p.wait();
	//}
	
	//{
	//	string test_escape[] = {
	//		"DUMMY_NODE",
	//		"DUMMY_NODE",
	//		"a",
	//		"\"",
	//		"a b",
	//		"\"a b\"",
	//		" ",
	//		" a",
	//		"a ",
	//		"  ",
	//		" \" ",
	//		" \"\" ",
	//		" \" \"",
	//		"",
	//		"\"",
	//		"\\",
	//		"\\\"",
	//		"\\\\",
	//		"\\\\\"",
	//		"\\\\\\",
	//		"\\\\\\\"",
	//	};
	//	//this one is supposed to test that the arguments are properly quoted,
	//	// but there's no 'dump argv' program on windows (linux doesn't need it), so can't do it
	//	//and windows has about 50 different quote parsers anyways, impossible to know which to follow
	//}
}

#ifdef ARLIB_SANDBOX
#include "sandbox/sandbox.h"

test("sandbox", "process", "sandbox")
{
	//test_skip("kinda slow");
	
	if (RUNNING_ON_VALGRIND) test_inconclusive("valgrind doesn't understand the sandbox");
	
	{
		sandproc p;
		bool has_access_fail = false;
		// this will fail because can't access /lib64/ld-linux-x86-64.so.2
		// (or /bin/true or whatever - no point caring exactly what file makes it blow up)
		p.set_access_violation_cb(bind_lambda([&](cstring path, bool write) { has_access_fail = true; } ));
		
		assert(p.launch(TRUE));
		assert_neq(p.wait(), 0);
		assert(has_access_fail);
	}
	
	{
		sandproc p;
		p.set_access_violation_cb(bind_lambda([&](cstring path, bool write) { assert_unreachable(); } ));
		p.fs_grant_syslibs(TRUE);
		
		assert(p.launch(TRUE));
		assert_eq(p.wait(), 0);
	}
}
#endif
#endif
