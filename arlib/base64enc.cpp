#include "base64.h"

static const char encode[64] = {
	'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',
	'0','1','2','3','4','5','6','7','8','9','+','/' };

void base64_enc_raw(arrayvieww<uint8_t> out, arrayview<uint8_t> bytes)
{
	uint8_t* outp = out.ptr();
	const uint8_t* inp = bytes.ptr();
	const uint8_t* inpe = inp + bytes.size();
	
	while (inp+3 <= inpe)
	{
		uint32_t three = inp[0]<<16 | inp[1]<<8 | inp[2];
		
		*(outp++) = encode[(three>>18)&63];
		*(outp++) = encode[(three>>12)&63];
		*(outp++) = encode[(three>>6 )&63];
		*(outp++) = encode[(three>>0 )&63];
		inp += 3;
	}
	
	if (inp+0 == inpe) {}
	if (inp+1 == inpe)
	{
		uint32_t three = inp[0]<<16;
		
		*(outp++) = encode[(three>>18)&63];
		*(outp++) = encode[(three>>12)&63];
		*(outp++) = '=';
		*(outp++) = '=';
	}
	if (inp+2 == inpe)
	{
		uint32_t three = inp[0]<<16 | inp[1]<<8;
		
		*(outp++) = encode[(three>>18)&63];
		*(outp++) = encode[(three>>12)&63];
		*(outp++) = encode[(three>>6 )&63];
		*(outp++) = '=';
	}
}

string base64_enc(arrayview<uint8_t> bytes)
{
	string ret;
	base64_enc_raw(ret.construct(base64_enc_len(bytes.size())), bytes);
	return ret;
}
