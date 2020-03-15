#include "bytestream.h"
#include "test.h"

test("bytestream", "array", "bytestream")
{
	uint8_t foo[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	bytestream b(foo);
	assert(!b.signature(1, 2, 3, 5));
	assert(b.signature(1, 2, 3, 4));
}
