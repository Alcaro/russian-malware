#include "stringconv.h"

size_t tostring_len(  signed long long val) { return ilog10(abs(val)|1) + (val < 0) + 1; }
size_t tostring_len(unsigned long long val) { return ilog10(val|1) + 1; }

size_t tostring_ptr(char* buf, unsigned long long val, size_t len)
{
	char* iter = buf+len;
	while (iter > buf)
	{
		*--iter = '0'+val%10;
		val /= 10;
	}
	return len;
}
size_t tostring_ptr(char* buf, signed long long val, size_t len)
{
	if (val < 0) { tostring_ptr(buf, (unsigned long long)-val, len); *buf = '-'; return len; }
	else return tostring_ptr(buf, (unsigned long long)val, len);
}

template<typename T>
string tostring_inner(T val)
{
	size_t len = tostring_len(val);
	string ret;
	char* ptr = (char*)ret.construct(len).ptr();
	tostring_ptr(ptr, val, len);
	return ret;
}
string tostring(  signed long long val) { return tostring_inner(val); }
string tostring(unsigned long long val) { return tostring_inner(val); }


static const char hexdigits[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
template<typename T>
string tostringhex_inner(T val, size_t mindigits)
{
	uint8_t buf[16];
	uint8_t* bufend = buf+sizeof(buf);
	uint8_t* iter = bufend;
	do {
		*--iter = hexdigits[val%16];
		val /= 16;
	} while (val);
	while ((size_t)(bufend-iter) < mindigits) *--iter = '0';
	return string(bytesr(iter, bufend-iter));
}
string tostringhex(unsigned long long val, size_t mindigits) { return tostringhex_inner(val, mindigits); }

string tostringhex(arrayview<uint8_t> val)
{
	string ret;
	arrayvieww<uint8_t> retb = ret.construct(val.size()*2);
	for (size_t i=0;i<val.size();i++)
	{
		retb[i*2+0] = hexdigits[val[i]>>4];
		retb[i*2+1] = hexdigits[val[i]&15];
	}
	return ret;
}

size_t tostringhex_ptr(char* buf, unsigned long long val, size_t len)
{
	char* iter = buf+len;
	while (iter > buf)
	{
		*--iter = hexdigits[val%16];
		val /= 16;
	}
	return len;
}



template<typename Tu> bool fromstring_int_inner(const char * in, const char * end, Tu& out)
{
	out = 0;
	if (UNLIKELY(in == end)) return false;
	Tu t_max = (Tu)-1;
	while (in != end)
	{
		if (LIKELY(*in >= '0' && *in <= '9'))
		{
			if (UNLIKELY(out >= t_max/10))
			{
				if (out > t_max/10) return false;
				if (*in > (char)(t_max%10+'0')) return false;
			}
			out = out*10 + *in-'0';
		}
		else return false;
		in++;
	}
	return true;
}

template<typename Tu> bool fromstring_uint_real(cstring s, Tu& out)
{
	const char * start = (char*)s.bytes().ptr();
	const char * end = start + s.length();
	return fromstring_int_inner<Tu>(start, end, out);
}

template<typename Tu, typename Ts> bool fromstring_int_real(cstring s, Ts& out)
{
	out = 0;
	const char * start = (char*)s.bytes().ptr();
	const char * end = start + s.length();
	
	if (UNLIKELY(start == end)) return false;
	bool neg = (*start == '-');
	start += neg;
	Tu tmp;
	
	if (UNLIKELY(!fromstring_int_inner<Tu>(start, end, tmp))) return false;
	
	if (neg)
	{
		out = (Ts)-tmp;
		return (out <= 0);
	}
	else
	{
		out = (Ts)tmp;
		return (out >= 0);
	}
}

template<typename Tu> bool fromstring_hex_real(const char * s, size_t len, Tu& out)
{
	const char * start = s;
	const char * end = s + len;
	
	out = 0;
	if (UNLIKELY(start == end)) return false;
	while (start < end && *start == '0') start++;
	if (UNLIKELY(end-start > (ptrdiff_t)sizeof(Tu)*2)) return false;
	
	while (start != end)
	{
		out <<= 4;
		char upper = *start & ~0x20;
		if (*start >= '0' && *start <= '9') out += *start-'0';
		else if (upper >= 'A' && upper <= 'F') out += upper-'A'+10;
		else return false;
		start++;
	}
	return true;
}

template<typename Tu> inline bool fromstring_uint(cstring s, Tu& out)
{
	// funny wrappers to minimize number of fromstring_(u)int_real instantiations, for size reasons
	if constexpr (sizeof(Tu) > sizeof(uintptr_t)) return fromstring_uint_real<Tu>(s, out);
	if constexpr (sizeof(Tu) == sizeof(uintptr_t)) return fromstring_uint_real<uintptr_t>(s, (uintptr_t&)out);
	uintptr_t tmp;
	bool ret = fromstring_uint_real<uintptr_t>(s, tmp);
	out = (Tu)tmp;
	return (ret && tmp == (uintptr_t)out);
}

template<typename Tu, typename Ts> inline bool fromstring_int(cstring s, Ts& out)
{
	if constexpr (sizeof(Ts) > sizeof(intptr_t)) return fromstring_int_real<Tu, Ts>(s, out);
	if constexpr (sizeof(Ts) == sizeof(intptr_t)) return fromstring_int_real<uintptr_t, intptr_t>(s, (intptr_t&)out);
	intptr_t tmp;
	bool ret = fromstring_int_real<uintptr_t, intptr_t>(s, tmp);
	out = (Ts)tmp;
	return (ret && tmp == (intptr_t)out);
}

template<typename Tu> inline bool fromstring_hex(cstring s, Tu& out)
{
	const char * ptr = (char*)s.bytes().ptr();
	size_t len = s.length();
	
	if constexpr (sizeof(Tu) > sizeof(uintptr_t)) return fromstring_hex_real<Tu>(ptr, len, out);
	if constexpr (sizeof(Tu) == sizeof(uintptr_t)) return fromstring_hex_real<uintptr_t>(ptr, len, (uintptr_t&)out);
	uintptr_t tmp;
	bool ret = fromstring_hex_real<uintptr_t>(ptr, len, tmp);
	out = (Tu)tmp;
	return (ret && tmp == (uintptr_t)out);
}

	
template<typename Tu> inline bool fromstring_hex(const char * s, size_t len, Tu& out)
{
	if constexpr (sizeof(Tu) > sizeof(uintptr_t)) return fromstring_hex_real<Tu>(s, len, out);
	if constexpr (sizeof(Tu) == sizeof(uintptr_t)) return fromstring_hex_real<uintptr_t>(s, len, (uintptr_t&)out);
	uintptr_t tmp;
	bool ret = fromstring_hex_real<uintptr_t>(s, len, tmp);
	out = (Tu)tmp;
	return (ret && tmp == (uintptr_t)out);
}

#define FROMFUNC(Tu, Ts) \
	bool fromstring(cstring s, Tu& out) { return fromstring_uint<Tu>(s, out); } \
	bool fromstring(cstring s, Ts& out) { return fromstring_int<Tu, Ts>(s, out); } \
	bool fromstringhex(cstring s, Tu& out) { return fromstring_hex<Tu>(s, out); } \
	bool fromstringhex_ptr(const char * s, size_t len, Tu& out) { return fromstring_hex<Tu>(s, len, out); }
FROMFUNC(unsigned char,      signed char)
FROMFUNC(unsigned short,     signed short)
FROMFUNC(unsigned int,       signed int)
FROMFUNC(unsigned long,      signed long)
FROMFUNC(unsigned long long, signed long long)
#undef FROMFUNC


bool fromstring(cstring s, bool& out)
{
	if (s == "false" || s == "0")
	{
		out = false;
		return true;
	}
	
	if (s == "true" || s == "1")
	{
		out = true;
		return true;
	}
	
	out = false;
	return false;
}

bool fromstringhex(cstring s, arrayvieww<uint8_t> val)
{
	if (val.size()*2 != s.length()) return false;
	bool ok = true;
	for (size_t i=0;i<val.size();i++)
	{
		ok &= fromstringhex(s.substr(i*2, i*2+2), val[i]);
	}
	return ok;
}
bool fromstringhex(cstring s, array<uint8_t>& val)
{
	val.resize(s.length()/2);
	return fromstringhex(s, (arrayvieww<uint8_t>)val);
}




#include "test.h"
#include <math.h>

template<typename T> void testunhex(const char * S, T V)
{
	T a;
	assert_eq(fromstringhex(S, a), true);
	test_nothrow { assert_eq(a, V); }
}
template<typename T> void testundec(const char * S, T V)
{
	test_nothrow
	{
		T a;
		assert(fromstring(S, a));
		assert_eq(a, V); // if it's an integer type, just assert normally
	}
}

test("string conversion - integer", "", "string")
{
	uint8_t u8;
	int32_t i32;
	int64_t i64;
	uint32_t u32;
	uint64_t u64;
	
	testcall(testundec<int>("123", 123));
	testcall(testundec<int>("0123", 123)); // no octal allowed
	testcall(testundec<int>("00123", 123));
	testcall(testundec<int>("000123", 123));
	testcall(testundec<int>("09", 9));
	testcall(testundec<int>("0", 0));
	testcall(testundec<int>("0000", 0));
	
	// just to verify that all 10 work, C's integer types are absurd
	testcall(testundec<signed char     >("123", 123));
	testcall(testundec<signed short    >("12345", 12345));
	testcall(testundec<signed int      >("1234567890", 1234567890));
	testcall(testundec<signed long     >("1234567890", 1234567890));
	testcall(testundec<signed long long>("1234567890123456789", 1234567890123456789));
	testcall(testundec<unsigned char     >("123", 123));
	testcall(testundec<unsigned short    >("12345", 12345));
	testcall(testundec<unsigned int      >("1234567890", 1234567890));
	testcall(testundec<unsigned long     >("1234567890", 1234567890));
	testcall(testundec<unsigned long long>("12345678901234567890", 12345678901234567890u)); // occasional u suffixes because
	testcall(testunhex<unsigned char     >("aa", 0xaa));       // 'warning: integer constant is so large that it is unsigned'
	testcall(testunhex<unsigned char     >("AA", 0xAA));
	testcall(testunhex<unsigned short    >("aaaa", 0xaaaa));
	testcall(testunhex<unsigned short    >("AAAA", 0xAAAA));
	testcall(testunhex<unsigned int      >("aaaaaaaa", 0xaaaaaaaa));
	testcall(testunhex<unsigned int      >("AAAAAAAA", 0xAAAAAAAA));
	testcall(testunhex<unsigned long     >("aaaaaaaa", 0xaaaaaaaa)); // long is sometimes 64bit, but good enough
	testcall(testunhex<unsigned long     >("AAAAAAAA", 0xAAAAAAAA));
	testcall(testunhex<unsigned long long>("aaaaaaaaaaaaaaaa", 0xaaaaaaaaaaaaaaaa));
	testcall(testunhex<unsigned long long>("AAAAAAAAAAAAAAAA", 0xAAAAAAAAAAAAAAAA));
	
	uint8_t foo[4] = {0x12,0x34,0x56,0x78};
	assert_eq(tostringhex(arrayview<uint8_t>(foo)), "12345678");
	assert_eq(tostring_dbg(arrayview<uint8_t>(foo)), "[18,52,86,120]");
	assert_eq(strlen(tostring_dbg("abc")), 3); // no [97,98,99,0] allowed
	assert_eq(tostring(18446744073709551615u), "18446744073709551615");
	assert_eq(tostring((int8_t)-128), "-128");
	assert_eq(tostring((int16_t)-32768), "-32768");
	assert_eq(tostring((int32_t)-2147483648), "-2147483648");
	assert_eq(tostring((int64_t)-9223372036854775808u), "-9223372036854775808");
	assert_eq(tostringhex<4>(0x0123), "0123");
	assert_eq(tostringhex(18446744073709551615u), "FFFFFFFFFFFFFFFF");
	
	assert(fromstringhex("87654321", arrayvieww<uint8_t>(foo)));
	assert_eq(foo[0], 0x87); assert_eq(foo[1], 0x65); assert_eq(foo[2], 0x43); assert_eq(foo[3], 0x21);
	
	array<uint8_t> bar;
	assert(fromstringhex("1234567890", bar));
	assert_eq(bar.size(), 5);
	assert_eq(bar[0], 0x12);
	assert_eq(bar[1], 0x34);
	assert_eq(bar[2], 0x56);
	assert_eq(bar[3], 0x78);
	assert_eq(bar[4], 0x90);
	
	assert(!fromstringhex("123456", arrayvieww<uint8_t>(foo))); // not 4 bytes
	assert(!fromstringhex("1234567", bar)); // odd length
	assert(!fromstringhex("0x123456", bar)); // no 0x allowed
	
	u32 = 42; assert(!fromstring("", u32)); assert_eq(u32, 0); // this isn't an integer
	i32 = 42; assert(!fromstring("", i32)); assert_eq(i32, 0);
	u32 = 42; assert(!fromstringhex("", u32)); assert_eq(u32, 0);
	
	string s = "42" + string::nul();
	assert(!fromstring(s, i32)); // no nul allowed
	assert(!fromstring(s, u32));
	assert(!fromstringhex(s, u32));
	
	assert(!fromstring("-0", u32)); // if -1 is invalid, -0 should be too
	assert(!fromstringhex("-0", u32));
	assert( fromstring("-0", i32)); // but if -1 is valid, -0 should be too
	assert(!fromstring("+1", u32)); // no + prefix allowed
	assert(!fromstring("+1", i32));
	assert(!fromstring(" 42", u32));
	assert(!fromstring("0x42", u32));
	assert(!fromstringhex(" 42", u32));
	assert(!fromstringhex("0x42", u32));
	
	// ensure overflow fails
	assert(!fromstring("256", u8));
	assert(!fromstring("-1", u8));
	assert( fromstring("4294967295", u32));
	assert(!fromstring("4294967296", u32));
	assert( fromstring( "2147483647", i32));
	assert( fromstring("-2147483647", i32));
	assert(!fromstring( "2147483648", i32));
	assert( fromstring("-2147483648", i32));
	assert(!fromstring( "2147483649", i32));
	assert(!fromstring("-2147483649", i32));
	assert( fromstring("18446744073709551615", u64));
	assert(!fromstring("18446744073709551616", u64));
	assert( fromstring("9223372036854775808", u64));
	assert(!fromstring("9223372036854775808", i64));
	assert( fromstring("9223372036854775807", i64));
	assert( fromstringhex("0FF", u8));
	assert(!fromstringhex("100", u8));
	assert( fromstringhex("0FFFFFFFF", u32));
	assert(!fromstringhex("100000000", u32));
	assert( fromstringhex("0FFFFFFFFFFFFFFFF", u64));
	assert(!fromstringhex("10000000000000000", u64));
	assert( fromstringhex("0000000000000000FFFFFFFFFFFFFFFF", u64));
	
	assert_eq(format("abc",1,-2,fmt_pad<4>(4u),fmt_hex((uint16_t)0xAA),fmt_hex<6>((uint16_t)0xAAA)), "abc1-2000400AA000AAA");
}
