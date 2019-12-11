#include "stringconv.h"
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <errno.h>
#include "test.h"

#define FROMFUNC(t,frt,f) \
	bool fromstring(cstring s, t& out) \
	{ \
		out = 0; \
		if (!s || isspace(s[0])) return false; \
		if ((t)-1 > (t)0 && s[0]=='-') return false; \
		auto tmp_s = s.c_str(); \
		const char * tmp_cp = tmp_s; \
		char * tmp_cpo; \
		if (sizeof(t) == sizeof(frt)) errno = 0; \
		frt ret = f(tmp_cp, &tmp_cpo, 10); \
		if (tmp_cpo != tmp_cp + s.length()) return false; \
		if ((t)ret != (frt)ret) return false; \
		if (sizeof(t) == sizeof(frt) && errno == ERANGE) return false; \
		out = ret; \
		return true; \
	}

// if the input does not start with 0x, return it unchanged
// otherwise, return a substring of input that strtoul rejects, for example pointer to the x (pointer to the nul would work too)
static const char * drop0x(const char * in)
{
	if (in[0]=='0' && (in[1]=='x' || in[1]=='X')) return in+1;
	else return in;
}

#define FROMFUNCHEX(t,frt,f) \
	FROMFUNC(t,frt,f) \
	bool fromstringhex(cstring s, t& out) \
	{ \
		out = 0; \
		auto tmp_s = s.c_str(); \
		const char * tmp_cp = tmp_s; \
		if (!*tmp_cp || isspace(*tmp_cp) || *tmp_cp == '-') return false; \
		char * tmp_cpo; \
		if (sizeof(t) == sizeof(frt)) errno = 0; \
		frt ret = f(drop0x(tmp_cp), &tmp_cpo, 16); \
		if (tmp_cpo != tmp_cp + s.length()) return false; \
		if ((t)ret != (frt)ret) return false; \
		if (sizeof(t) == sizeof(frt) && errno == ERANGE) return false; \
		out = ret; \
		return true; \
	}

FROMFUNC(     signed char,    signed long, strtol)
FROMFUNCHEX(unsigned char,  unsigned long, strtoul)
FROMFUNC(     signed short,   signed long, strtol)
FROMFUNCHEX(unsigned short, unsigned long, strtoul)
FROMFUNC(     signed int,     signed long, strtol)
FROMFUNCHEX(unsigned int,   unsigned long, strtoul)
FROMFUNC(     signed long,    signed long, strtol)
FROMFUNCHEX(unsigned long,  unsigned long, strtoul)
FROMFUNC(     signed long long,   signed long long, strtoll)
FROMFUNCHEX(unsigned long long, unsigned long long, strtoull)

static bool fromstring_float(cstring s, double& out, double(*strtod_l)(const char*,char**))
{
	auto tmp_s = s.c_str();
	const char * tmp_cp = tmp_s;
	if (*tmp_cp != '-' && !isdigit(*tmp_cp)) return false;
	char * tmp_cpo;
	out = strtod_l(drop0x(tmp_cp), &tmp_cpo);
	if (tmp_cpo != tmp_cp + s.length()) return false;
	if (!isdigit(tmp_cpo[-1])) return false;
	if (out == HUGE_VAL || out == -HUGE_VAL) return false;
	return true;
}

bool fromstring(cstring s, double& out)
{
	out = 0;
	double ret;
	if (!fromstring_float(s, ret, strtod)) return false;
	out = ret;
	return true;
}

bool fromstring(cstring s, float& out)
{
	out = 0;
	double ret;
	if (!fromstring_float(s, ret, [](const char * str, char** str_end)->double { return strtof(str, str_end); })) return false;
	out = ret;
	return true;
}

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

string tostring(double val)
{
	char ret[512]; // max double is about 1.79 * 10^308, give it space for that and a little more https://stackoverflow.com/q/7235456
	sprintf(ret, "%f", val);
	char* end = strchr(ret, '\0');
	while (end > ret && end[-1] == '0') end--;
	while (end > ret && end[-1] == '.') end--;
	*end = '\0';
	return ret;
}


