#include "deflate.h"

uint32_t inflator::zlibhead::adler32(bytesr by, uint32_t adler_prev)
{
	// could simd this, but not much point, it's not a bottleneck
	uint32_t a = adler_prev&0xFFFF;
	uint32_t b = adler_prev>>16;
	
	size_t i = 0;
	while (i < (by.size()&~4095)) // 5552 is safe, but 4096 is easier to deal with
	{
		do {
			a += by[i++]; b += a;
			a += by[i++]; b += a;
			a += by[i++]; b += a;
			a += by[i++]; b += a;
		} while (i&4095);
		a %= 65521;
		b %= 65521;
	}
	while (i < (by.size()&~3))
	{
		a += by[i++]; b += a;
		a += by[i++]; b += a;
		a += by[i++]; b += a;
		a += by[i++]; b += a;
	}
	while (i < by.size())
	{
		a += by[i++];
		b += a;
	}
	a %= 65521;
	b %= 65521;
	
	return a | (b<<16);
}

inflator::ret_t inflator::zlibhead::inflate()
{
	if (inf.m_state >= 254)
	{
	process_tail:
		inf.bits_refill_fast();
		if (inf.m_in_nbits < 32) return ret_more_input; // state 254 only needs 16 bits, but the other 16 will be used soon anyways
		if (inf.m_state == 254) // zlib header
		{
			uint32_t head = inf.bits_extract(16);
			if ((head&0x000F) != 0x0008 || // CMF.CF == DEFLATE (ZIP supports 8=DEFLATE and 0=uncompressed, but zlib only supports 8)
			// ignore 00F0 CMF.CINFO, inflator ignores window size (and it's almost always 7, the maximum)
			// ignore C000 FLG.FLEVEL, it's just informational, and there are too many compressors for it to make sense anymore
			    (head&0x2000) != 0x0000 || // FLG.FDICT, not supported
			    (__builtin_bswap16(head)%31) != 0) return ret_error; // FLG.FCHECK (1F00 bits), must be a multiple of 31
			// (are both 0 and 31 allowed in FCHECK if 0 is the correct checksum? I think they are)
			
			inf.m_state = 0;
		}
		else // zlib footer
		{
			inf.m_in_bits_buf >>= inf.m_in_nbits&7;
			inf.m_in_nbits &= ~7;
			uint32_t adler_head = __builtin_bswap32(inf.bits_extract(32)); // zlib header is big endian, even though DEFLATE is little...
			if (adler_head != adler) return ret_error;
			return ret_done;
		}
	}
	
	const uint8_t * out_prev = inf.m_out_at;
	ret_t ret = inf.inflate();
	adler = adler32(bytesr(out_prev, inf.m_out_at - out_prev), adler);
	if (ret != ret_done) return ret;
	
	inf.m_state = 255;
	goto process_tail;
}

bytearray inflator::zlibhead::inflate(bytesr in)
{
	inflator::zlibhead inf;
	inf.set_input(in, true);
	bytearray ret;
	ret.resize(max(4096, in.size()*8));
	inf.set_output_first(ret);
again:
	inflator::ret_t err = inf.inflate();
	if (err == inflator::ret_more_output)
	{
		ret.resize(ret.size()*2);
		inf.set_output_grow(ret);
		goto again;
	}
	else if (err != inflator::ret_done || inf.unused_input() != 0) ret.reset();
	else ret.resize(inf.output_in_last());
	
	return ret;
}

bool inflator::zlibhead::inflate(bytesw out, bytesr in)
{
	inflator::zlibhead inf;
	inf.set_input(in, true);
	inf.set_output_first(out);
	return (inf.inflate() == inflator::ret_done && inf.output_in_last() == out.size() && inf.unused_input() == 0);
}


#include "test.h"
#include "os.h"
#define adler32 inflator::zlibhead::adler32

static bool do_bench = false;

static void bench(const uint8_t * buf, int len, int iter, uint32_t exp)
{
	timer t;
	uint32_t tmp = 1;
	for (size_t n : range(iter))
	{
		tmp = adler32(bytesr(buf, len), tmp);
		if (n == 0) assert_eq(tmp, exp);
		if (!do_bench) return;
	}
	uint64_t us = t.us();
	if (iter > 1)
		printf("size %d - %luus - %fGB/s\n", len, (unsigned long)us, (double)len*iter/us/1024/1024/1024*1000000);
}

