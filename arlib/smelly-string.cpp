#include "string.h"
#include "endian.h"
#include "simd.h"
#include "test.h"
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

string smelly_string::ucs1_to_utf8(arrayview<uint8_t> ucs1)
{
	bytearray ret;
	ret.resize(ucs1.size()*2);
	uint8_t* out_start = ret.ptr();
	uint8_t* out = out_start;
	size_t n=0;
	while (n < ucs1.size())
	{
#ifdef __SSE2__
		while (n+16 <= ucs1.size() && _mm_movemask_epi8(_mm_loadu_si128((__m128i*)(ucs1.ptr()+n))) == 0)
		{
			memcpy(out, ucs1.ptr()+n, 16);
			out += 16;
			n += 16;
		}
#endif
		uint32_t codepoint = ucs1[n++];
		if (LIKELY(codepoint < 0x80))
		{
			*out++ = codepoint;
		}
		else
		{
			*out++ = (codepoint>>6) | 0xC0;
			*out++ = (codepoint&0x3F) | 0x80;
		}
	}
	ret.resize(out-out_start);
	return ret;
}

size_t smelly_string::utf16_to_utf8_buf(arrayview<uint16_t> utf16, arrayvieww<uint8_t> utf8)
{
#ifdef _WIN32
	return WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)utf16.ptr(), utf16.size(), (char*)utf8.ptr(), utf8.size(), nullptr, nullptr);
#else
	uint8_t* out = utf8.ptr();
	for (size_t n = 0; n < utf16.size(); n++)
	{
		uint32_t codepoint = utf16[n];
		if (LIKELY(codepoint <= 0x7F))
		{
			*out++ = codepoint;
			continue;
		}
		if (codepoint >= 0xD800 && codepoint <= 0xDBFF && n+1 < utf16.size() && utf16[n+1] >= 0xDC00 && utf16[n+1] <= 0xDFFF)
		{
			codepoint = 0x10000 + ((codepoint-0xD800)<<10) + (utf16[n+1]-0xDC00);
			n++;
		}
		out += string::codepoint(out, codepoint);
	}
	return out - utf8.ptr();
#endif
}

string smelly_string::utf16_to_utf8(arrayview<uint16_t> utf16)
{
	bytearray ret;
	ret.resize(utf16.size()*3);
	size_t retlen = utf16_to_utf8_buf(utf16, ret);
	ret.resize(retlen);
	return ret;
}

string smelly_string::utf16l_to_utf8(arrayview<uint8_t> utf16)
{
	if (END_LITTLE)
	{
		return utf16_to_utf8(arrayview<uint16_t>((uint16_t*)utf16.ptr(), utf16.size()/2));
	}
	else
	{
		abort(); // todo
	}
}

#ifdef _WIN32
array<uint16_t> smelly_string::utf8_to_utf16(cstring utf8)
{
	array<uint16_t> ret;
	ret.resize(utf8.length()+1);
	int len = MultiByteToWideChar(CP_UTF8, 0, (char*)utf8.bytes().ptr(), utf8.length(), (wchar_t*)ret.ptr(), ret.size());
	ret.resize(len);
	return ret;
}

static void fputs_utf8(cstring str, FILE* f)
{
	int fd = _fileno(f);
	if (fd >= 0)
	{
		HANDLE h = (HANDLE)_get_osfhandle(fd);
		// I could use GetStdHandle instead, but that'd require passing around things other than a FILE*
		if (h != INVALID_HANDLE_VALUE)
		{
			DWORD ignore;
			if (GetConsoleMode(h, &ignore))
			{
				fflush(f);
				array<uint16_t> utf16 = smelly_string::utf8_to_utf16(str);
				WriteConsoleW(h, utf16.ptr(), utf16.size(), nullptr, nullptr);
				return;
			}
		}
	}
	fwrite(str.bytes().ptr(), 1, str.length(), f);
}

void puts(cstring str)
{
	fputs_utf8(str, stdout);
	fputc('\n', stdout);
}
#endif

test("smelly_string", "", "")
{
	// ensure strings are utf8
	static_assert("ö"[0] == (char)0xC3); // extra cast because char is signed on x86
	static_assert("ö"[1] == (char)0xB6);
	static_assert("ö"[2] == (char)0x00);
#define E32 "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
#define SMR_UCS1 "sm\xF6rg\xE5sr\xE4ka"
#define SMR_U8 "smörgåsräka"
	assert_eq(smelly_string::ucs1_to_utf8(cstring(SMR_UCS1).bytes()), SMR_U8);
	assert_eq(smelly_string::ucs1_to_utf8(cstring(E32 SMR_UCS1 E32).bytes()), E32 SMR_U8 E32);
	
	uint16_t utf16[] = { 's','m',0xF6,'r','g',0xE5,'s','r',0xE4,'k','a',0x2603,0xD83E,0xDD14 };
	assert_eq(smelly_string::utf16_to_utf8(utf16), "smörgåsräka☃🤔");
	
#ifdef _WIN32
	assert_eq(smelly_string::utf8_to_utf16("smörgåsräka☃🤔"), utf16);
#endif
}

//test("smelly_puts", "", "")
//{
//	string str = "smörgåsräka☃🤔\n";
//	puts((cstring)str);
//	puts((const char*)str);
//	WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), str.bytes().ptr(), str.length(), nullptr, nullptr);
//	puts(SetConsoleOutputCP(CP_UTF8) ? "true" : "false");
//	puts((cstring)str);
//	puts((const char*)str);
//	WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), str.bytes().ptr(), str.length(), nullptr, nullptr);
//}
