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

template<typename T, T(*strtod)(const char*,char**)>
bool fromstring_float(cstring s, T& out)
{
	out = 0;
	auto tmp_s = s.c_str();
	const char * tmp_cp = tmp_s;
	if (*tmp_cp != '-' && !isdigit(*tmp_cp)) goto maybe_inf;
	char * tmp_cpo;
	out = strtod(drop0x(tmp_cp), &tmp_cpo);
	if (tmp_cpo != tmp_cp + s.length()) goto maybe_inf;
	if (!isdigit(tmp_cpo[-1])) goto maybe_inf;
	if (out == HUGE_VAL || out == -HUGE_VAL) goto maybe_inf;
	return true;
maybe_inf:
	if (s == "inf") { out = HUGE_VAL; return true; }
	if (s == "-inf") { out = -HUGE_VAL; return true; }
	if (s == "nan") { out = NAN; return true; }
	return false;
}
bool fromstring(cstring s, double& out) { return fromstring_float<double, strtod>(s, out); }
bool fromstring(cstring s, float& out) { return fromstring_float<float, strtof>(s, out); }

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

// Removes unnecessary zeroes (trailing in fraction, leading in exponent) from a number from printf %#f or %#e.
static void flatten_zeroes(char* str)
{
	char* e = strchr(str, 'e');
	char* end = e ? e : strchr(str, '\0');
	while (end[-1] == '0') end--;
	if (end[-1] == '.') end--;
	
	if (e)
	{
		*(end++) = *(e++); // e
		*(end++) = *(e++); // + or -
		if (*e == '0') e++;
		while (*e) *(end++) = *(e++);
	}
	*end = '\0';
}
template<typename T, typename Ti, T(*strtod)(const char*,char**), int minprec>
string tostring_float(T f)
{
	static_assert(std::numeric_limits<T>::is_iec559);
	static_assert(sizeof(T) == sizeof(Ti));
	
	if (isnan(f)) return "nan";
	if (isinf(f)) return "-inf" + !signbit(f);
	
	int prec = minprec;
	
	char fmt[] = "%#.*e";
	if ((fabs(f) >= (T)0.00001 && fabs(f) < (T)10000000000000000.0) || f==0.0)
	{
		fmt[4] = 'f'; // decimal notation
		prec = minprec - log10(f);
		if (prec < 0) prec = 0;
		if (prec > minprec) prec = minprec;
	}
	if (!isnormal(f)) prec = 0;
	
	while (true)
	{
		char ret[64];
		sprintf(ret, fmt, prec, f);
		T parsed = strtod(ret, nullptr);
		if (parsed != f)
		{
			// For a few numbers, like 1.262177448e-29f, rounding to 8 digits parses as the previous float,
			// but incrementing the last digit parses correctly.
			Ti f_i;
			memcpy(&f_i, &f, sizeof(f_i));
			Ti parsed_i;
			memcpy(&parsed_i, &parsed, sizeof(parsed_i));
			if (f_i-1 == parsed_i)
			{
				char* last = strchr(ret, 'e');
				if (!last) last = strchr(ret, '\0');
				last[-1]++; // ignore if that's a 9 and overflows, it would've been caught on the previous digit
				parsed = strtod(ret, nullptr);
			}
			// There's no need to worry about the other direction (where the best answer would be decreasing last[-1]).
			// Proof: Assume there is such a number. Call it N. Let N-1 and N+1 be the two closest floats.
			// Also assume, without loss of generality, that 1.23 parses as N, but rounding N to three digits yields 1.24.
			// -> N's full decimal expansion is 1.235 or greater (otherwise it wouldn't round to 1.24)
			// -> N-1 is 1.225 or less (otherwise 1.23 wouldn't parse as N)
			// -> N+1's full decimal expansion is 1.245 or less (otherwise 1.24 would parse as N)
			// -> N - N-1 >= 0.010, N - N+1 <= 0.010
			// -> the epsilon grows as the number shrinks
			// -> impossible. Floats don't work that way.
			// The opposite can happen (and is handled), but it too requires an epsilon change, i.e. N is an exact power of two.
		}
		if (parsed == f)
		{
			flatten_zeroes(ret);
			return ret;
		}
		// it's possible to do a binary search instead, but all likely inputs only have a few digits anyways, so better keep it simple
		prec++;
	}
}
string tostring(double f) { return tostring_float<double, uint64_t, strtod, 14>(f); }
string tostring(float f) { return tostring_float<float, uint32_t, strtof, 5>(f); }

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
	testcall(testundec<int>("0123", 123)); // no octal allowed
	testcall(testundec<int>("00123", 123));
	testcall(testundec<int>("000123", 123));
	testcall(testundec<int>("09", 9));
	testcall(testundec<int>("0", 0));
	testcall(testundec<int>("0000", 0));
	testcall(testundec<double>("123", 123));
	testcall(testundec<double>("0123", 123));
	testcall(testundec<double>("00123", 123));
	testcall(testundec<double>("000123", 123));
	testcall(testundec<double>("0", 0));
	testcall(testundec<double>("0000", 0));
	testcall(testundec<double>("0e1", 0)); // this input has triggered the 0x detector, making it fail
	testcall(testundec<double>("0e-1", 0));
	testcall(testundec<double>("0e+1", 0));
	testcall(testundec<double>("11e1", 110));
	testcall(testundec<double>("11e+1", 110));
	testcall(testundec<double>("-1", -1));
	testcall(testundec<double>("inf", 1.0/0.0));
	testcall(testundec<double>("-inf", -1.0/0.0));
	testcall(testundec<float>("2.5", 2.5));
	testcall(testundec<float>("2.5e+1", 25));
	testcall(testundec<float>("33554450", 33554448)); // should round to most trailing zeroes in mantissa
	testcall(testundec<float>("inf", 1.0/0.0));
	testcall(testundec<float>("-inf", -1.0/0.0));
	
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
	assert(fromstring("nan", f)); assert(isnan(f));
	assert(!fromstring("NAN", f));
	assert(!fromstring("INF", f));
	assert(!fromstring("-INF", f));
	assert(fromstring("nan", d)); assert(isnan(d));
	
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
	
	// ensure float->string is the shortest possible string that roundtrips, like Python
	// exception: unlike Python, I skip the period if the fractional part is zero
	// I also always use decimal notation in the range 0.00001 to 10000000000000000, and scientific elsewhere
	// upper threshold is because above that, decimal notation has more than 17 digits (max needed for scientific)
	// lower is because beyond that, accurate decimal form can be longer than scientific, even with the exponent
	// thresholds are same for float and double because why wouldn't they
	assert_eq(tostring(1.0), "1");
	assert_eq(tostring(3.0), "3");
	assert_eq(tostring(10.0), "10");
	assert_eq(tostring(123.0), "123");
	assert_eq(tostring(0.1), "0.1");
	assert_eq(tostring(0.2), "0.2");
	assert_eq(tostring(0.3), "0.3");
	assert_eq(tostring(0.1+0.2), "0.30000000000000004");
	assert_eq(tostring(0.7-0.4), "0.29999999999999993");
	assert_eq(tostring(-0.1), "-0.1");
	assert_eq(tostring(0.9999999999999999), "0.9999999999999999"); // next is 1
	assert_eq(tostring(4.999999999999999), "4.999999999999999"); // next is 5
	assert_eq(tostring(9.999999999999998), "9.999999999999998"); // next is 10
	assert_eq(tostring(999999999999999.9), "999999999999999.9"); // largest non-integer where next is a power of 10
	assert_eq(tostring(2251799813685247.8), "2251799813685247.8"); // prev is .5000, next is integer
	assert_eq(tostring(2251799813685247.2), "2251799813685247.2"); // another few where both rounding directions are equally far
	assert_eq(tostring(2251799813685246.8), "2251799813685246.8"); // should round last digit to even
	assert_eq(tostring(2251799813685246.2), "2251799813685246.2");
	assert_eq(tostring(4503599627370495.5), "4503599627370495.5"); // largest non-integer
	assert_eq(tostring(4503599627370494.5), "4503599627370494.5"); // second largest non-integer, to ensure it rounds properly
	assert_eq(tostring(399999999999999.94), "399999999999999.94"); // next is integer, prev's fraction doesn't start with 9
	assert_eq(tostring(0.6822871999174), "0.6822871999174"); // glitchy in C# ToString R
	assert_eq(tostring(0.6822871999174001), "0.6822871999174001");
	assert_eq(tostring(0.0), "0");
	assert_eq(tostring(-0.0), "-0");
	assert_eq(tostring(1.0/0.0), "inf");
	assert_eq(tostring(-1.0/0.0), "-inf");
	assert_eq(tostring(0.0/0.0), "nan");
	assert_eq(tostring((double)0.1f), "0.10000000149011612");
	assert_eq(tostring(1.7976931348623157081452742373170e+308), "1.7976931348623157e+308"); // max possible double
	assert_eq(tostring(1.7976931348623155085612432838451e+308), "1.7976931348623155e+308"); // second largest
	assert_eq(tostring(5e-324), "5e-324"); // smallest possible
	assert_eq(tostring(1e-323), "1e-323"); // second smallest
	assert_eq(tostring(0.00000999999999999999912396464463), "9.999999999999999e-6"); // the scientific notation cutoff points
	assert_eq(tostring(0.00001000000000000000081803053914), "0.00001");
	assert_eq(tostring(0.00001000000000000000251209643365), "0.000010000000000000003");
	assert_eq(tostring( 9999999999999998.0), "9999999999999998");
	assert_eq(tostring(10000000000000000.0), "1e+16");
	assert_eq(tostring(10000000000000002.0), "1.0000000000000002e+16");
	assert_eq(tostring(7.14169434645052e-92), "7.14169434645052e-92");
	assert_eq(tostring(-0.01), "-0.01");
	assert_eq(tostring(-0.00001), "-0.00001");
	assert_eq(tostring(-0.000001), "-1e-6");
	assert_eq(tostring(-1.0), "-1");
	assert_eq(tostring(-1000.0), "-1000");
	assert_eq(tostring(-10.01), "-10.01");
	assert_eq(tostring(-1000000000000000.0), "-1000000000000000");
	assert_eq(tostring(-10000000000000000.0), "-1e+16");
	assert_eq(tostring(-1.2345678901234567e+123), "-1.2345678901234567e+123"); // longest possible double in scientific
	assert_eq(tostring(-0.000012345678901234567), "-0.000012345678901234568"); // longest possible in decimal with this ruleset
	
	assert_eq(tostring(0.1f), "0.1");
	assert_eq(tostring(0.00001f), "0.00001");
	assert_eq(tostring(0.0000000000000000000000000000000000000000000014012984643248170709f), "1e-45"); // smallest positive float
	assert_eq(tostring(340282346638528859811704183484516925440.0f), "3.4028235e+38"); // max possible float
	assert_eq(tostring(340282326356119256160033759537265639424.0f), "3.4028233e+38"); // second largest
	assert_eq(tostring(7.038531e-26f), "7.038531e-26"); // closest float != float closest to closest double
	assert_eq(tostring(9.99e-43f), "9.99e-43");
	assert_eq(tostring(4.7019785e-38f), "4.7019785e-38");
	assert_eq(tostring(9.40397050112110050170108664354930e-38f), "9.40397e-38"); // adding another digit gives a too long answer
	assert_eq(tostring(0.00024414061044808477163314819336f), "0.00024414061");
	assert_eq(tostring(0.00024414062500000000000000000000f), "0.00024414062"); // should round to 2, not 3
	assert_eq(tostring(0.00024414065410383045673370361328f), "0.00024414065");
	assert_eq(tostring(0.00000002980718250000791158527136f), "2.9807183e-8");
	assert_eq(tostring(3.355445e+7f), "33554448"); // expanding shortest scientific notation to decimal gives wrong answer
	assert_eq(tostring(1.262177373e-29f), "1.2621774e-29"); // middle is tricky to round correctly
	assert_eq(tostring(1.262177448e-29f), "1.2621775e-29"); // printf to 8 digits is ...774 which is wrong, but 8 is still possible
	assert_eq(tostring(1.262177598e-29f), "1.2621776e-29"); // (the other two are just the two closest, for human readers)
}