string tostringhex(arrayview<byte> val)
{
	string ret;
	arrayvieww<byte> retb = ret.construct(val.size()*2);
	for (size_t i=0;i<val.size();i++)
	{
		sprintf((char*)retb.slice(i*2, 2).ptr(), "%.2X", val[i]);
	}
	return ret;
}

bool fromstringhex(cstring s, arrayvieww<byte> val)
{
	if (val.size()*2 != s.length()) return false;
	bool ok = true;
	for (size_t i=0;i<val.size();i++)
	{
		ok &= fromstringhex(s.substr(i*2, i*2+2), val[i]);
	}
	return ok;
}
bool fromstringhex(cstring s, array<byte>& val)
{
	val.resize(s.length()/2);
	return fromstringhex(s, (arrayvieww<byte>)val);
}


template<typename T> void testunhex(const char * S, T V)
{
	T a;
	assert_eq(fromstringhex(S, a), true);
	assert_eq(a, V);
}
template<typename T> void testundec(const char * S, T V)
{
	T a;
	assert_eq(fromstring(S, a), true);
	assert_eq(a, V);
}
test("string conversion", "", "string")
{
	testcall(testunhex<unsigned char     >("aa", 0xaa));
	testcall(testunhex<unsigned char     >("AA", 0xAA));
	testcall(testunhex<unsigned short    >("aaaa", 0xaaaa));
	testcall(testunhex<unsigned short    >("AAAA", 0xAAAA)); // AAAAAAAAAAAHHHHH MOTHERLAND http://www.albinoblacksheep.com/flash/end
	testcall(testunhex<unsigned int      >("aaaaaaaa", 0xaaaaaaaa));
	testcall(testunhex<unsigned int      >("AAAAAAAA", 0xAAAAAAAA));
	testcall(testunhex<unsigned long     >("aaaaaaaa", 0xaaaaaaaa)); // long is sometimes 64bit, but good enough
	testcall(testunhex<unsigned long     >("AAAAAAAA", 0xAAAAAAAA));
	testcall(testunhex<unsigned long long>("aaaaaaaaaaaaaaaa", 0xaaaaaaaaaaaaaaaa));
	testcall(testunhex<unsigned long long>("AAAAAAAAAAAAAAAA", 0xAAAAAAAAAAAAAAAA));
	
	testcall(testundec<int>("123", 123));
	testcall(testundec<int>("0123", 123));
	testcall(testundec<int>("00123", 123));
	testcall(testundec<int>("000123", 123));
	testcall(testundec<int>("0", 0));
	testcall(testundec<int>("09", 9)); // no octal allowed
	testcall(testundec<double>("123", 123));
	testcall(testundec<double>("0123", 123));
	testcall(testundec<double>("00123", 123));
	testcall(testundec<double>("000123", 123));
	testcall(testundec<double>("0", 0));
	testcall(testundec<double>("0e1", 0)); // this input has triggered the 0x detector, making it fail
	testcall(testundec<double>("0e-1", 0));
	testcall(testundec<double>("0e+1", 0));
	testcall(testundec<double>("11e1", 110));
	testcall(testundec<double>("11e+1", 110));
	testcall(testundec<double>("-1", -1));
	testcall(testundec<float>("2.5", 2.5));
	testcall(testundec<float>("2.5e+1", 25));
	
	byte foo[4] = {0x12,0x34,0x56,0x78};
	assert_eq(tostringhex(arrayview<byte>(foo)), "12345678");
	
	assert(fromstringhex("87654321", arrayvieww<byte>(foo)));
	assert_eq(foo[0], 0x87); assert_eq(foo[1], 0x65); assert_eq(foo[2], 0x43); assert_eq(foo[3], 0x21);
	
	array<byte> bar;
	assert(fromstringhex("1234567890", bar));
	assert_eq(bar.size(), 5);
	assert_eq(bar[0], 0x12);
	assert_eq(bar[1], 0x34);
	assert_eq(bar[2], 0x56);
	assert_eq(bar[3], 0x78);
	assert_eq(bar[4], 0x90);
	
	assert(!fromstringhex("123456", arrayvieww<byte>(foo))); // not 4 bytes
	assert(!fromstringhex("1234567", bar)); // odd length
	assert(!fromstringhex("0x123456", bar)); // no 0x allowed
	
	uint8_t u8;
	int32_t i32;
	uint32_t u32;
	uint64_t u64;
	float f;
	double d;
	assert(!fromstring("", u32)); // this isn't an integer
	assert(!fromstringhex("", u32));
	assert(!fromstring("", f));
	
	assert(!fromstring("2,5", f)); // this is not the decimal separator, has never been and will never be
	
	string s = "42" + string::nul();
	assert(!fromstring(s, u32)); // no nul allowed
	assert(!fromstring(s, f));
	assert(!fromstringhex(s, u32));
	
	assert(!fromstring("-0", u32)); // if -1 is invalid, -0 should be too
	assert(!fromstringhex("-0", u32));
	assert( fromstring("-0", i32)); // but if -1 is valid, -0 should be too
	assert(!fromstring(" 42", u32));
	assert(!fromstring("0x42", u32));
	assert(!fromstringhex(" 42", u32));
	assert(!fromstringhex("0x42", u32));
	assert(!fromstring(" 42", f));
	assert(!fromstring("0x42", f));
	
	assert(!fromstring("1e", f));
	assert(!fromstring("1e+", f));
	assert(!fromstring("1e-", f));
	assert(!fromstring("1.", f));
	assert(!fromstring(".1", f));
	
	try_fromstring("123", i32); // just to ensure they compile
	try_fromstring("123", s);
	
	// 0.0000000000000000000000000703853100000000000000000000000000000000 <- input
	// 0.0000000000000000000000000703853069185120912085918801714030697411 <- float closest to the input
	// 0.0000000000000000000000000703853100000000022281692450609677778769 <- double closest to the input
	// 0.0000000000000000000000000703853130814879132477466099505324860128 <- float closest to the above double
	assert(fromstring("7.038531e-26", f));
	assert_ne(f, (float)7.038531e-26);
	assert_eq(f, 7.038531e-26f);
	static_assert(7.038531e-26f != (float)7.038531e-26); // test the compiler too
	
	// ensure overflow fails
	assert(!fromstring("256", u8));
	assert(!fromstring("-1", u8));
	assert(!fromstring("4294967296", u32));
	assert( fromstring( "2147483647", i32));
	assert( fromstring("-2147483647", i32));
	assert(!fromstring( "2147483648", i32));
	assert( fromstring("-2147483648", i32));
	assert(!fromstring( "2147483649", i32));
	assert(!fromstring("-2147483649", i32));
	assert( fromstring("18446744073709551615", u64));
	assert(!fromstring("18446744073709551616", u64));
	assert( fromstringhex("FFFFFFFF", u32));
	assert(!fromstringhex("100000000", u32));
	assert( fromstringhex("FFFFFFFFFFFFFFFF", u64));
	assert(!fromstringhex("10000000000000000", u64));
	assert( fromstring("340282346638528859811704183484516925440", f)); // max possible float; a few higher values round down to that,
	assert(!fromstring("999999999999999999999999999999999999999", f)); // but anything too big should be rejected
	assert( fromstring("1797693134862315708145274237317043567980705675258449965989174768031572607800285387605895586327668781715"
	                   "4045895351438246423432132688946418276846754670353751698604991057655128207624549009038932894407586850845"
	                   "5133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368", d));
	assert(!fromstring("9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999"
	                   "9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999"
	                   "9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999", d));
	assert(fromstring("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                    "0000000000000000000000049406564584124654417656879286822137236505980261432476442558568250067550727020"
	                    "8751865299836361635992379796564695445717730926656710355939796398774796010781878126300713190311404527"
	                    "8458171678489821036887186360569987307230500063874091535649843873124733972731696151400317153853980741"
	                    "2623856559117102665855668676818703956031062493194527159149245532930545654440112748012970999954193198"
	                    "9409080416563324524757147869014726780159355238611550134803526493472019379026810710749170333222684475"
	                    "3335720832431936092382893458368060106011506169809753078342277318329247904982524730776375927247874656"
	                    "0847782037344696995336470179726777175851256605511991315048911014510378627381672509558373897335989936"
	                    "64809941164205702637090279242767544565229087538682506419718265533447265625", d));
	assert_ne(d, 0); // smallest possible subnormal float
	assert_eq(d/2, 0); // halving it rounds to zero
}
