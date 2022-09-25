#pragma once
#include "global.h"
#include <compare>
#include <time.h>
#include <type_traits>

#ifdef _WIN32
typedef struct _FILETIME FILETIME;
#endif

struct duration {
	time_t sec;
	long nsec; // to match timestamp
	
	std::strong_ordering operator<=>(const duration& other) const = default;
	
	static duration ms(int ms) { return { ms/1000, ms%1000*1000000 }; }
	int ms() { return sec*1000 + nsec/1000000; }
	
	// Returns time since an unspecified point (can be system boot, can be Unix epoch, can be something else).
	// May be more precise than timestamp::now(), and/or quicker to read; it's more suitable for speed tests and short timeouts.
	// May also react differently from timestamp::now() if the system's clock changes, due to leap seconds or whatever.
	static duration perfcounter()
	{
		static_assert(std::is_layout_compatible_v<struct timespec, duration>);
		duration ret;
		clock_gettime(CLOCK_MONOTONIC, (struct timespec*)&ret);
		return ret;
	}
};
struct timestamp {
	time_t sec;
	long nsec; // disgusting type, but struct timespec is time_t + long, and I want to be binary compatible
	
	std::strong_ordering operator<=>(const timestamp& other) const = default;
	timestamp operator+(duration dur) const
	{
		timestamp ret = *this;
		ret.sec += dur.sec;
		ret.nsec += dur.nsec;
		if (__builtin_constant_p(dur.nsec) && dur.nsec == 0) {}
		else if (ret.nsec > 1000000000)
		{
			ret.sec++;
			ret.nsec -= 1000000000;
		}
		return ret;
	}
	
	timestamp operator-(duration other) const
	{
		timestamp ret;
		ret.sec = sec - other.sec;
		ret.nsec = nsec - other.nsec;
		if (__builtin_constant_p(other.nsec) && other.nsec == 0) {}
		else if (ret.nsec < 0)
		{
			ret.sec--;
			ret.nsec += 1000000000;
		}
		return ret;
	}
	duration operator-(timestamp other) const
	{
		duration ret;
		ret.sec = sec - other.sec;
		ret.nsec = nsec - other.nsec;
		if (ret.nsec < 0)
		{
			ret.sec--;
			ret.nsec += 1000000000;
		}
		return ret;
	}
	
#ifdef __unix__
	static timestamp from_native(struct timespec ts) { return reinterpret<timestamp>(ts); }
	struct timespec to_native() const { return reinterpret<struct timespec>(*this); }
#else
	static timestamp from_native(FILETIME ts);
	FILETIME to_native() const;
#endif
	
#ifdef __unix__
	static timestamp now()
	{
		static_assert(std::is_layout_compatible_v<struct timespec, timestamp>);
		timestamp ret;
		clock_gettime(CLOCK_REALTIME, (struct timespec*)&ret);
		return ret;
	}
#else
	static timestamp now();
#endif
	
	// Returns a timestamp in the far future, suitable as timeout for infinite waits.
	// This exact timestamp may be hardcoded; don't do any math on it.
	static timestamp at_never() { return { sec_never(), 0 }; }
	
	static time_t sec_never()
	{
		if constexpr (sizeof(time_t) == sizeof(int32_t)) return INT32_MAX;
		if constexpr (sizeof(time_t) == sizeof(int64_t)) return INT64_MAX;
		__builtin_trap();
	}
	
	static timestamp in_ms(int ms)
	{
		return now() + duration::ms(ms);
	}
};
