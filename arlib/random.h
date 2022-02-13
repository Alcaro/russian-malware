#pragma once
#include "global.h"
#ifdef __linux__
#include <unistd.h>
#endif

#if defined(_WIN32)
#include <windows.h> // ntsecapi.h doesn't include its dependencies properly
#include <ntsecapi.h>
#endif

// Yields cryptographically secure random numbers. Max size is 256. Despite the return value, it always succeeds.
static bool rand_secure(void* out, size_t n)
{
#if defined(__linux__)
	return getentropy(out, n) == 0; // can't fail unless kernel < 3.17 (oct 2014), bad pointer, size > 256, or strange seccomp/ptrace/etc
#elif defined(_WIN32)
	// documented on msdn as having no import library, but works in mingw (other than the WINAPI goof)
	// msdn doesn't claim it's cryptographically secure, but everything else that talks about it treats it as such
	// msdn also says I should use CryptGenRandom, but that requires creating a random number provider and offers no clear benefits
	// BCryptGenRandom exists, but doesn't help until 7+, is in a rarer DLL than RtlGenRandom, and offers no clear benefits either
	return RtlGenRandom(out, n);
#else
	#error unsupported
#endif
}


template<bool is_global = false>
class random_base_t : nocopy {
protected:
	uint64_t state;
public:
	// CSPRNG is overkill, but the alternative is time, which has a few drawbacks of its own. Better overkill than underkill.
	void seed() { rand_secure(&state, sizeof(state)); }
	void seed(uint64_t num) { state = num; rand32(); } // discard first output, otherwise seed(1) would return 1 as next output
	
	uint32_t rand32();
	uint64_t rand64();
	uint32_t rand_mod(uint32_t limit);
	uint64_t rand_mod(uint64_t limit);
	
	uint32_t operator()(uint32_t mod) { return rand_mod(mod); }
	uint64_t operator()(uint64_t mod) { return rand_mod(mod); }
	// gives bad answers for negative input, but there are no good answers for that
	uint32_t operator()(int mod) { return rand_mod((uint32_t)mod); }
};
class random_t : public random_base_t<false> {
public:
	random_t() { seed(); }
	random_t(uint64_t num) { seed(num); } // Not recommended unless you need predictable output.
};
#ifdef ARLIB_THREAD
extern random_base_t<true> g_rand; // g_rand is thread safe, random_t is not
#else
extern random_base_t<false> g_rand;
#endif
