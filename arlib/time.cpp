#include "os.h"
#include "time.h"

// Returns a*b/c, but gives the correct answer if a*b doesn't fit in uint64_t.
// (May give wrong answer if a*b/c doesn't fit, or if b*c > UINT64_MAX.)
inline uint64_t muldiv64(uint64_t a, const uint64_t b, uint64_t c)
{
#ifdef __x86_64__
	if (__builtin_constant_p(b))
	{
		uint64_t out;
		uint64_t clobber;
		__asm__("{imul %3,%2,%%rax\nidiv %4|imul rax,%2,%3\nidiv %4}" : "=&a"(out), "=d"(clobber) : "r"(a), "i"(b), "r"(c), "d"(0));
		return out;
	}
#endif
	// doing it in __int128 would be easier, but that ends up calling __udivti3 which is a waste of time.
	return (a/c*b) + (a%c*b/c);
}

#ifdef _WIN32
#include <windows.h>
static LARGE_INTEGER timer_freq;
oninit_static()
{
	QueryPerformanceFrequency(&timer_freq);
}

// force expand div by constant to mul+shift, gcc won't do that automatically on -Os
inline uint64_t div10(uint64_t val)
{
#if SIZE_MAX > 0xFFFFFFFF // gcc doesn't support int128 on 32bit platforms
	return ((unsigned __int128)val*0xCCCCCCCCCCCCCCCD) >> 64 >> 3;
#else
	return val/10;
#endif
}
inline uint64_t div10000(uint64_t val)
{
#if SIZE_MAX > 0xFFFFFFFF
	return ((unsigned __int128)val*0x346DC5D63886594B) >> 64 >> 11;
#else
	return val/10000;
#endif
}
inline uint64_t div24(uint64_t val)
{
#if SIZE_MAX > 0xFFFFFFFF
	return ((unsigned __int128)val*0xaaaaaaaaaaaaaaab) >> 64 >> 4;
#else
	return val/24;
#endif
}
inline uint64_t div24000(uint64_t val)
{
#if SIZE_MAX > 0xFFFFFFFF
	return ((unsigned __int128)val*0xaec33e1f671529a5) >> 64 >> 14;
#else
	return val/24000;
#endif
}

uint64_t timer::get_counter()
{
	LARGE_INTEGER timer_now;
	QueryPerformanceCounter(&timer_now);
	return timer_now.QuadPart;
}
uint64_t timer::to_us(uint64_t count)
{
#ifdef __x86_64__
	// according to values in https://github.com/microsoft/STL/blob/main/stl/inc/__msvc_chrono.hpp
	if (LIKELY(timer_freq.QuadPart == 10000000))
		return div10(count);
	// frequency can be 24 million if on an ARM emulating x86, but they can just take the slow path.
	// This function is mostly for benchmarking, benchmarking under emulation tests the emulator and nothing else.
#endif
#ifdef __aarch64__
	if (LIKELY(timer_freq.QuadPart == 24000000))
		return div24(count);
#endif
	return muldiv64(count, 1000000, timer_freq.QuadPart);
}
uint64_t timer::to_ms(uint64_t count)
{
#ifdef __x86_64__
	if (LIKELY(timer_freq.QuadPart == 10000000))
		return div10000(count);
#endif
#ifdef __aarch64__
	if (LIKELY(timer_freq.QuadPart == 24000000))
		return div24000(count);
#endif
	return muldiv64(count, 1000, timer_freq.QuadPart);
}

#define WINDOWS_TICK 10000000
#define SEC_TO_UNIX_EPOCH 11644473600LL

timestamp timestamp::from_native(FILETIME time)
{
	int64_t tmp = ((uint64_t)time.dwHighDateTime << 32) | time.dwLowDateTime;
	
	timestamp ret;
	ret.sec = tmp / WINDOWS_TICK - SEC_TO_UNIX_EPOCH;
	ret.nsec = tmp % WINDOWS_TICK * (1000000000/WINDOWS_TICK);
	return ret;
}

FILETIME timestamp::to_native() const
{
	int64_t tmp = (sec + SEC_TO_UNIX_EPOCH) * WINDOWS_TICK + nsec / (1000000000/WINDOWS_TICK);
	
	FILETIME ret;
	ret.dwLowDateTime = tmp & 0xFFFFFFFF;
	ret.dwHighDateTime = tmp>>32;
	return ret;
}

timestamp timestamp::now()
{
	// this one has an accuracy of 10ms by default
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	return from_native(ft);
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

#include "stringconv.h"
string tostring(timestamp val)
{
	string ret = format(val.sec, ".", fmt_pad<9>((uint32_t)val.nsec));
	size_t len = ret.length();
	while (ret[len-1] == '0')
		len--;
	if (ret[len-1] == '.')
		len--;
	return ret.substr(0, len);
}
bool fromstring(cstring s, timestamp& out)
{
	out = {};
	const char * start = (char*)s.bytes().ptr();
	const char * end = start + s.length();
	const char * int_end = start;
	while (int_end < end && isdigit(*int_end))
		int_end++;
	if (!fromstring(s.substr(0, int_end-start), out.sec))
		return false;
	if (int_end != end)
	{
		if (*int_end != '.')
			return false;
		int_end++;
		size_t n_digits = end - int_end;
		if (n_digits == 0 || n_digits > 9)
			return false;
		long ret = 0;
		for (int i=0;i<9;i++)
		{
			ret *= 10;
			if (int_end == end) {}
			else if (!isdigit(*int_end)) return false;
			else ret += *(int_end++) - '0';
		}
		out.nsec = ret;
	}
	return true;
}

#include "test.h"
#ifdef __unix__
#include <unistd.h>
#endif

test("time", "", "time")
{
	timer t;
#ifdef _WIN32
	Sleep(50);
#else
	usleep(50000);
#endif
	assert_range(t.us(), 40000, 60000);
}

test("muldiv64", "", "")
{
	assert_eq(muldiv64(100, 100, 2), 5000);
	assert_eq(muldiv64(0x1000000000000000, 0x0000000000010000, 0x0000000001000000), 0x0010000000000000);
	assert_eq(muldiv64(1000000000000000000, 10000000, 1000000000), 10000000000000000);
	assert_eq(muldiv64(100, 100, 3), 3333);
}

test("timestamp serialization", "", "")
{
	assert_eq(tostring((timestamp){ 123456789,123456789 }), "123456789.123456789");
	assert_eq(tostring((timestamp){ 123456789,123456000 }), "123456789.123456");
	assert_eq(tostring((timestamp){ 123456789,000000000 }), "123456789");
	assert_eq(tostring((timestamp){         0,123456000 }), "0.123456");
	
	// todo: make these less ugly
	assert_eq(try_fromstring<timestamp>("123456789.123456789"), (timestamp{123456789,123456789}));
	assert_eq(try_fromstring<timestamp>("123456789.123456"   ), (timestamp{123456789,123456000}));
	assert_eq(try_fromstring<timestamp>("123456789"          ), (timestamp{123456789,000000000}));
	assert_eq(try_fromstring<timestamp>(        "0.123456"   ), (timestamp{        0,123456000}));
}
