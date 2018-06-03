#include "safeint.h"
#include "test.h"

//must be a macro, can't pass operator<< as template argument
#define assertstrbase(t, op) \
	"(" #t ")"+tostring(au)+#op+tostring(bu)
#define assertstr(t, op, act, exp) \
	assertstrbase(t, op)+": expected "+tostring(exp)+", got "+tostring(act)
#define TEST(t, op, opname) \
	for (int a=0;a<256;a++) \
	for (int b=0;b<256;b++) \
	{ \
		int au = (t)a; \
		int bu = (t)b; \
		int cu; \
		bool cu_overflow = false; \
		if (1 op 8 == 256 /* if it's operator<< */ && (au < 0 || bu < 0 || bu >= 8)) cu_overflow = true; \
		else cu = au op bu; \
		if (!cu_overflow && (t)cu != cu) cu_overflow = true; \
		\
		t out = -1; /* gcc throws uninitialized warnings without this */ \
		assert_msg(safeint<t>::opname##ov_nobuiltin(au, bu, &out) == cu_overflow, assertstr(t, op, cu_overflow, !cu_overflow)); \
		if (!cu_overflow) assert_msg(out == cu, assertstr(t, op, out, cu)); \
		\
		safeint<t> as = au; \
		safeint<t> bs = bu; \
		safeint<t> cs = as op bs; \
		\
		if (!as.valid() || !bs.valid()) cu_overflow = true; \
		\
		if (!cu_overflow) \
			assert_msg(cs.val() == cu, assertstr(t, op, cs.val(), a op b)); \
		else \
			assert_msg(!cs.valid(), assertstr(t, op, cs.val(), a op b)); \
	}

test("safeint", "", "")
{
	test_skip("kinda slow");
	
	TEST(uint8_t, +,  add)
	TEST(uint8_t, -,  sub)
	TEST(uint8_t, *,  mul)
	TEST(uint8_t, <<, lsl)
	TEST( int8_t, +,  add)
	TEST( int8_t, -,  sub)
	TEST( int8_t, *,  mul)
	TEST( int8_t, <<, lsl)
	
	int16_t i16;
	int32_t i32;
	uint32_t u32;
	
	assert(!safeint<short>::lslov_nobuiltin(1, 14, &i16));
	assert_eq(i16, 1<<14);
	assert( safeint<short>::lslov_nobuiltin(1, 15, &i16));
	assert( safeint<short>::lslov_nobuiltin(1, 16, &i16));
	assert( safeint<short>::lslov_nobuiltin(1, 32, &i16));
	assert( safeint<short>::lslov_nobuiltin(1, 48, &i16));
	assert( safeint<short>::lslov_nobuiltin(1, 64, &i16));
	
	assert(!safeint<int>::lslov_nobuiltin(1, 30, &i32));
	assert_eq(i32, 1<<30);
	assert( safeint<int>::lslov_nobuiltin(1, 31, &i32));
	assert( safeint<int>::lslov_nobuiltin(1, 32, &i32));
	assert( safeint<int>::lslov_nobuiltin(1, 48, &i32));
	assert( safeint<int>::lslov_nobuiltin(1, 64, &i32));
	assert( safeint<int>::lslov_nobuiltin(1, 96, &i32));
	assert( safeint<int>::lslov_nobuiltin(1, 128, &i32));
	
	assert(!safeint<short>::lslov_nobuiltin(2, 13, &i16));
	assert_eq(i16, 1<<14);
	assert( safeint<short>::lslov_nobuiltin(2, 14, &i16));
	assert( safeint<short>::lslov_nobuiltin(2, 15, &i16));
	assert( safeint<short>::lslov_nobuiltin(2, 31, &i16));
	assert( safeint<short>::lslov_nobuiltin(2, 47, &i16));
	assert( safeint<short>::lslov_nobuiltin(2, 63, &i16));
	
	assert(!safeint<int>::lslov_nobuiltin(2, 29, &i32));
	assert_eq(i32, 1<<30);
	assert( safeint<int>::lslov_nobuiltin(2, 30, &i32));
	assert( safeint<int>::lslov_nobuiltin(2, 31, &i32));
	assert( safeint<int>::lslov_nobuiltin(2, 47, &i32));
	assert( safeint<int>::lslov_nobuiltin(2, 63, &i32));
	assert( safeint<int>::lslov_nobuiltin(2, 95, &i32));
	assert( safeint<int>::lslov_nobuiltin(2, 127, &i32));
	
	assert( safeint<int>::lslov_nobuiltin(1, -1,  &i32));
	assert( safeint<int>::lslov_nobuiltin(-1, 1,  &i32));
	assert( safeint<int>::lslov_nobuiltin(-1, -1, &i32));
	assert( safeint<unsigned>::lslov_nobuiltin(1, -1,  &u32));
	assert( safeint<unsigned>::lslov_nobuiltin(-1, 1,  &u32));
	assert( safeint<unsigned>::lslov_nobuiltin(-1, -1, &u32));
}
