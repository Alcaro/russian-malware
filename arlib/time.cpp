#include "os.h"
#include "test.h"

#ifdef _WIN32
#include <windows.h>
// Returns a*b/c, but gives the correct answer if a*b doesn't fit in uint64_t.
// (Still gives wrong answer if a*b/c doesn't fit, or if b*c > UINT64_MAX.)
static uint64_t muldiv64(uint64_t a, uint64_t b, uint64_t c)
{
	// doing it in __int128 would be easier, but that ends up calling __udivti3 which is a waste of time.
	return (a/c*b) + (a%c*b/c);
}

static LARGE_INTEGER timer_freq;
oninit_static()
{
	QueryPerformanceFrequency(&timer_freq);
}

uint64_t time_us_ne()
{
	////this one has an accuracy of 10ms by default
	//ULARGE_INTEGER time;
	//GetSystemTimeAsFileTime((LPFILETIME)&time);
	//return time.QuadPart/10;//this one is in intervals of 100 nanoseconds, for some reason. We want microseconds.
	
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
#include <unistd.h>

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
	
#ifndef _WIN32
	assert_range(time2_u_fm-time_u_fm, 40000, 60000);
	assert_range(time2_u_fu-time_u_fu, 40000, 60000);
#else
	assert_range(time2_u_fm-time_u_fm, 40000, 70000); // Windows time is low resolution by default
	assert_range(time2_u_fu-time_u_fu, 40000, 70000);
#endif
	assert_range(time2_une_fm-time_une_fm, 40000, 60000);
	assert_range(time2_une_fu-time_une_fu, 40000, 60000);
}
