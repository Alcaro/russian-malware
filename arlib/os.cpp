#define WANT_VALGRIND
#include "os.h"
#include "thread.h"
#include <stdlib.h>
#include <string.h>
#include "test.h"

#ifdef __unix__
#include <dlfcn.h>

//static mutex dylib_lock;
//
//static void* dylib_load_uniq(const char * filename, bool force)
//{
//	synchronized(dylib_lock)
//	{
//		//try loading it normally first, to avoid loading libc twice if not needed
//		//duplicate libcs probably don't work very well
//		void* test = dlopen(filename, RTLD_NOLOAD);
//		if (!test) return dlopen(filename, RTLD_LAZY);
//		
//		dlclose(test);
//#ifdef __linux__
//		if (force)
//			return dlmopen(LM_ID_NEWLM, filename, RTLD_LAZY);
//		else
//#endif
//			return NULL;
//	}
//	return NULL; // unreachable, bug in synchronized() and/or gcc
//}

bool dylib::init(const char * filename)
{
	if (handle) abort();
	//synchronized(dylib_lock)
	{
		handle = dlopen(filename, RTLD_LAZY);
	}
	return handle;
}

//bool dylib::init_uniq(const char * filename)
//{
//	if (handle) abort();
//	handle = dylib_load_uniq(filename, false);
//	return handle;
//}
//
//bool dylib::init_uniq_force(const char * filename)
//{
//	if (handle) abort();
//	handle = dylib_load_uniq(filename, true);
//	return handle;
//}

void* dylib::sym_ptr(const char * name)
{
	if (!handle) return NULL;
	return dlsym(handle, name);
}

void dylib::deinit()
{
	if (handle) dlclose(handle);
	handle = NULL;
}
#endif


#ifdef _WIN32
static mutex dylib_lock;

bool dylib::init(const char * filename)
{
	if (handle) abort();
	
	synchronized(dylib_lock) // two threads racing on SetDllDirectory is bad news
	{
		//if (uniq)
		//{
		//	if (GetModuleHandleEx(0, filename, (HMODULE*)&handle)) return NULL;
		//}
		
		//this is so weird dependencies, for example winpthread-1.dll, can be placed beside the dll where they belong
		char * filename_copy = strdup(filename);
		char * filename_copy_slash = strrchr(filename_copy, '/');
		if (!filename_copy_slash) filename_copy_slash = strrchr(filename_copy, '\0');
		filename_copy_slash[0]='\0';
		SetDllDirectory(filename_copy);
		free(filename_copy);
		
		handle = LoadLibrary(filename);
		SetDllDirectory(NULL);
	}
	return handle;
}

//bool dylib::init_uniq(const char * filename)
//{
//	deinit();
//	handle = dylib_init(filename, true);
//	return handle;
//}
//
//bool dylib::init_uniq_force(const char * filename)
//{
//	return init_uniq(filename);
//}

void* dylib::sym_ptr(const char * name)
{
	if (!handle) return NULL;
	return (void*)GetProcAddress((HMODULE)handle, name);
}

void dylib::deinit()
{
	if (handle) FreeLibrary((HMODULE)handle);
	handle = NULL;
}
#endif

bool dylib::sym_multi(funcptr* out, const char * names)
{
	bool all = true;
	
	while (*names)
	{
		*out = this->sym_func(names);
		if (!*out) all = false;
		
		out++;
		names += strlen(names)+1;
	}
	
	return all;
}



#ifdef _WIN32
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

#undef debug_or_print
#include "file.h"
bool debug_or_print(const char * filename, int line)
{
	if (RUNNING_ON_VALGRIND) VALGRIND_PRINTF_BACKTRACE("debug trace");
	else if (has_debugger()) raise(SIGTRAP);
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



#ifdef _WIN32
// Returns a*b/c, but gives the correct answer if a*b doesn't fit in uint64_t.
// (Still gives wrong answer if a*b/c doesn't fit, or if b*c > UINT64_MAX.)
static uint64_t muldiv64(uint64_t a, uint64_t b, uint64_t c)
{
	// doing it in __int128 would be easier, but that ends up calling __udivti3 which is a waste of time.
	return (a/c*b) + (a%c*b/c);
}

uint64_t time_us_ne()
{
	////this one has an accuracy of 10ms by default
	//ULARGE_INTEGER time;
	//GetSystemTimeAsFileTime((LPFILETIME)&time);
	//return time.QuadPart/10;//this one is in intervals of 100 nanoseconds, for some insane reason. We want microseconds.
	
	static LARGE_INTEGER timer_freq;
	if (!timer_freq.QuadPart) QueryPerformanceFrequency(&timer_freq);
	
	LARGE_INTEGER timer_now;
	QueryPerformanceCounter(&timer_now);
	return muldiv64(timer_now.QuadPart, 1000000, timer_freq.QuadPart);
}
uint64_t time_ms_ne()
{
	return time_us_ne() / 1000;
}

uint64_t time_us()
{
	//this one has an accuracy of 10ms by default
	ULARGE_INTEGER time;
	GetSystemTimeAsFileTime((LPFILETIME)&time);
	// this one is in intervals of 100 nanoseconds, for some insane reason. We want microseconds.
	// also epoch is jan 1 1601, subtract that
	return time.QuadPart/10 - 11644473600000000ULL;
}
uint64_t time_ms()
{
	return time_us() / 1000;
}
#else
#include <time.h>

//these functions calculate n/1000 and n/1000000, respectively
//-O2 optimizes this automatically, but I want -Os on most of the program, only speed-optimizing the hottest spots
//this is one of said hotspots; the size penalty is tiny (4 bytes, 8 for both), and it's about twice as fast
//attribute optimize -O2 makes no difference
static inline uint32_t div1000(uint32_t n)
{
	return 274877907*(uint64_t)n >> 38;
}
static inline uint32_t div1mil(uint32_t n)
{
	return 1125899907*(uint64_t)n >> 50;
}

uint64_t time_us()
{
	struct timespec tp;
	clock_gettime(CLOCK_REALTIME, &tp);
	return (uint64_t)tp.tv_sec*(uint64_t)1000000 + div1000(tp.tv_nsec);
}
uint64_t time_ms()
{
	struct timespec tp;
	clock_gettime(CLOCK_REALTIME, &tp);
	return (uint64_t)tp.tv_sec*(uint64_t)1000 + div1mil(tp.tv_nsec);
}

uint64_t time_us_ne()
{
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp); // CLOCK_MONOTONIC_RAW makes more sense, but MONOTONIC uses vdso and skips the syscall
	return (uint64_t)tp.tv_sec*(uint64_t)1000000 + div1000(tp.tv_nsec);
}
uint64_t time_ms_ne()
{
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return (uint64_t)tp.tv_sec*(uint64_t)1000 + div1mil(tp.tv_nsec);
}
#endif

