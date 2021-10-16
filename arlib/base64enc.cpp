#include "base64.h"
#include "endian.h"

static const char encode[64] = {
	'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',
	'0','1','2','3','4','5','6','7','8','9','+','/' };

void base64_enc_raw(arrayvieww<uint8_t> out, arrayview<uint8_t> bytes)
{
	if (!bytes.size()) return;
	
	uint8_t* outp = out.ptr();
	const uint8_t* inp = bytes.ptr();
	const uint8_t* inpe = inp + bytes.size();
	
	while (inp+4 <= inpe)
	{
		uint32_t three = readu_be32(inp) >> 8;
		outp[0] = encode[(three>>18)   ];
		outp[1] = encode[(three>>12)&63];
		outp[2] = encode[(three>>6 )&63];
		outp[3] = encode[(three>>0 )&63];
		inp += 3;
		outp += 4;
	}
	
	uint32_t three = inp[0]<<16;
	if (inp+1 < inpe) three |= inp[1]<<8;
	if (inp+2 < inpe) three |= inp[2]<<0;
	
	outp[0] = encode[(three>>18)   ];
	outp[1] = encode[(three>>12)&63];
	outp[2] = encode[(three>>6 )&63];
	outp[3] = encode[(three>>0 )&63];
	
	if (inp+1 >= inpe) outp[2] = '='; // don't move the outp[2,3] above into elses, it optimizes better this way
	if (inp+2 >= inpe) outp[3] = '=';
}

string base64_enc(arrayview<uint8_t> bytes)
{
	string ret;
	base64_enc_raw(ret.construct(base64_enc_len(bytes.size())), bytes);
	return ret;
}
