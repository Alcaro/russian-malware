#include "crc32.h"
#include "simd.h"
#define MINIZ_HEADER_FILE_ONLY
#include "deps/miniz.c"

static inline constexpr uint32_t crc32_poly_exp(int dist)
{
	uint32_t out = 1;
	while (dist > 0)
	{
		uint32_t eor = -(out&1) & 0xEDB88320;
		out = eor ^ (out>>1);
		dist--;
	}
	while (dist < 0)
	{
		uint32_t eor = -(out>>31) & 0xDB710641;
		out = (out<<1) ^ eor;
		dist++;
	}
	return out;
}

#ifdef MAYBE_SSE2
#include <wmmintrin.h>

__attribute__((target("pclmul"), always_inline))
static inline __m128i do_fold(__m128i state, __m128i amt)
{
	return _mm_xor_si128(_mm_clmulepi64_si128(state, amt, 0x00), _mm_clmulepi64_si128(state, amt, 0x11));
}

__attribute__((target("pclmul")))
static uint32_t crc32_pclmul(arrayview<uint8_t> data, uint32_t crc)
{
	// 0x104c11db7 is the usual polynomial; it's 0xedb88320 with bits reversed, and a 1 prefixed
	// 0x82608edb and 0x1db710641 also represent the same polynomial; they're the above two with or without a trailing 1 bit
	
	__m128i state = _mm_cvtsi32_si128(~crc);
	__m128i* ptr = (__m128i*)data.ptr();
	size_t len = data.size();
	
	__m128i fold_neg128 = _mm_set_epi32(0, crc32_poly_exp(-128-64), 0, crc32_poly_exp(-128));
	__m128i fold_8   = _mm_set_epi32(0, crc32_poly_exp(  8-64), 0, crc32_poly_exp(8));
	__m128i fold_16  = _mm_set_epi32(0, crc32_poly_exp( 16-64), 0, crc32_poly_exp(16));
	__m128i fold_32  = _mm_set_epi32(0, crc32_poly_exp( 32-64), 0, crc32_poly_exp(32));
	__m128i fold_64  = _mm_set_epi32(0, crc32_poly_exp( 64-64), 0, crc32_poly_exp(64));
	__m128i fold_128 = _mm_set_epi32(0, crc32_poly_exp(128-64), 0, crc32_poly_exp(128));
	__m128i fold_256 = _mm_set_epi32(0, crc32_poly_exp(256-64), 0, crc32_poly_exp(256));
	__m128i fold_384 = _mm_set_epi32(0, crc32_poly_exp(384-64), 0, crc32_poly_exp(384));
	__m128i fold_512 = _mm_set_epi32(0, crc32_poly_exp(512-64), 0, crc32_poly_exp(512));
	
	if (len >= 16+48+16)
	{
		__m128i state0 = state;
		__m128i state1 = _mm_setzero_si128();
		__m128i state2 = _mm_setzero_si128();
		__m128i state3 = _mm_setzero_si128();
		
		while (len >= 64+48+16)
		{
			// standard recommendation is process 64 bytes at the time, pclmul's latency is about 7x its execution time
			state0 = do_fold(_mm_xor_si128(state0, _mm_loadu_si128(ptr++)), fold_512);
			state1 = do_fold(_mm_xor_si128(state1, _mm_loadu_si128(ptr++)), fold_512);
			state2 = do_fold(_mm_xor_si128(state2, _mm_loadu_si128(ptr++)), fold_512);
			state3 = do_fold(_mm_xor_si128(state3, _mm_loadu_si128(ptr++)), fold_512);
			len -= 64;
		}
		
		state0 = do_fold(_mm_xor_si128(state0, _mm_loadu_si128(ptr++)), fold_384);
		state1 = do_fold(_mm_xor_si128(state1, _mm_loadu_si128(ptr++)), fold_256);
		state2 = do_fold(_mm_xor_si128(state2, _mm_loadu_si128(ptr++)), fold_128);
		
		state = _mm_xor_si128(_mm_xor_si128(state0, state1), _mm_xor_si128(state2, state3));
		len -= 48;
	}
	
	while (len >= 16+16)
	{
		state = do_fold(_mm_xor_si128(state, _mm_loadu_si128(ptr++)), fold_128);
		len -= 16;
	}
	
	const uint8_t * ptr8 = (uint8_t*)ptr;
	if (len & 8)
	{
		state = do_fold(_mm_xor_si128(state, _mm_loadl_epi64(ptr)), fold_64);
		ptr8 += 8;
	}
	if (len & 4)
	{
		state = do_fold(_mm_xor_si128(state, _mm_cvtsi32_si128(*(uint32_t*)ptr8)), fold_32);
		ptr8 += 4;
	}
	if (len & 2)
	{
		state = do_fold(_mm_xor_si128(state, _mm_cvtsi32_si128(*(uint16_t*)ptr8)), fold_16);
		ptr8 += 2;
	}
	if (len & 1)
	{
		state = do_fold(_mm_xor_si128(state, _mm_cvtsi32_si128(*(uint8_t*)ptr8)), fold_8);
		ptr8 += 1;
	}
	
	if (len >= 16)
		state = _mm_xor_si128(state, _mm_loadu_si128((__m128i*)ptr8));
	else
		state = do_fold(state, fold_neg128); // inverse fold - kinda funky, but it works. Used only if input size is less than 16.
	
	// fold 128 bits into 64
	__m128i mask_low32 = _mm_set1_epi64x(0x00000000FFFFFFFF);
	state = _mm_xor_si128(_mm_srli_si128(state, 8), _mm_clmulepi64_si128(state, fold_64, 0x00));
	state = _mm_xor_si128(_mm_srli_si128(state, 4), _mm_clmulepi64_si128(_mm_and_si128(state, mask_low32), fold_32, 0x00));
	
	// Barrett reduction, using P(x) and µ from http://intel.ly/2ySEwL0, with bits backwards
	__m128i magic = _mm_set_epi64x(0x01f7011641, 0x01db710641);
	__m128i magic2;
	magic2 = _mm_clmulepi64_si128(_mm_and_si128(state,  mask_low32), magic, 0x10);
	magic2 = _mm_clmulepi64_si128(_mm_and_si128(magic2, mask_low32), magic, 0x00);
	
	return ~_mm_cvtsi128_si32(_mm_srli_si128(_mm_xor_si128(state, magic2), 4));
}