test("adler", "", "adler32")
{
	//do_bench = true;
	
	uint8_t buf[65536];
	for (size_t i : range(256)) buf[i] = 0;
	assert_eq(adler32(bytesr(buf, 256)), 0x01000001);
	
	for (size_t i : range(256)) buf[i] = i;
	assert_eq(adler32(bytesr(buf, 256)), 0xADF67F81);
	
	uint32_t k = ~0;
	for (size_t i : range(65536))
	{
		k = (-(k&1) & 0xEDB88320) ^ (k>>1); // random-looking sequence (same as in crc32 tests)
		buf[i] = k;
	}
	
	uint32_t expected[129] = {
		0x00000001,
		
		0x00e000e0,0x020f012f,0x03c501b6,0x065e0299,0x09c8036a,0x0dfa0432,0x13100516,0x18980588,
		0x1e5905c1,0x245605fd,0x2af1069b,0x325b076a,0x3a8c0831,0x438008f4,0x4cb50935,0x55ea0935,
		0x5f9f09b5,0x699409f5,0x74290a95,0x7f8e0b65,0x8b5b0bcd,0x97dc0c81,0xa5370d5b,0xb37f0e48,
		0xc21d0e9e,0xd0e60ec9,0xdfe40efe,0xef9c0fb8,0xffb11015,0x0fe31023,0x208d10aa,0x321a118d,
		0x43f811de,0x565e1266,0x690812aa,0x7c54134c,0x8ff1139d,0xa4161425,0xb8ff14e9,0xceca15cb,
		0xe506163c,0xfb5a1654,0x124916e0,0x296f1726,0x40b81749,0x5832177a,0x70641832,0x88f2188e,
		0xa1ae18bc,0xbb011953,0xd53f1a3e,0xefd21a93,0x0afe1b1d,0x26601b62,0x41c41b64,0x5da91be5,
		0x79ee1c45,0x96e31cf5,0xb4b01dcd,0xd2e91e39,0xf1581e6f,0x10711f0a,0x2fe81f77,0x4ff5200d,
		
		0x704d2058,0x90aa205d,0xb1a920ff,0xd2f92150,0xf4d121d8,0x16fc221c,0x39ba22be,0x5cc9230f,
		0x80602397,0xa43b23db,0xc8b8247d,0xee06254e,0x13ab2596,0x396525ba,0x5f3125cc,0x85862655,
		0xacbf2739,0xd46a27ab,0xfc4e27e4,0x247d2820,0x4cbb283e,0x758828cd,0x9ebc2934,0xc8032947,
		0xf1732970,0x1b2629a4,0x45642a3e,0x6fef2a8b,0x9a802a91,0xc5942b14,0xf1092b75,0x1d1d2c05,
		0x49ea2ccd,0x771b2d31,0xa47e2d63,0xd27a2dfc,0x01712ee8,0x30cf2f5e,0x60e83019,0x917e3096,
		0xc2b23134,0xf4b53203,0x278e32ca,0x5a9b330d,0x8e29338e,0xc297346e,0xf77534de,0x2c9a3516,
		0x61cc3532,0x978c35c0,0xce133687,0x04ec36ca,0x3c37374b,0x73e237ab,0xabbd37db,0xe3b037f3,
		0x1bbe37ff,0x53c33805,0x8c4b3888,0xc5b43969,0xff6d39b9,0x39dd3a61,0x74923ab5,0xaff13b5f,
	};
	for (int i=0;i<=128;i++)
		bench(buf, i, 1, expected[i]);
	
	bench(buf, 65536, 4096*16, 0xdaa138b6);
	bench(buf, 1024, 65536*64, 0x93b2ed08);
	bench(buf, 256, 1048576*8, 0xebf777b6);
	bench(buf, 64, 16777216, 0x4ff5200d);
	bench(buf, 32, 16777216, 0x321a118d);
	bench(buf, 31, 16777216, 0x208d10aa);
	bench(buf, 24, 16777216, 0xb37f0e48);
	bench(buf, 16, 16777216, 0x55ea0935);
	bench(buf, 8, 16777216,  0x18980588);
	bench(buf, 4, 16777216,  0x065e0299);
	bench(buf, 2, 16777216,  0x020f012f);
	bench(buf, 1, 16777216,  0x00e000e0);
	bench(buf, 0, 16777216,  0x00000001);
}
