#include "test.h"

test("memeq", "", "memeq")
{
	if (RUNNING_ON_VALGRIND) test_skip_force("takes six seconds on valgrind");
	
	for (int bits=0;bits<65536;bits++)
	{
		static const uint8_t n1[32] = {};
		uint8_t n2[16];
		uint16_t n3[16];
		uint16_t n4[16];
		for (int i=0;i<16;i++)
		{
			n2[i] = !!(bits & (1<<i));
			n3[i] = !!(bits & (1<<i));
			n4[i] = !!(bits & (1<<i)) << 8;
		}
		
		// ugly copypasta, but if it's not unrolled, it reverts to memcmp, and testing that memcmp equals memcmp isn't really interesting.
		assert_eq(memeq(n1, n2, 0 ), !memcmp(n1, n2, 0 ));
		assert_eq(memeq(n1, n2, 1 ), !memcmp(n1, n2, 1 ));
		assert_eq(memeq(n1, n2, 2 ), !memcmp(n1, n2, 2 ));
		assert_eq(memeq(n1, n2, 3 ), !memcmp(n1, n2, 3 ));
		assert_eq(memeq(n1, n2, 4 ), !memcmp(n1, n2, 4 ));
		assert_eq(memeq(n1, n2, 5 ), !memcmp(n1, n2, 5 ));
		assert_eq(memeq(n1, n2, 6 ), !memcmp(n1, n2, 6 ));
		assert_eq(memeq(n1, n2, 7 ), !memcmp(n1, n2, 7 ));
		assert_eq(memeq(n1, n2, 8 ), !memcmp(n1, n2, 8 ));
		assert_eq(memeq(n1, n2, 9 ), !memcmp(n1, n2, 9 ));
		assert_eq(memeq(n1, n2, 10), !memcmp(n1, n2, 10));
		assert_eq(memeq(n1, n2, 11), !memcmp(n1, n2, 11));
		assert_eq(memeq(n1, n2, 12), !memcmp(n1, n2, 12));
		assert_eq(memeq(n1, n2, 13), !memcmp(n1, n2, 13));
		assert_eq(memeq(n1, n2, 14), !memcmp(n1, n2, 14));
		assert_eq(memeq(n1, n2, 15), !memcmp(n1, n2, 15));
		assert_eq(memeq(n1, n2, 16), !memcmp(n1, n2, 16));
		
		assert_eq(memeq(n1, n3, 17), !memcmp(n1, n3, 17));
		assert_eq(memeq(n1, n3, 18), !memcmp(n1, n3, 18));
		assert_eq(memeq(n1, n3, 19), !memcmp(n1, n3, 19));
		assert_eq(memeq(n1, n3, 20), !memcmp(n1, n3, 20));
		assert_eq(memeq(n1, n3, 21), !memcmp(n1, n3, 21));
		assert_eq(memeq(n1, n3, 22), !memcmp(n1, n3, 22));
		assert_eq(memeq(n1, n3, 23), !memcmp(n1, n3, 23));
		assert_eq(memeq(n1, n3, 24), !memcmp(n1, n3, 24));
		assert_eq(memeq(n1, n3, 25), !memcmp(n1, n3, 25));
		assert_eq(memeq(n1, n3, 26), !memcmp(n1, n3, 26));
		assert_eq(memeq(n1, n3, 27), !memcmp(n1, n3, 27));
		assert_eq(memeq(n1, n3, 28), !memcmp(n1, n3, 28));
		assert_eq(memeq(n1, n3, 29), !memcmp(n1, n3, 29));
		assert_eq(memeq(n1, n3, 30), !memcmp(n1, n3, 30));
		assert_eq(memeq(n1, n3, 31), !memcmp(n1, n3, 31));
		assert_eq(memeq(n1, n3, 32), !memcmp(n1, n3, 32));
		
		assert_eq(memeq(n1, n4, 17), !memcmp(n1, n4, 17));
		assert_eq(memeq(n1, n4, 18), !memcmp(n1, n4, 18));
		assert_eq(memeq(n1, n4, 19), !memcmp(n1, n4, 19));
		assert_eq(memeq(n1, n4, 20), !memcmp(n1, n4, 20));
		assert_eq(memeq(n1, n4, 21), !memcmp(n1, n4, 21));
		assert_eq(memeq(n1, n4, 22), !memcmp(n1, n4, 22));
		assert_eq(memeq(n1, n4, 23), !memcmp(n1, n4, 23));
		assert_eq(memeq(n1, n4, 24), !memcmp(n1, n4, 24));
		assert_eq(memeq(n1, n4, 25), !memcmp(n1, n4, 25));
		assert_eq(memeq(n1, n4, 26), !memcmp(n1, n4, 26));
		assert_eq(memeq(n1, n4, 27), !memcmp(n1, n4, 27));
		assert_eq(memeq(n1, n4, 28), !memcmp(n1, n4, 28));
		assert_eq(memeq(n1, n4, 29), !memcmp(n1, n4, 29));
		assert_eq(memeq(n1, n4, 30), !memcmp(n1, n4, 30));
		assert_eq(memeq(n1, n4, 31), !memcmp(n1, n4, 31));
		assert_eq(memeq(n1, n4, 32), !memcmp(n1, n4, 32));
	}
}
