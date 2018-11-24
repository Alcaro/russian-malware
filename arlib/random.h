#include "global.h"

// this is PCG-XSH-RR with 64-bit state and 32-bit output, adapted from wikipedia
// https://en.wikipedia.org/wiki/Permuted_congruential_generator
class random_pcg : nocopy {
	uint64_t state; // wikipedia initializes this to 0x4d595df4d0f33173
	
	static const uint64_t multiplier = 6364136223846793005;
	static const uint64_t increment  = 1442695040888963407;
	
	//optimizes to a single 'ror' instruction on x86, and similar on other archs - even the &31 disappears
	//(in fact, it needs the &31 - without it, gcc emits one opcode per operator instead)
	static uint32_t ror32(uint32_t x, unsigned bits)
	{
		return (x>>bits) | (x<<(-bits&31));
	}
public:
	uint32_t rand32()
	{
		uint64_t prev = state;
		state = state*multiplier + increment;
		return ror32((prev^(prev>>18))>>27, prev>>59);
	}
	uint64_t rand64()
	{
		return ((uint64_t)rand32() << 32) | rand32();
	}
	
	void seed(uint64_t seed)
	{
		state = seed+increment;
		rand32();
	}
};

//recommended use:
//random_t rand;
//rand.seed(time(NULL));
//return rand()%42;
class random_t : public random_pcg {
	class randresult {
		random_t& src;
	public:
		randresult(random_t& src) : src(src) {}
		uint32_t operator%(uint32_t other)
		{
			return src.rand_mod(other);
		}
		uint32_t operator%(int other)
		{
			return src.rand_mod((uint32_t)other); // blows up if given negative input. just ... don't do that
		}
		uint64_t operator%(uint64_t other)
		{
			return src.rand_mod(other);
		}
		operator uint32_t() { return src.rand32(); }
		operator uint64_t() { return src.rand64(); }
	};
public:
	
	randresult operator()() { return randresult(*this); }
	uint32_t rand_mod(uint32_t limit)
	{
		//if RAND_MAX+1 is not a multiple of 'limit', rand() mod limit becomes biased towards lower values
		//to avoid that, find the highest multiple of limit <= RAND_MAX+1,
		//and if the returned value is higher than that, discard it and create a new one
		//alternatively and equivalently, that many values can be discarded from the lower end of the results
		//the size of the discarded section must be (RAND_MAX+1) mod limit, and discarding low ones is easier
		
		//unfortunatly, RAND_MAX+1 is 2^32, which won't fit in a uint32
		//however, for a >= b, a%b equals (a-b)%b, and UINT32_MAX+1 is always greater than limit
		//the extra -1 +1 needed to avoid an overflow can also be avoided, by using how unsigned integer overflow is defined
		//so the calculation needed is simply
		uint32_t minvalid = ((uint32_t)-limit) % limit;
		
		while (true)
		{
			uint32_t candidate = rand32();
			if (candidate < minvalid) continue;
			return candidate%limit;
		}
	}
	uint64_t rand_mod(uint64_t limit)
	{
		if (LIKELY(limit <= 0xFFFFFFFF)) return rand_mod((uint32_t)limit);
		uint64_t minvalid = ((uint64_t)-limit) % limit;
		
		while (true)
		{
			uint64_t candidate = rand64();
			if (candidate < minvalid) continue;
			return candidate%limit;
		}
	}
	
	//TODO: read /dev/urandom or whatever, then seed with that
	//void seed()
	//{
	//}
};
