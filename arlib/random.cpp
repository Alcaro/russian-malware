#include "random.h"
#include "test.h"

test("random distribution","","random")
{
	random_t rand;
	rand.seed(42);
	
	int bad[3]={0,0,0};
	int good[3]={0,0,0};
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
