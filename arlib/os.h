#pragma once
#include "global.h"
#include <time.h>

#if defined(_WIN32)
#define DLLEXPORT extern "C" __declspec(dllexport)
#elif defined(__GNUC__)
#define DLLEXPORT extern "C" __attribute__((__visibility__("default")))
#endif

class dylib : nocopy {
	void* handle = NULL;
	
public:
	dylib() = default;
	dylib(const char * filename) { init(filename); }
	
	// If called multiple times on the same object, will crash. Deinit it first.
	bool init(const char * filename);
	
	// Will return NULL if initialization failed.
	void* sym_ptr(const char * name);
	//Separate function because
	//  The ISO C standard does not require that pointers to functions can be cast back and forth to pointers to data.
	//  -- POSIX dlsym, http://pubs.opengroup.org/onlinepubs/009695399/functions/dlsym.html#tag_03_112_08
	//The cast works fine in practice, but no reason not to. Compiler optimizes it out anyways.
	funcptr sym_func(const char * name)
	{
		funcptr ret;
		*(void**)&ret = this->sym_ptr(name);
		return ret;
	}
	// Usage recommendation:
	// auto Direct3DCreate9 = (decltype(::Direct3DCreate9)*)d3d9.sym_func("Direct3DCreate9");
	// If you need multiple symbols, using DECL_DYLIB_T will save you a bit of typing.
	
	//Fetches multiple symbols. 'names' must be a NUL-separated list of names, terminated with a blank one.
	// This is easiest done by using multiple NUL-terminated strings, and let compiler append another NUL.
	// For example libc.sym_multi(whatever, "isalpha\0isdigit\0isalnum\0")
	//Returns whether all of them were successfully fetched. If not (even if init failed), failures are NULL; all are still attempted.
	//Not recommended for public use; DECL_DYLIB_T is a lot more convenient.
	bool sym_multi(funcptr* out, const char * names);
	
	void deinit();
	~dylib() { deinit(); }
};

#define DECL_DYLIB_MEMB(prefix, name) decltype(::prefix##name)* name;
#define DECL_DYLIB_RESET(prefix, name) name = nullptr;
#define DECL_DYLIB_NAME(prefix, name) #prefix #name "\0"
#define DECL_DYLIB_PREFIX_T(name, prefix, ...) \
	class name { \
	public: \
		PPFOREACH_A(DECL_DYLIB_MEMB, prefix, __VA_ARGS__); \
		\
		name() { PPFOREACH_A(DECL_DYLIB_RESET, prefix, __VA_ARGS__) } \
		name(const char * filename) { init(filename); } \
		bool init(const char * filename) /* TODO: what if 'init' is also one of the imported symbols? */ \
		{ \
			_internal_dylib.init(filename); \
			/* call this even on failure, to ensure members are nulled */ \
			return _internal_dylib.sym_multi((funcptr*)this, PPFOREACH_A(DECL_DYLIB_NAME, prefix, __VA_ARGS__)); \
		} \
	private: \
		dylib _internal_dylib; \
	}
//The intended usecase of the prefixed one is a DLL exporting multiple functions, for example isalpha, isdigit, and isalnum.
//DECL_DYLIB_PREFIX_T(is_t, is, alpha, digit, alnum);
//is_t is("libc.so.6");
//if (is.alpha('a')) {}
//If your functions don't have any plausible prefix, or you want to include the prefix, feel free to instead use
#define DECL_DYLIB_T(name, ...) DECL_DYLIB_PREFIX_T(name, , __VA_ARGS__)
//which is just the above without the second argument.

// This one is like the above two, but caller is expected to initialize it,
//  probably because caller needs it initialized via something other than a dylib.
// raw_names() and raw_target() are valid arguments to dylib::sym_multi(), but caller is welcome to pass them someone else.
// Note that unlike DECL_DYLIB_T, the member function pointers are not set to NULL by default.
#define DECL_DYLIB_RAW_T(name, prefix, ...) \
	class name { \
	public: \
		PPFOREACH_A(DECL_DYLIB_MEMB, prefix, __VA_ARGS__); \
		const char * raw_names() { return PPFOREACH_A(DECL_DYLIB_NAME, prefix, __VA_ARGS__); } \
		funcptr * raw_target() { return (funcptr*)this; } \
	}


//If the program is run under a debugger, this triggers a breakpoint. If not, does nothing.
//Returns whether it did something. The other three do too, but they always do something, if they return at all.
bool debug_or_ignore();
//If the program is run under a debugger, this triggers a breakpoint. If not, the program whines to stderr, and a nearby file.
bool debug_or_print(const char * filename, int line);
#define debug_or_print() debug_or_print(__FILE__, __LINE__)
//If the program is run under a debugger, this triggers a breakpoint. If not, the program silently exits.
bool debug_or_exit();
//If the program is run under a debugger, this triggers a breakpoint. If not, the program crashes.
bool debug_or_abort();

//Same epoch as time(). They're unsigned because the time is known to be after 1970, but it's fine to cast them to signed.
uint64_t time_ms();
uint64_t time_us(); // this will overflow in year 586524
//No epoch; the epoch may vary across machines or reboots. In exchange, it may be faster.
//ms/us will have the same epoch as each other, and will remain constant unless the machine is rebooted.
//It is unspecified whether this clock ticks while the machine is suspended or hibernated.
//There is no second-counting time_ne().
uint64_t time_ms_ne();
uint64_t time_us_ne();

class timer {
	uint64_t start;
public:
	timer()
	{
		reset();
	}
	void reset()
	{
		start = time_us_ne();
	}
	uint64_t us()
	{
		return time_us_ne() - start;
	}
	uint64_t ms()
	{
		return us() / 1000;
	}
	uint64_t us_reset()
	{
		uint64_t new_us = time_us_ne();
		uint64_t prev_us = start;
		start = new_us;
		return new_us - prev_us;
	}
	uint64_t ms_reset()
	{
		return us_reset() / 1000;
	}
};

class benchmark {
	timer t;
	
public:
	uint32_t iterations = 0; // Callers are allowed to read, but not write, these two.
	uint32_t us = 500000;
	
	benchmark() {}
	benchmark(uint32_t us) : us(us) {}
	
	operator bool()
	{
		iterations++;
		if (!(iterations & (iterations-1))) // for really fast-running things, fetching the time is a bottleneck; do it rarely
		{
			uint32_t us_actual = t.us();
			if (us_actual > us)
			{
				us = us_actual;
				return false;
			}
		}
		return true;
	}
	
	double per_second()
	{
		return (double)iterations * 1000000 / us;
	}
};

#ifdef _WIN32
#define gmtime_r(a,b) (*(b)=*gmtime(a)) // this is safe, gmtime() returns a thread local
time_t timegm(struct tm * t); // similar to mktime, but UTC timezone
#endif
