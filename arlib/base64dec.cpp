#include "base64.h"

static const uint8_t decode[128] = {
#define __ 0x80 // invalid
#define PA 0x81 // padding
#define SP 0xFF // whitespace
//x0  x1  x2  x3  x4  x5  x6  x7  x8  x9  xA  xB  xC  xD  xE  xF
  __, __, __, __, __, __, __, __, __, SP, SP, __, __, SP, __, __, // 0x
  __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, // 1x
  SP, __, __, __, __, __, __, __, __, __, __, 62, __, 62, __, 63, // 2x  !"#$%&'()*+,-./ (yes, there are two 62s and 63s)
  52, 53, 54, 55, 56, 57, 58, 59, 60, 61, __, __, __, PA, __, __, // 3x 0123456789:;<=>?
  __,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, // 4x @ABCDEFGHIJKLMNO
  15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, __, __, __, __, 63, // 5x PQRSTUVWXYZ[\]^_
  __, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, // 6x `abcdefghijklmno
  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, __, __, __, __, __, // 7x pqrstuvwxyz{|}~
};

size_t base64_dec_raw(arrayvieww<uint8_t> out, cstring text)
{
	uint8_t * outptr = out.ptr();
	const char * in = (char*)text.bytes().ptr();
	const char * inend = in + text.length();
	
	int final_chunk = 3;
	const char * in_real;
	
	while (in <= inend-4)
	{
		uint8_t c1 = in[0];
		uint8_t c2 = in[1];
		uint8_t c3 = in[2];
		uint8_t c4 = in[3];
		if (UNLIKELY((c1|c2|c3|c4) >= 0x80)) return 0;
		c1 = decode[c1];
		c2 = decode[c2];
		c3 = decode[c3];
		c4 = decode[c4];
		in += 4;
		
	space_again:
		if (UNLIKELY((c1|c2|c3|c4) >= 0x80))
		{
			if ((c1|c2|c3|c4) == SP)
			{
				if (in == inend) return 0;
				if (c1 == SP) { c1 = c2; c2 = SP; }
				if (c2 == SP) { c2 = c3; c3 = SP; }
				if (c3 == SP) { c3 = c4; c4 = SP; }
				c4 = *in++;
				if (c4 & 0x80) return 0;
				c4 = decode[c4];
				goto space_again;
			}
			if (c4 == PA)
			{
				in_real = in; // some goofy tricks to get a few instructions out of the fast path
				in = inend;
				final_chunk = 2;
				c4 = 0;
				if (c3 == PA)
				{
					final_chunk = 1;
					c3 = 0;
					if (c2&15) return 0; // reject padded data where the bits of the partial byte aren't zero
				}
				if (c3&3) return 0;
			}
			
			if ((c1|c2|c3|c4) >= 0x80) return 0;
		}
		
		uint32_t outraw = (c1<<18) | (c2<<12) | (c3<<6) | (c4<<0);
		outptr[0] = outraw>>16;
		outptr[1] = outraw>>8;
		outptr[2] = outraw>>0;
		outptr += 3;
	}
	
	if (final_chunk != 3)
	{
		in = in_real;
		outptr = outptr-3 + final_chunk;
	}
	while (in < inend && *in < 0x80 && decode[(uint8_t)*in] == SP) in++;
	
	if (in != inend) return 0;
	return outptr-out.ptr();
}

array<uint8_t> base64_dec(cstring text)
{
	array<uint8_t> ret;
	ret.resize(base64_dec_len(text.length()));
	size_t actual = base64_dec_raw(ret, text);
	if (!actual) return NULL;
	ret.resize(actual);
	return ret;
}

// this tests encoder too
#include "test.h"
static void do_test(cstring enc, cstring dec)
{
	size_t declen_exp = dec.length();
	if (enc[enc.length()-1] == '=') declen_exp++;
	if (enc[enc.length()-2] == '=') declen_exp++;
	assert_eq(base64_dec_len(enc.length()), declen_exp);
	assert_eq(base64_enc_len(dec.length()), enc.length());
	assert_eq(base64_enc(dec.bytes()), enc);
	assert_eq(cstring(base64_dec(enc)), dec);
}

test("base64", "string,array", "base64")
{
	testcall(do_test("cGxlYXN1cmUu", "pleasure."));
	testcall(do_test("bGVhc3VyZS4=", "leasure." ));
	testcall(do_test("ZWFzdXJlLg==", "easure."  ));
	testcall(do_test("YXN1cmUu",     "asure."   ));
	testcall(do_test("c3VyZS4=",     "sure."    ));
	assert_eq(string(base64_dec(" c G x l Y X N 1 c m U u ")), "pleasure.");
	assert_eq(string(base64_dec(" b G V h c 3 V y Z S 4 = ")), "leasure." );
	assert_eq(string(base64_dec(" Z W F z d X J l L g = = ")), "easure."  );
	assert_eq(string(base64_dec("  Y  X  N  1  c  m  U  u  ")),  "asure."   );
	assert_eq(string(base64_dec("  c  3  V  y  Z  S  4  =  ")),  "sure."    );
	assert(!base64_dec("aaaaaaa$"));
	// blank string should successfully decode to blank array, but successfully decoding zero bytes is indistinguishable from failure
	assert(!base64_dec("A"));
	assert(!base64_dec("A="));
	assert(!base64_dec("A=="));
	assert(!base64_dec("A==="));
	assert(!base64_dec("A===="));
	assert(!base64_dec("AA"));
	assert(!base64_dec("AA="));
	assert( base64_dec("AA=="));
	assert(!base64_dec("AA==="));
	assert(!base64_dec("AA===="));
	assert(!base64_dec("AAA"));
	assert( base64_dec("AAA="));
	assert(!base64_dec("AAA=="));
	assert(!base64_dec("AAA==="));
	assert(!base64_dec("AAA===="));
	assert( base64_dec("AAAA"));
	assert(!base64_dec("AAAA="));
	assert(!base64_dec("AAAA=="));
	assert(!base64_dec("AAAA==="));
	assert(!base64_dec("AAAA===="));
	assert(!base64_dec("AB==")); // no set bits allowed in padding
	assert(!base64_dec("AAB="));
}
