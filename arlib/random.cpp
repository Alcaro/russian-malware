#include "random.h"
#include "thread.h"

//optimizes to a single ror instruction on x86, and similar on other archs - even the &31 disappears
//(in fact, it needs the &31 - without it, gcc emits one opcode per operator)
static uint32_t ror32(uint32_t x, unsigned bits)
{
	return (x>>bits) | (x<<(-bits&31));
}

// this is PCG-XSH-RR with 64-bit state and 32-bit output, adapted from wikipedia
// https://en.wikipedia.org/wiki/Permuted_congruential_generator

static const uint64_t multiplier = 6364136223846793005; // I don't know how this number was chosen
static const uint64_t increment  = 1442695040888963407; // "or an arbitrary odd constant" ~wikipedia

static uint32_t permute(uint64_t state)
{
	return ror32((state^(state>>18))>>27, state>>59);
}

template<>
uint32_t random_base_t<false>::rand32()
{
	uint64_t prev = state;
	state = prev*multiplier + increment;
	return permute(prev);
}
#ifdef ARLIB_THREAD
template<>
uint32_t random_base_t<true>::rand32()
{
	uint64_t prev = lock_read<lock_loose>(&state);
	while (true)
	{
		uint64_t prev2 = lock_cmpxchg<lock_loose,lock_loose>(&state, prev, prev*multiplier + increment);
		if (prev == prev2) break;
		prev = prev2;
	}
	return permute(prev);
}
#endif
template<bool is_global>
uint64_t random_base_t<is_global>::rand64()
{
	return ((uint64_t)rand32() << 32) | rand32();
}

template<bool is_global>
uint32_t random_base_t<is_global>::rand_mod(uint32_t limit)
{
	// if RAND_MAX+1 is not a multiple of 'limit', rand() mod limit becomes biased towards lower values
	// to avoid that, find the highest multiple of limit <= RAND_MAX+1,
	// and if the returned value is higher than that, discard it and create a new one
	// alternatively and equivalently, that many values can be discarded from the lower end of the results
	// the size of the discarded section must be (RAND_MAX+1) mod limit, and discarding low ones is easier
	
	// unfortunately, RAND_MAX+1 is 2^32, which won't fit in a uint32
	// however, for a >= b, a%b equals (a-b)%b, and UINT32_MAX+1 is always greater than limit
	// the extra -1 +1 needed to avoid an overflow can also be avoided, by using how unsigned integer overflow is defined
	// so the calculation needed is simply
	uint32_t minvalid = -limit % limit;
	
	while (true)
	{
		uint32_t candidate = rand32();
		if (candidate < minvalid) continue;
		return candidate%limit;
	}
}
template<bool is_global>
uint64_t random_base_t<is_global>::rand_mod(uint64_t limit)
{
	if (LIKELY(limit <= 0xFFFFFFFF)) return rand_mod((uint32_t)limit);
	uint64_t minvalid = -limit % limit;
	
	while (true)
	{
		uint64_t candidate = rand64();
		if (candidate < minvalid) continue;
		return candidate%limit;
	}
}

template class random_base_t<false>;
#ifdef ARLIB_THREAD
template class random_base_t<true>;
random_base_t<true> g_rand;
#else
random_base_t<false> g_rand;
#endif
oninit_static() { g_rand.seed(); }


#include "test.h"
test("random distribution","","random")
{
	random_t rand(42);
	
	int bad[3] = { 0, 0, 0 };
	int good[3] = { 0, 0, 0 };
	for (int i=0;i<10000;i++)
	{
		uint32_t bad1 = rand.rand32() % 0xC0000000;
		uint32_t good1 = rand(0xC0000000);
		
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
		assert(rand_secure(&n[i], sizeof(n[i])));
	}
	
	// not gonna implement a real RNG test, just some basic sanity checks
	assert_ne(n[0], 0);
	assert_ne(n[1], 0);
	assert_ne(n[2], 0);
	assert_ne(n[0], (uint64_t)-1);
	assert_ne(n[1], (uint64_t)-1);
	assert_ne(n[2], (uint64_t)-1);
	assert_ne(n[0], n[1]);
	assert_ne(n[0], n[2]);
	assert_ne(n[1], n[2]);
}
