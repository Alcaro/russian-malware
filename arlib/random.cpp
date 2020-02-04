#include "random.h"

#if defined(_WIN32) && _WIN32_WINNT < _WIN32_WINNT_WIN7
#include <wincrypt.h>

#ifdef ARLIB_THREAD
#include "thread.h"
#endif

static HCRYPTPROV prov = 0; // despite starting with H, it's ULONG_PTR, not a pointer
bool random_t::get_seed(void* out, size_t n)
{
#ifndef ARLIB_THREAD
	if (!prov)
		CryptAcquireContextA(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT|CRYPT_SILENT);
#else
	if (!lock_read_acq(&prov))
	{
		HCRYPTPROV newprov;
		CryptAcquireContextA(&newprov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT|CRYPT_SILENT);
		if (lock_cmpxchg_rel(&prov, 0, newprov) != 0)
			CryptReleaseContext(newprov, 0);
	}
#endif
	return CryptGenRandom(prov, n, (uint8_t*)out);
}
#endif

#include "test.h"
test("random distribution","","random")
{
	random_t rand(42);
	
	int bad[3] = { 0, 0, 0 };
	int good[3] = { 0, 0, 0 };
	for (int i=0;i<10000;i++)
	{
		uint32_t bad1 = rand();
		bad1 %= 0xC0000000;
		uint32_t good1 = rand() % 0xC0000000;
		
		bad[bad1/0x40000000]++;
		good[good1/0x40000000]++;
	}
	
	assert_gt(bad[0], 4800);
	assert_gt(bad[1], 2400);
	assert_gt(bad[2], 2400);
	assert_gt(good[0], 3200);
	assert_gt(good[1], 3200);
	assert_gt(good[2], 3200);
}

test("random seeding","","random")
{
	uint64_t n[3] = { 0, 0, 0 };
	for (int i=0;i<3;i++)
	{
		assert(random_t::get_seed(&n[i], sizeof(n[i])));
	}
	
	// not gonna implement a real RNG test, just some basic sanity checks
	assert_ne(n[0], 0);
	assert_ne(n[1], 0);
	assert_ne(n[2], 0);
	assert_ne(n[0], n[1]);
	assert_ne(n[0], n[2]);
	assert_ne(n[1], n[2]);
}