uint32_t crc32_update(arrayview<uint8_t> data, uint32_t crc)
{
#ifdef MAYBE_SSE2
	if (data.size() >= 4 // mz_crc32 is slow per byte, but pclmul has a pretty high startup time; mz wins for size <= 4
#ifndef __PCLMUL__       // (mz_crc32 needs to exist for machines without pclmul support)
		&& __builtin_cpu_supports("pclmul") // this should be optimized if -mpclmul, but isn't, so more ifdef
#endif
	)
		return crc32_pclmul(data, crc);
#endif
	return mz_crc32(crc, data.ptr(), data.size());
}
#endif

#include "test.h"
#include "os.h"

static bool do_bench = false;

static void bench(const uint8_t * buf, int len, int iter, uint32_t exp)
{
	timer t;
	uint32_t tmp = 0;
	for (size_t n : range(iter))
	{
		tmp += crc32_update(bytesr(buf, len), tmp);
		if (n == 0) assert_eq(tmp, exp);
		if (!do_bench) return;
	}
	uint64_t us = t.us();
	if (iter > 1)
		printf("size %d - %luus - %fGB/s\n", len, (unsigned long)us, (double)len*iter/us/1024/1024/1024*1000000);
}

test("crc32", "", "crc32")
{
	//do_bench = true;
	
	uint8_t buf[65536];
	for (size_t i : range(256)) buf[i] = 0;
	buf[0] = 0xFF; buf[1] = 0xFF; buf[2] = 0xFF; buf[3] = 0xFF;
	assert_eq(crc32(bytesr(buf, 256)), 0xFFFFFFFF);
	
	buf[255] = 0x80;
	assert_eq(crc32(bytesr(buf, 256)), ~0xEDB88320);
	
	for (size_t i : range(256)) buf[i] = i;
	assert_eq(crc32(bytesr(buf, 256)), 0x29058C73);
	
	uint32_t k = ~0;
	for (size_t i : range(65536))
	{
		k = (-(k&1) & 0xEDB88320) ^ (k>>1); // random-looking sequence, so table-based implementations can't predict anything
		buf[i] = k;
	}
	
	uint32_t expected[129] = {
		0x00000000,
		
		0xc46e20c8,0xa11a972e,0x7da9ee56,0x69740160,0x6e0d1896,0x5ebf9e66,0xd1eab21f,0xe1d018da,
		0xebe08c57,0x0889b7d1,0x34693a3b,0x68e6b0b2,0xf2058c8e,0xda9dd72c,0xe1db6fbf,0x893d9aff,
		0x1231beba,0x8f78c531,0x5585349a,0x445ec237,0x2992fd2a,0xc59cc333,0x0b112992,0x12b39209,
		0x29c4107a,0xce474aff,0xa97d8369,0xcd711160,0x8aa7d28d,0xa6399ac5,0x4a76f6ab,0xaa4f50d9,
		0x31cbabcf,0x3a8bf015,0xce54051f,0x671c74b9,0x7cb499b3,0x6375b230,0x68b1ac3a,0x5a6204c7,
		0xf05a9b30,0xe7471dec,0x9f57c9c8,0xd89d1663,0xa406330b,0x14aa300c,0x1e1a3228,0x8574409a,
		0x1e8bec58,0xd9c7bb40,0x93df5256,0x4b986165,0xe2fc8805,0xa43c5295,0xbccdd538,0x60032970,
		0xcfd5fcc0,0x82c84b4d,0x52e5400d,0xe8e55b03,0x8459cef5,0x788cfa1c,0xf57822dd,0x33f90fb7,
		
		0x663aa8b5,0x190566a9,0x45c93363,0x1a90773e,0xf01a6943,0x4c966047,0x022e6f1a,0x330e59fa,
		0x6c3af1c4,0x3fd6565c,0x8838e6c0,0xb83af799,0x236e3738,0xc620ddf5,0xecaab88b,0x3ce02419,
		0x11328108,0x62c3452e,0x51b3a90f,0x6d833d32,0xe0b70053,0x5d87c672,0xbf828ca0,0x80d58f37,
		0x288d0761,0xc92bc7f1,0x08ab7c9a,0xcabd0386,0x3f70d1ae,0x97e2c329,0xaa92c4ec,0x8b1b405e,
		0xcbe5c2bc,0x5ac150a9,0xb5856411,0x31d461fb,0x51e0be2b,0xc789227a,0x3ea69489,0x68ec7f1c,
		0xd1dce1fe,0x830d3356,0x5589416c,0x79865b95,0xc8a1bdab,0x33c4d628,0xb781f29d,0x74093918,
		0xd51b22ad,0x70b085dd,0x2f10a672,0x83f3ff11,0x228e8f36,0x502895c3,0xf6e664be,0xed410f34,
		0xfaed161c,0x2f9afbe1,0x71917502,0xeb70cd3a,0x7f8e1706,0xe313ef75,0x9e88ec3c,0xcbf05110,
		};
	for (int i=0;i<=128;i++)
	{
		testctx(tostring(i)) {
			bench(buf, i, 1, expected[i]);
		}
	}
	
	bench(buf, 65536, 4096,  0xE42084DB);
	bench(buf, 1024, 65536,  0xD902C36C);
	bench(buf, 256, 1048576, 0x5FFC6491);
	bench(buf, 64, 16777216, 0x33F90FB7);
	bench(buf, 32, 16777216, 0xAA4F50D9);
	bench(buf, 31, 16777216, 0x4A76F6AB);
	bench(buf, 24, 16777216, 0x12B39209);
	bench(buf, 16, 16777216, 0x893D9AFF);
	bench(buf, 8, 16777216,  0xE1D018DA);
	bench(buf, 4, 16777216,  0x69740160);
	bench(buf, 2, 16777216,  0xA11A972E);
	bench(buf, 1, 16777216,  0xC46E20C8);
	bench(buf, 0, 16777216,  0x00000000);
}
