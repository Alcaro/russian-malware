#include "string.h"
#include "endian.h"
#include "simd.h"
#include "test.h"
#ifdef _WIN32
#include <windows.h>
#endif

string smelly_string::ucs1_to_utf8(arrayview<uint8_t> ucs1)
{
#ifdef __SSE2__
	const uint8_t * fast_iter = ucs1.ptr();
	const uint8_t * fast_iter_end = fast_iter + ucs1.size();
	while (fast_iter+16 <= fast_iter_end)
	{
		if (_mm_movemask_epi8(_mm_loadu_si128((__m128i*)fast_iter)) != 0)
			goto slowpath;
		fast_iter += 16;
	}
	while (fast_iter < fast_iter_end)
	{
		if (*fast_iter++ & 0x80)
			goto slowpath;
	}
	return ucs1;
slowpath:
#endif
	
	bytearray ret;
	ret.resize(ucs1.size()*2);
	uint8_t* out_start = ret.ptr();
	uint8_t* out = out_start;
	for (size_t n=0;n<ucs1.size();n++)
	{
		uint32_t codepoint = ucs1[n];
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

string smelly_string::utf16_to_utf8(arrayview<uint16_t> utf16)
{
#ifdef _WIN32
	bytearray ret;
	ret.resize(utf16.size()*3);
	int retlen = WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)utf16.ptr(), utf16.size(), (char*)ret.ptr(), ret.size(), nullptr, nullptr);
	ret.resize(retlen);
	return ret;
#else
	bytearray ret;
	ret.resize(utf16.size()*3);
	uint8_t* out_start = ret.ptr();
	uint8_t* out = out_start;
	for (size_t n=0;n<utf16.size();n++)
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
	ret.resize(out-out_start);
	return ret;
#endif
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

static void fputs_utf8(FILE* f, DWORD n, cstring str)
{
	// I've tried several permutations of
	//  setlocale(LC_ALL, "en_US.UTF-8")
	//  SetConsoleOutputCP(CP_UTF8)
	//  fputws(smelly_string(str), stdout)
	// but they all break in various ways; this is the best I can find
	// (WriteConsoleW fails to process astral planes characters; not sure if that's fixed in the new Terminal)
	HANDLE h = GetStdHandle(n);
	DWORD ignore;
	if (GetConsoleMode(h, &ignore))
	{
		fflush(f);
		array<uint16_t> utf16 = smelly_string::utf8_to_utf16(str);
		WriteConsoleW(h, utf16.ptr(), utf16.size(), nullptr, nullptr);
	}
	else
	{
		fwrite(str.bytes().ptr(), 1, str.length(), f);
	}
}
template<bool use_stderr>
void fputs_utf8(cstring str)
{
	fputs_utf8(use_stderr ? stderr : stdout, use_stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE, str);
}

void puts(cstring str)
{
	fputs_utf8<false>(str);
	fputc('\n', stdout);
}
#endif

test("smelly_string", "", "")
{
	assert_eq(smelly_string::ucs1_to_utf8(cstring("sm\xF6rg\xE5sr\xE4ka").bytes()), "smörgåsräka");
	
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
