#define WANT_VALGRIND
#include "os.h"
#include "thread.h"
#include "test.h"
#include "file.h"

static FILE* log_file = NULL;
static mutex log_mut;
static int log_count = 0;

static void debug_log_raw(const char * text) // log_mut must be locked when calling this
{
	fputs(text, stderr);
	if (!log_file) log_file = fopen("arlib-debug.log", "at"); // probably wrong directory, but calling malloc here can recurse
	if (log_file) { fputs(text, log_file); fflush(log_file); }
}

#ifdef __unix__
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

//method from https://src.chromium.org/viewvc/chrome/trunk/src/base/debug/debugger_posix.cc
static bool has_debugger()
{
	char buf[4096];
	int fd = open("/proc/self/status", O_RDONLY);
	if (fd < 0) return false;
	
	ssize_t bytes = read(fd, buf, sizeof(buf)-1);
	close(fd);
	
	if (bytes < 0) return false;
	buf[bytes] = '\0';
	
	const char * tracer = strstr(buf, "TracerPid:\t");
	if (!tracer) return false;
	tracer += strlen("TracerPid:\t");
	
	return (*tracer != '0');
}

bool debug_break(const char * text)
{
	if (RUNNING_ON_VALGRIND) VALGRIND_PRINTF_BACKTRACE("%s", text);
	else if (has_debugger()) raise(SIGTRAP);
	else return false;
	return true;
}


#include <execinfo.h>
#include <dlfcn.h>
void debug_log_stack(const char * text)
{
	synchronized(log_mut)
	{
		if (log_count > 20) return;
		log_count++;
		
		debug_log_raw(text);
		
		void* addrs[20];
		int n_addrs = backtrace(addrs, ARRAY_SIZE(addrs));
		
		//*
		backtrace_symbols_fd(addrs, n_addrs, STDERR_FILENO);
		backtrace_symbols_fd(addrs, n_addrs, fileno(log_file));
		/*/
		// alternative variant that always prints offset from the start of the executable
		auto debug_log_sym = [](const char * fmt, void* ptr, const char * symname, void* sym_at)
		{
			size_t off = (char*)ptr - (char*)sym_at;
			fprintf(stderr, fmt, symname, off);
			if (log_file) fprintf(log_file, fmt, symname, off);
		};
		for (int i=0;i<n_addrs;i++)
		{
			Dl_info inf;
			dladdr(addrs[i], &inf);
			
			if (inf.dli_fname)
			{
				debug_log_sym("%s+0x%zx", addrs[i], inf.dli_fname, inf.dli_fbase);
				if (inf.dli_sname)
					debug_log_sym(" (%s+0x%zx)", addrs[i], inf.dli_sname, inf.dli_saddr);
			}
			else
			{
				fprintf(stderr, "%p", addrs[i]);
				if (log_file) fprintf(log_file, "%p", addrs[i]);
			}
			debug_log_raw("\n");
		}
		// */
	}
}
#endif

#ifdef _WIN32
#include <windows.h>

bool debug_break(const char * text)
{
	if (IsDebuggerPresent()) DebugBreak();
	else return false;
	return true;
}

void debug_log_stack(const char * text)
{
	synchronized(log_mut)
	{
		if (log_count > 20) return;
		log_count++;
		
		debug_log_raw(text);
		
		void* addrs[20];
		int n_addrs = CaptureStackBackTrace(0, ARRAY_SIZE(addrs), addrs, NULL);
		
		for (int i=0;i<n_addrs;i++)
		{
			HMODULE mod = NULL;
			GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			                   (char*)addrs[i], &mod);
			if (mod)
			{
				char name[PATH_MAX];
				// 80% of stack frames will be same module as last one
				// but no point optimizing, this codepath is cold
				GetModuleFileNameA(mod, name, sizeof(name));
				char * nameend = name;
				for (char * iter = name; *iter; iter++)
				{
					if (*iter == '/' || *iter == '\\') nameend = iter+1;
				}
				
				// truncate to 32 bits, easier than figuring out which of %llx, %zx, %I64x, etc work on this windows edition
				uint32_t off = (char*)addrs[i] - (char*)mod;
				fprintf(stderr, "%s+0x%x\n", nameend, off);
				if (log_file) fprintf(log_file, "%s+0x%x\n", nameend, off);
			}
			else
			{
				fprintf(stderr, "%p\n", addrs[i]);
				if (log_file) fprintf(log_file, "%p\n", addrs[i]);
			}
		}
	}
}
#endif

void debug_log(const char * text)
{
	synchronized(log_mut) { 
		if (log_count > 20) return;
		log_count++;
		
		debug_log_raw(text);
	}
}

void debug_warn(const char * text) { if (!debug_break(text)) debug_log(text); }
void debug_fatal(const char * text) { if (!debug_break(text)) { debug_log(text); } abort(); }
void debug_warn_stack(const char * text) { if (!debug_break(text)) debug_log_stack(text); }
void debug_fatal_stack(const char * text) { if (!debug_break(text)) debug_log_stack(text); abort(); }
