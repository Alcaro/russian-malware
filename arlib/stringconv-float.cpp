#include "stringconv.h"
#include <math.h>

#if defined(__MINGW32__)
float strtof_arlib(const char * str, char** str_end)
{
	int n = 0;
	float ret;
	sscanf(str, "%f%n", &ret, &n);
	if (str_end) *str_end = (char*)str+n;
	return ret;
}
double strtod_arlib(const char * str, char** str_end)
{
	int n = 0;
	double ret;
	sscanf(str, "%lf%n", &ret, &n);
	if (str_end) *str_end = (char*)str+n;
	return ret;
}
// long double is creepy, no strtold
#endif


// strtod allows all kinds of stuff I'd rather reject; leading space, .5, 5., 0x123, nan(123), INF, +inf, infinity, 1e+999, etc
// I want only valid JSON numbers (minus the leading zeroes restriction), and "inf" "-inf" "nan"
static bool fromstring_float(cstring s, double& out, double(*strtod)(const char*,char**))
{
	out = 0;
	auto tmp_s = s.c_str();
	const char * tmp_cp = tmp_s;
	const char * tmp_cp_digit = tmp_cp;
	if (*tmp_cp_digit == '-') tmp_cp_digit++;
	if (UNLIKELY(!isdigit(*tmp_cp_digit)))
	{
		if (!strcmp(tmp_cp, "inf")) { out = HUGE_VAL; return true; }
		if (!strcmp(tmp_cp, "-inf")) { out = -HUGE_VAL; return true; }
		if (!strcmp(tmp_cp, "nan")) { out = NAN; return true; }
		return false;
	}
	if (UNLIKELY(tmp_cp_digit[0] == '0' && (tmp_cp_digit[1]|0x20) == 'x')) return false;
	char * tmp_cpo;
	out = strtod(tmp_cp, &tmp_cpo);
	if (UNLIKELY(tmp_cpo != tmp_cp+s.length() ||
	             !isdigit(tmp_cpo[-1]) ||
	             out == HUGE_VAL || out == -HUGE_VAL)) return false;
	return true;
}
bool fromstring(cstring s, double& out) { return fromstring_float(s, out, strtod); }
bool fromstring(cstring s, float& out)
{
	double tmp;
	// can't just pass strtod, it yields double rounding and wrong answer on 7.038531e-26
	bool ret = fromstring_float(s, tmp, [](const char * str, char * * str_end)->double { return strtof(str, str_end); });
	out = tmp;
	return ret;
}
#undef strtod


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
	if (LIKELY((fabs(f) >= (T)0.0001 && fabs(f) < (T)10000000000000000.0) || f==0.0))
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

// tests are in stringconv.cpp
