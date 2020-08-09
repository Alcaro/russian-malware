#include "bytestream.h"
#include "test.h"

test("bytestream", "array", "bytestream")
{
	uint8_t foo[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0x00,0xE4,0x40,0x46, 0x46,0x40,0xE4,0x00 };
	bytestream b(foo);
	assert(!b.signature(1, 2, 3, 5));
	assert(b.signature(1, 2, 3, 4, 5));
	assert_eq(b.u32b(), 0x06070809);
	assert_eq(b.u8(), 10);
	assert_eq(b.f32l(), 12345.0);
	assert_eq(b.f32b(), 12345.0);
	
	bytestreamw w;
	w.u8(1);
	w.u16l(0x0302);
	w.u32b(0x04050607);
	w.u8s(8, 9, 10);
	w.f32l(12345.0);
	w.f32b(12345.0);
	assert_eq(bytesr(w.finish()), bytesr(foo));
}