#ifdef _WIN32
//similar to mktime, but UTC timezone
//from http://stackoverflow.com/a/11324281
time_t timegm(struct tm * t)
/* struct tm to seconds since Unix epoch */
{
    long year;
    time_t result;
#define MONTHSPERYEAR   12      /* months per calendar year */
    static const int cumdays[MONTHSPERYEAR] =
        { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

    /*@ +matchanyintegral @*/
    year = 1900 + t->tm_year + t->tm_mon / MONTHSPERYEAR;
    result = (year - 1970) * 365 + cumdays[t->tm_mon % MONTHSPERYEAR];
    result += (year - 1968) / 4;
    result -= (year - 1900) / 100;
    result += (year - 1600) / 400;
    if ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0) &&
        (t->tm_mon % MONTHSPERYEAR) < 2)
        result--;
    result += t->tm_mday - 1;
    result *= 24;
    result += t->tm_hour;
    result *= 60;
    result += t->tm_min;
    result *= 60;
    result += t->tm_sec;
    if (t->tm_isdst == 1)
        result -= 3600;
    /*@ -matchanyintegral @*/
#undef MONTHSPERYEAR
    return (result);
}
#endif

test("time", "", "time")
{
	uint64_t time_u_ft = (uint64_t)time(NULL)*1000000;
	uint64_t time_u_fm = time_ms()*1000;
	uint64_t time_u_fu = time_us();
	assert_range(time_u_fm, time_u_ft-1100000, time_u_ft+1100000);
	assert_range(time_u_fu, time_u_fm-1100,    time_u_fm+1500);
	
	uint64_t time_une_fm = time_ms_ne()*1000;
	uint64_t time_une_fu = time_us_ne();
	assert_range(time_une_fu, time_une_fm-1100,    time_une_fm+1500);
	
#ifdef _WIN32
	Sleep(50);
#else
	usleep(50000);
#endif
	
	uint64_t time2_u_ft = (uint64_t)time(NULL)*1000000;
	uint64_t time2_u_fm = time_ms()*1000;
	uint64_t time2_u_fu = time_us();
	assert_range(time2_u_fm, time2_u_ft-1100000, time2_u_ft+1100000);
	assert_range(time2_u_fu, time2_u_fm-1100,    time2_u_fm+1500);
	
	uint64_t time2_une_fm = time_ms_ne()*1000;
	uint64_t time2_une_fu = time_us_ne();
	assert_range(time2_une_fu, time2_une_fm-1100,    time2_une_fm+1500);
	
#ifdef __unix__
	assert_range(time2_u_fm-time_u_fm, 40000, 60000);
	assert_range(time2_u_fu-time_u_fu, 40000, 60000);
#else
	assert_range(time2_u_fm-time_u_fm, 40000, 70000); // Windows time is low resolution by default
	assert_range(time2_u_fu-time_u_fu, 40000, 70000);
#endif
	assert_range(time2_une_fm-time_une_fm, 40000, 60000);
	assert_range(time2_une_fu-time_une_fu, 40000, 60000);
}

void not_a_function(); // linker error if the uses aren't optimized out
DECL_DYLIB_T(libc_t, not_a_function, fread, isalpha, mktime);
DECL_DYLIB_PREFIX_T(libc_f_t, f, open, read, close);

test("dylib", "", "dylib")
{
	libc_t libc;
	libc_f_t f;
	assert(!libc.fread);
#ifdef __linux__
	const char * libc_so = "libc.so.6";
#endif
#ifdef _WIN32
	const char * libc_so = "msvcrt.dll";
#endif
	assert(!libc.init(libc_so));
	assert(f.init(libc_so));
	assert(libc.fread); // these guys must exist, despite not_a_function failing
	assert(libc.isalpha);
	assert(libc.mktime);
	assert(libc.isalpha('a'));
	assert(!libc.isalpha('1'));
	assert(!libc.not_a_function);
	assert(f.read == libc.fread);
}
