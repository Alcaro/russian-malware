#include "thread.h"
#if defined(_WIN32) && defined(ARLIB_THREAD)
#include <process.h>

//list of synchronization points: http://msdn.microsoft.com/en-us/library/windows/desktop/ms686355%28v=vs.85%29.aspx

// don't dllimport, mingw and msvcrt have different fpreset
// mingw sets fpcw to 0x037f (do all calculations in float80), msvcrt 0x027f (all in float64)
extern "C" void _fpreset();

struct threaddata_win32 {
	function<void()> func;
};
static unsigned __stdcall threadproc(void* userdata)
{
	threaddata_win32* thdat = (threaddata_win32*)userdata;
	// otherwise 1.0 + 1.1102230246251568e-16 gets the wrong answer
	// https://blog.zaita.com/mingw64-compiler-bug/
	// (arguably, the real bug is that msvc and mingw prefer different fpcw... sucks if both exe and a dll expect different float behavior)
#ifdef __i386__ // on x86_64, everything runs in float80, and _fpreset does nothing
	_fpreset();
#endif
	thdat->func();
	delete thdat;
	return 0;
}
void thread_create(function<void()>&& start, priority_t pri)
{
	// https://docs.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread
	//  A thread in an executable that calls the C run-time library (CRT) should use the _beginthreadex
	//  and _endthreadex functions for thread management rather than CreateThread and ExitThread
	// no clue what it actually does, but better obey
	threaddata_win32* thdat = new threaddata_win32;
	thdat->func = std::move(start);
	
	HANDLE h = (HANDLE)_beginthreadex(NULL, 0, threadproc, thdat, 0, NULL);
	if (!h) abort();
	static const int8_t prios[] = {
		THREAD_PRIORITY_NORMAL,
		THREAD_PRIORITY_ABOVE_NORMAL,
		THREAD_PRIORITY_BELOW_NORMAL,
		THREAD_PRIORITY_IDLE
	};
	SetThreadPriority(h, prios[pri]);
	CloseHandle(h);
}

unsigned int thread_num_cores()
{
	SYSTEM_INFO sysinf;
	GetSystemInfo(&sysinf);
	return sysinf.dwNumberOfProcessors;
}

unsigned int thread_num_cores_idle()
{
	// this function should return physical core count, or cores minus 1 if no hyperthreading,
	//  but there doesn't seem to be an easy way to get number of real cores
	// so this is good enough
	return (thread_num_cores()+1)/2;
}
#endif
