#include "stringconv.h"
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <errno.h>
#include "test.h"

template<typename T> bool parse_str_raw(const char * in, const char * end, T& out)
{
	out = 0;
	if (in == end) return false;
	T t_max = (T)-1;
	while (in != end)
	{
		if (*in >= '0' && *in <= '9')
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

template<typename T> bool parse_str(const char * in, const char * end, T& out)
{
	if constexpr (sizeof(T) > sizeof(size_t)) return parse_str_raw<T>(in, end, out);
	
	out = 0;
	size_t n;
	if (!parse_str_raw<size_t>(in, end, n)) return false; // minimize number of parse_str_raw instantiations
	out = n;
	return ((size_t)out == n);
}

template<typename Ts, typename Tu> bool parse_str_sign(const char * in, const char * end, Ts& out)
{
	out = 0;
	if (UNLIKELY(in == end)) return false;
	bool neg = (*in == '-');
	in += neg;
	
	Tu tmp;
	if (UNLIKELY(!parse_str<Tu>(in, end, tmp))) return false;
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


template<typename T> bool parse_str_hex_raw(const char * in, const char * end, T& out)
{
	out = 0;
	if (UNLIKELY(in == end)) return false;
	while (in < end && *in == '0') in++;
	if (UNLIKELY(end-in > (ptrdiff_t)sizeof(T)*2)) return false;
	
	while (in != end)
	{
		out <<= 4;
		if (*in >= '0' && *in <= '9') out += *in-'0';
		else
		{
			char upper = *in & ~0x20;
			if (upper >= 'A' && upper <= 'F') out += upper-'A'+10;
			else return false;
		}
		in++;
	}
	return true;
}

template<typename T> bool parse_str_hex(const char * in, const char * end, T& out)
{
	if constexpr (sizeof(T) > sizeof(size_t)) return parse_str_hex_raw<T>(in, end, out);
	
	size_t n;
	if (!parse_str_hex_raw<size_t>(in, end, n)) return false;
	out = n;
	return ((size_t)out == n);
}


#define FROMFUNC(Tu, Ts) \
	bool fromstring(cstring s, Tu& out) \
	{ \
		return parse_str((char*)s.bytes().ptr(), (char*)s.bytes().ptr()+s.length(), out); \
	} \
	bool fromstring(cstring s, Ts& out) \
	{ \
		return parse_str_sign<Ts, Tu>((char*)s.bytes().ptr(), (char*)s.bytes().ptr()+s.length(), out); \
	} \
	bool fromstringhex(cstring s, Tu& out) \
	{ \
		return parse_str_hex((char*)s.bytes().ptr(), (char*)s.bytes().ptr()+s.length(), out); \
	}

FROMFUNC(unsigned char,      signed char)
FROMFUNC(unsigned short,     signed short)
FROMFUNC(unsigned int,       signed int)
FROMFUNC(unsigned long,      signed long)
FROMFUNC(unsigned long long, signed long long)


// if the input does not start with 0x, return it unchanged
// otherwise, return a substring of input that strtod rejects, for example pointer to the x (pointer to the nul would work too)
static const char * drop0x(const char * in)
{
	if (in[0]=='0' && (in[1]=='x' || in[1]=='X')) return in+1;
	else return in;
}

template<typename T, T(*strtod)(const char*,char**)>
bool fromstring_float(cstring s, T& out)
{
	out = 0;
	auto tmp_s = s.c_str();
	const char * tmp_cp = tmp_s;
	if (UNLIKELY(*tmp_cp != '-' && !isdigit(*tmp_cp))) goto maybe_inf;
	char * tmp_cpo;
	out = strtod(drop0x(tmp_cp), &tmp_cpo);
	if (UNLIKELY(tmp_cpo != tmp_cp + s.length())) goto maybe_inf;
	if (UNLIKELY(!isdigit(tmp_cpo[-1]))) goto maybe_inf;
	if (UNLIKELY(out == HUGE_VAL || out == -HUGE_VAL)) goto maybe_inf;
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

static void flatten_zeroes(char* str);
// Arlib's float->string functions yield the shortest possible string that roundtrips, like Python,
//  except I omit the fraction if zero, and I don't zero pad the exponent.
// Like Python, I use decimal for 0.0001 to 9999999999999998 (inclusive), and scientific for anything else.
// Upper threshold is because above that, rounding gets wonky. https://github.com/python/cpython/blob/v3.8.0/Python/pystrtod.c#L1116
// Lower threshold is to match Python. I don't know why they chose that one, personally I would've kept it decimal for another digit.
// Thresholds are same for float and double because why wouldn't they. However, tostring((float)0.1) is 0.1, not 0.10000000149011612.
template<typename T, typename Ti, T(*strtod)(const char*,char**), int minprec>
string tostring_float(T f)
{
	static_assert(std::numeric_limits<T>::is_iec559);
	static_assert(sizeof(T) == sizeof(Ti));
	
	if (isnan(f)) return "nan";
	if (isinf(f)) return &"-inf"[!signbit(f)];
	
	int prec = minprec;
	
	char fmt[] = "%#.*e";
	if ((fabs(f) >= (T)0.0001 && fabs(f) < (T)10000000000000000.0) || f==0.0)
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
			// For a few numbers, like 5.960464477539063e-8 and 1.262177448e-29f, rounding to N digits parses as the previous float,
			// but incrementing the last digit parses as the desired number.
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
			// There's no need to worry about the other direction, where the best answer would be decreasing last[-1].
			// Proof: Assume there is such a number. Call it N. Let N-1 and N+1 be the two closest floats.
			// Also assume, without loss of generality, that 1.23 parses as N, but rounding N to three digits yields 1.24.
			// -> N's full decimal expansion is 1.235 or greater (otherwise it wouldn't round to 1.24)
			// -> N-1 is 1.225 or less (otherwise 1.23 wouldn't parse as N)
			// -> N+1's full decimal expansion is 1.245 or less (otherwise 1.24 would parse as N)
			// -> N - N-1 >= 0.010, N - N+1 <= 0.010
			// -> the epsilon grows as the number shrinks
			// -> impossible. Floats don't work that way.
			// Per the above, the opposite can happen, but it too requires an epsilon change, i.e. N is an exact power of two.
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
// Removes unnecessary zeroes (trailing in fraction, leading in exponent) from a number from printf %#f or %#e.
// Any other input is undefined behavior.
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
#ifdef _WIN32 // msvcrt printf uses three-digit exponents, glibc uses two
		while (*e == '0') e++;
#else
		if (*e == '0') e++;
#endif
		while (*e) *(end++) = *(e++);
	}
	*end = '\0';
}
string tostring(double f) { return tostring_float<double, uint64_t, strtod, 14>(f); }
string tostring(float f) { return tostring_float<float, uint32_t, strtof, 5>(f); }

string tostringhex(arrayview<uint8_t> val)
{
	string ret;
	arrayvieww<uint8_t> retb = ret.construct(val.size()*2);
	for (size_t i=0;i<val.size();i++)
	{
		sprintf((char*)retb.slice(i*2, 2).ptr(), "%.2X", val[i]);
	}
	return ret;
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
	test_nothrow { assert_eq(a, V); }
}
test("string conversion", "", "string")
{
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
	testcall(testundec<double>("11e-1", 1.1));
	testcall(testundec<double>("-1", -1));
	testcall(testundec<double>("inf", HUGE_VAL));
	testcall(testundec<double>("-inf", -HUGE_VAL));
	testcall(testundec<float>("2.5", 2.5));
	testcall(testundec<float>("2.5e+1", 25));
#ifndef _WIN32 // some of these fail on Windows (Wine is more accurate than Windows, but not perfect)
	testcall(testundec<float>("33554446", 33554448.0)); // should round to even mantissa
#endif
	testcall(testundec<float>("33554450", 33554448.0));
	testcall(testundec<float>("33554451", 33554452.0));
	testcall(testundec<float>("inf", HUGE_VALF));
	testcall(testundec<float>("-inf", -HUGE_VALF));
	
	// just to verify that all 10 exist, C's integer types are absurd
	testcall(testundec<signed char     >("123", 123));
	testcall(testundec<signed short    >("12345", 12345));
	testcall(testundec<signed int      >("1234567890", 1234567890));
	testcall(testundec<signed long     >("1234567890", 1234567890));
	testcall(testundec<signed long long>("1234567890123456789", 1234567890123456789ull));
	testcall(testundec<unsigned char     >("123", 123));
	testcall(testundec<unsigned short    >("12345", 12345));
	testcall(testundec<unsigned int      >("1234567890", 1234567890));
	testcall(testundec<unsigned long     >("1234567890", 1234567890));
	testcall(testundec<unsigned long long>("12345678901234567890", 12345678901234567890ull));
	testcall(testunhex<unsigned char     >("aa", 0xaa));
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
	
	uint8_t u8;
	int32_t i32;
	int64_t i64;
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
	
	assert(!fromstring("0x123", f));
	assert(!fromstring("1e", f));
	assert(!fromstring("1e+", f));
	assert(!fromstring("1e-", f));
	assert(!fromstring("1.", f));
	assert(!fromstring(".1", f));
	assert( fromstring("inf", f)); assert(isinf(f));
	assert( fromstring("-inf", f)); assert(isinf(f));
	assert( fromstring("nan", f)); assert(isnan(f));
	assert(!fromstring("NAN", f));
	assert(!fromstring("INF", f));
	assert(!fromstring("-INF", f));
	assert( fromstring("nan", d)); assert(isnan(d));
	
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
	assert( fromstring("340282346638528859811704183484516925440", f)); // max possible float; a few higher values round down to that,
	assert(!fromstring("340282366920938463463374607431768211456", f)); // but anything too big should be rejected
	assert( fromstring("1797693134862315708145274237317043567980705675258449965989174768031572607800285387605895586327668781715"
	                   "4045895351438246423432132688946418276846754670353751698604991057655128207624549009038932894407586850845"
	                   "5133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368", d));
	assert(!fromstring("1797693134862315907729305190789024733617976978942306572734300811577326758055009631327084773224075360211"
	                   "2011387987139335765878976881441662249284743063947412437776789342486548527630221960124609411945308295208"
	                   "5005768838150682342462881473913110540827237163350510684586298239947245938479716304835356329624224137216", d));
	// smallest possible subnormal float
	testcall(testundec<double>("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                             "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                             "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                             "0000000000000000000000049406564584124654417656879286822137236505980261432476442558568250067550727020"
	                             "8751865299836361635992379796564695445717730926656710355939796398774796010781878126300713190311404527"
	                             "8458171678489821036887186360569987307230500063874091535649843873124733972731696151400317153853980741"
	                             "2623856559117102665855668676818703956031062493194527159149245532930545654440112748012970999954193198"
	                             "9409080416563324524757147869014726780159355238611550134803526493472019379026810710749170333222684475"
	                             "3335720832431936092382893458368060106011506169809753078342277318329247904982524730776375927247874656"
	                             "0847782037344696995336470179726777175851256605511991315048911014510378627381672509558373897335989936"
	                             "64809941164205702637090279242767544565229087538682506419718265533447265625", 5e-324));
	// half of the above, should round to even i.e. to exactly 0.0
	testcall(testundec<double>("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                             "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                             "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                             "0000000000000000000000024703282292062327208828439643411068618252990130716238221279284125033775363510"
	                             "4375932649918180817996189898282347722858865463328355177969898199387398005390939063150356595155702263"
	                             "9229085839244910518443593180284993653615250031937045767824921936562366986365848075700158576926990370"
	                             "6311928279558551332927834338409351978015531246597263579574622766465272827220056374006485499977096599"
	                             "4704540208281662262378573934507363390079677619305775067401763246736009689513405355374585166611342237"
	                             "6667860416215968046191446729184030053005753084904876539171138659164623952491262365388187963623937328"
	                             "0423891018672348497668235089863388587925628302755995657524455507255189313690836254779186948667994968"
	                             "324049705821028513185451396213837722826145437693412532098591327667236328125", 0.0));
#ifndef _WIN32
	// slightly more than the above (msvcrt rounds poorly)
	testcall(testundec<double>("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                             "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                             "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                             "0000000000000000000000024703282292062327208828439643411068618252990130716238221279284125033775363510"
	                             "4375932649918180817996189898282347722858865463328355177969898199387398005390939063150356595155702263"
	                             "9229085839244910518443593180284993653615250031937045767824921936562366986365848075700158576926990370"
	                             "6311928279558551332927834338409351978015531246597263579574622766465272827220056374006485499977096599"
	                             "4704540208281662262378573934507363390079677619305775067401763246736009689513405355374585166611342237"
	                             "6667860416215968046191446729184030053005753084904876539171138659164623952491262365388187963623937328"
	                             "0423891018672348497668235089863388587925628302755995657524455507255189313690836254779186948667994968"
	                             "32404970582102851318545139621383772282614543769341253209859132766723632812500000000000001", 5e-324));
	
	// https://www.exploringbinary.com/incorrectly-rounded-conversions-in-visual-c-plus-plus/
	// many other troublesome numbers can be found at exploringbinary.com
	// (they still fail on Windows)
	testcall(testundec<double>("9214843084008499.0",
	                            9214843084008500.0));
	assert_eq(9214843084008499.0, 9214843084008500.0); // test the compiler too
	testcall(testundec<double>("0.500000000000000166533453693773481063544750213623046875",
	                            0.500000000000000222045));
	assert_eq(0.500000000000000166533453693773481063544750213623046875, 0.500000000000000222045);
	testcall(testundec<double>("30078505129381147446200",
	                            30078505129381149540352.0));
	assert_eq(30078505129381147446200.0, 30078505129381149540352.0);
	testcall(testundec<double>("1777820000000000000001",
	                            1777820000000000131072.0));
	assert_eq(1777820000000000000001.0, 1777820000000000131072.0);
	testcall(testundec<double>("0.500000000000000166547006220929549868969843373633921146392822265625",
	                            0.50000000000000022204));
	assert_eq(0.500000000000000166547006220929549868969843373633921146392822265625, 0.50000000000000022204);
	testcall(testundec<double>("0.50000000000000016656055874808561867439493653364479541778564453125",
	                            0.50000000000000022204));
	assert_eq(0.50000000000000016656055874808561867439493653364479541778564453125, 0.50000000000000022204);
	testcall(testundec<double>("0.3932922657273",
	                            0.39329226572730002776));
	assert_eq(0.3932922657273, 0.39329226572730002776);
#endif
	
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
#ifndef _WIN32
	assert_eq(tostring(2251799813685247.8), "2251799813685247.8"); // prev is .5000, next is integer
#endif
	assert_eq(tostring(2251799813685247.2), "2251799813685247.2"); // another few where both rounding directions are equally far
#ifndef _WIN32
	assert_eq(tostring(2251799813685246.8), "2251799813685246.8"); // should round last digit to even
#endif
	assert_eq(tostring(2251799813685246.2), "2251799813685246.2");
	assert_eq(tostring(4503599627370495.5), "4503599627370495.5"); // largest non-integer
	assert_eq(tostring(4503599627370494.5), "4503599627370494.5"); // second largest non-integer, to ensure it rounds properly
	assert_eq(tostring(399999999999999.94), "399999999999999.94"); // next is integer, prev's fraction doesn't start with 9
	assert_eq(tostring(0.6822871999174), "0.6822871999174"); // glitchy in C# ToString R
	assert_eq(tostring(0.6822871999174001), "0.6822871999174001");
	assert_eq(tostring(0.84551240822557006), "0.8455124082255701");
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
	assert_eq(tostring(0.00009999999999999999123964644632), "9.999999999999999e-5"); // the scientific notation cutoff points
	assert_eq(tostring(0.00010000000000000000479217360239), "0.0001");
	assert_eq(tostring(0.00010000000000000003189722791452), "0.00010000000000000003");
	assert_eq(tostring( 9999999999999998.0), "9999999999999998");
	assert_eq(tostring(10000000000000000.0), "1e+16");
	assert_eq(tostring(10000000000000002.0), "1.0000000000000002e+16");
	assert_eq(tostring(7.14169434645052e-92), "7.14169434645052e-92");
	assert_eq(tostring(-0.01), "-0.01");
	assert_eq(tostring(-0.000100000000000000004792173602385929598312941379845142364501953125), "-0.0001");
	assert_eq(tostring(-0.00001), "-1e-5");
	assert_eq(tostring(-1.0), "-1");
	assert_eq(tostring(-1000.0), "-1000");
	assert_eq(tostring(-10.01), "-10.01");
	assert_eq(tostring(-1000000000000000.0), "-1000000000000000");
	assert_eq(tostring(-10000000000000000.0), "-1e+16");
	assert_eq(tostring(-1.2345678901234567e+123), "-1.2345678901234567e+123"); // longest possible double in scientific
	assert_eq(tostring(-1.2345678901234567e-123), "-1.2345678901234567e-123"); // equally long
	assert_eq(tostring(-0.00012345678901234567), "-0.00012345678901234567"); // longest possible in decimal with this ruleset
	assert_eq(tostring(5.9604644775390618382e-8), "5.960464477539062e-8"); // middle is tricky to round correctly
	assert_eq(tostring(5.9604644775390625000e-8), "5.960464477539063e-8"); // printf to 16 digits is ...2 which is wrong,
	assert_eq(tostring(5.9604644775390638234e-8), "5.960464477539064e-8"); // but 16 is still possible
	assert_eq(tostring(5.3169119831396629013e+36), "5.316911983139663e+36"); // another increment-last
	assert_eq(tostring(5.3169119831396634916e+36), "5.316911983139664e+36"); // (four of these six are just the tricky ones' neighbors,
	assert_eq(tostring(5.3169119831396646722e+36), "5.316911983139665e+36"); //  for human readers)
	
	assert_eq(tostring(0.1f), "0.1");
	assert_eq(tostring(0.0000999999974737875163555145263671875f), "0.0001"); // below threshold, but at threshold if rounded
#ifndef _WIN32
	assert_eq(tostring(0.0000000000000000000000000000000000000000000014012984643248170709f), "1e-45"); // smallest positive float
#endif
	assert_eq(tostring(340282346638528859811704183484516925440.0f), "3.4028235e+38"); // max possible float
	assert_eq(tostring(340282326356119256160033759537265639424.0f), "3.4028233e+38"); // second largest
#ifndef _WIN32 // this one is nasty - Windows turns it into 1e-42, which is a different number.
	assert_eq(tostring(9.99e-43f), "9.99e-43"); // I think it only happens on subnormals...
#endif
	assert_eq(tostring(4.7019785e-38f), "4.7019785e-38");
	assert_eq(tostring(9.40397050112110050170108664354930e-38f), "9.40397e-38"); // printing to 6 decimals gives nonzero last
	assert_eq(tostring(0.00024414061044808477163314819336f), "0.00024414061");
#ifndef _WIN32
	assert_eq(tostring(0.00024414062500000000000000000000f), "0.00024414062"); // last digit should round to 2, not 3
#endif
	assert_eq(tostring(0.00024414065410383045673370361328f), "0.00024414065");
	assert_eq(tostring(0.00000002980718250000791158527136f), "2.9807183e-8");
	assert_eq(tostring(3.355445e+7f), "33554448"); // expanding shortest scientific notation to decimal gives wrong answer
	assert_eq(tostring(1.262177373e-29f), "1.2621774e-29");
	assert_eq(tostring(1.262177448e-29f), "1.2621775e-29"); // one of three increment-last floats in this ruleset
	assert_eq(tostring(1.262177598e-29f), "1.2621776e-29"); // (the other two are 1.5474251e+26f and 1.2379401e+27f)
	assert_eq(tostring(4.30373586499999995214071901727947988547384738922119140625e-15f), "4.303736e-15"); // easy to round wrong
}
