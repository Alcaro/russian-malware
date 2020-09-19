#define WANT_VALGRIND
#include "os.h"
#include "thread.h"
#include "test.h"

#ifdef _WIN32
#include <windows.h>

bool debug_or_ignore()
{
	if (!IsDebuggerPresent())
		return false;
	DebugBreak();
	return true;
}

bool debug_or_exit()
{
	if (IsDebuggerPresent()) DebugBreak();
	ExitProcess(1);
}

bool debug_or_abort()
{
	DebugBreak();
	FatalExit(1);
	__builtin_unreachable();
}
#endif

#ifdef __unix__
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

//method from https://src.chromium.org/svn/trunk/src/base/debug/debugger_posix.cc
static bool has_debugger()
{
	char buf[4096];
	int fd = open("/proc/self/status", O_RDONLY);
	if (!fd) return false;
	
	ssize_t bytes = read(fd, buf, sizeof(buf)-1);
	close(fd);
	
	if (bytes < 0) return false;
	buf[bytes] = '\0';
	
	const char * tracer = strstr(buf, "TracerPid:\t");
	if (!tracer) return false;
	tracer += strlen("TracerPid:\t");
	
	return (*tracer != '0');
}

bool debug_or_ignore()
{
	if (RUNNING_ON_VALGRIND) VALGRIND_PRINTF_BACKTRACE("debug trace");
	else if (has_debugger()) raise(SIGTRAP);
	else return false;
	return true;
}

bool debug_or_exit()
{
	if (RUNNING_ON_VALGRIND) VALGRIND_PRINTF_BACKTRACE("debug trace");
	else if (has_debugger()) raise(SIGTRAP);
	exit(1);
}

bool debug_or_abort()
{
	if (RUNNING_ON_VALGRIND) VALGRIND_PRINTF_BACKTRACE("debug trace");
	else raise(SIGTRAP);
	abort();
}
#endif

#undef debug_or_print
#include "file.h"
bool debug_or_print(const char * filename, int line)
{
	if (debug_or_ignore()) {} // if we're being debugged, no need for anything else
	else
	{
		static file f;
		static mutex mut;
		synchronized(mut)
		{
			string err = (cstring)"arlib: debug_or_print("+filename+", "+tostring(line)+")\n";
			fputs(err, stderr);
			
			if (!f) f.open(file::exepath()+"/arlib-debug-or-print.log", file::m_replace);
			if (f) f.write(err);
			else fputs("arlib: debug_or_print(): couldn't open debug log", stderr);
		}
	}
	return true;
}
