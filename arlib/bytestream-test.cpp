#include "bytestream.h"
#include "test.h"

test("bytestream", "array", "bytestream")
{
	uint8_t foo[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	bytestream b(foo);
	assert(!b.signature(1, 2, 3, 5));
	assert(b.signature(1, 2, 3, 4, 5));
	assert_eq(b.u32b(), 0x06070809);
	
	bytestreamw w;
	w.u8(1);
	w.u16l(0x0302);
	w.u32b(0x04050607);
	w.u8s(8, 9, 10);
	assert_eq(w.finish(), foo);
}
