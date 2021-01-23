#pragma once
#include "global.h"
#ifdef __linux__
#include <unistd.h>
#endif
#if defined(_WIN32) && _WIN32_WINNT >= _WIN32_WINNT_WIN7
#include <windows.h>
#include <bcrypt.h>
#endif
#if defined(_WIN32) && _WIN32_WINNT < _WIN32_WINNT_WIN7
#include <wincrypt.h>
extern HCRYPTPROV rand_seed_prov;
#endif

// Max size is 256. Despite the return value, it always succeeds.
static bool rand_secure(void* out, size_t n)
{
#if defined(__linux__)
	return getentropy(out, n) == 0; // can't fail unless kernel < 3.17 (oct 2014), bad pointer, size > 256, or malicious seccomp/ptrace
#elif defined(_WIN32) && _WIN32_WINNT >= _WIN32_WINNT_WIN7
	return BCryptGenRandom(nullptr, (uint8_t*)out, n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(_WIN32) && _WIN32_WINNT < _WIN32_WINNT_WIN7
	return CryptGenRandom(rand_seed_prov, n, (uint8_t*)out);
#else
	#error unsupported
#endif
}


template<bool is_global = false>
class random_base_t : nocopy {
protected:
	uint64_t state;
public:
	void seed() { rand_secure(&state, sizeof(state)); }
	void seed(uint64_t num) { state = num; rand32(); }
	
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
extern random_base_t<true> g_rand; // g_rand is thread safe, random_t is not
