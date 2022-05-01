#if (__GNUC__ >= 11 || __clang_major__ >= 14) && !defined(_WIN32)
#define HAVE_TO_CHARS

// from_chars
// - accepts 0. and .0 (but not . or most other weird inputs)
// - accepts infinity and INF
// - returns ERANGE for 1e+999 (at least on gcc)
// - returns ERANGE for various dubiously long subnormals (at least on gcc)
// msvc is better on subnormals, but 1e-999 is still ERANGE and not 0
// some of the above are easy to work around, but the ERANGEs are not
// as such, I cannot use it
//#define HAVE_FROM_CHARS

#include <charconv> // this includes <cctype> in gcc <= 11.3, which does not like Arlib's overridden ctype functions
#if __GNUC__ >= 12
#warning rearrange the includes
#endif
#endif

#include "stringconv.h"
#include <math.h>

#ifdef HAVE_FROM_CHARS
#include <system_error>

template<typename T>
bool fromstring_float(cstring s, T& out)
{
	out = 0;
	const char * start = (char*)s.bytes().ptr();
	const char * end = start + s.length();
	auto ret = std::from_chars(start, end, out);
	if (ret.ptr != end) out = 0;
	return (ret.ptr == end && ret.ec == std::errc());
}
bool fromstring(cstring s, float& out) { return fromstring_float<float>(s, out); }
bool fromstring(cstring s, double& out) { return fromstring_float<double>(s, out); }

#else

struct floatinf {
	uint64_t max_sig; // Lowest unrepresentable integer minus 1, aka 1<<(bits_mantissa+1).
	uint8_t max_int_exp; // Highest exactly representable power of 10.
	uint8_t bits_mantissa; // Not counting the implicit 1.
	int32_t max_bin_exp; // At what exponent the number is infinite, plus some arbitrary offset that's basically trial and error.
};
static const floatinf fi_double = { 1ull<<53, 22, 52, 963 };
static const floatinf fi_float = { 1<<24, 10, 23, 67 };

static bool fromstring_float(cstring s, double& out, const floatinf * fi)
{
	out = 0;
	
	const char * start = (const char*)s.bytes().ptr();
	const char * end = start + s.length();
	const char * digits_end = end;
	if (end-start > 10000) return false; // 0.(9999 zeroes)1e+10000 is technically equal to 1, but it's also stupid.
	// TODO: should allow it anyways
	
	bool neg = false;
	if (start < end && *start == '-')
	{
		neg = true;
		start++;
	}
	
	if (UNLIKELY(!isdigit(*start) || start==end))
	{
		if (s == "inf") { out = HUGE_VAL; return true; }
		if (s == "-inf") { out = -HUGE_VAL; return true; }
		if (s == "nan") { out = NAN; return true; }
		return false;
	}
	
	const char * iter = start;
	
	int exponent = INT_MAX;
	uint64_t significand = 0;
	uint64_t significand_sum = 0;
	int n_digits = 0;
	
	while (true)
	{
		if ((uint8_t)(*iter-'0') <= 9)
		{
			significand *= 10;
			significand += *iter-'0';
			significand_sum |= significand; // make it sticky, so input 2^64 doesn't overflow significand back to zero
			exponent--;
			n_digits++;
		}
		else if (*iter == '.')
		{
			if (exponent <= 0) return false;
			if (iter == end || !isdigit(iter[1])) return false;
			exponent = 0;
		}
		else if ((0x20|*iter) == 'e')
		{
			digits_end = iter;
			if (exponent > 0) exponent = 0;
			iter++;
			
			bool exp_neg = false;
			if (*iter == '-') { exp_neg = true; iter++; }
			else if (*iter == '+') iter++;
			
			uint32_t newexp = 0;
			uint32_t newexp_sum = 0;
			if (iter == end) return false;
			while (iter < end)
			{
				if (UNLIKELY((uint8_t)(*iter-'0') > 9)) return false;
				newexp = newexp*10 + *iter-'0';
				newexp_sum |= newexp; // another sticky
				iter++;
			}
			if (newexp_sum >= 16384)
			{
				// if the exponent is crazy, ignore the digits; it's either zero or infinite
				if (!exp_neg && significand_sum != 0) out = HUGE_VAL;
				if (neg) out = -out;
				if (out == 0) return true;
				return false;
			}
			if (exp_neg) exponent -= newexp;
			else exponent += newexp;
			break;
		}
		else return false;
		iter++;
		if (iter == end)
		{
			if (exponent > 0) exponent = 0;
			break;
		}
	}
	
	// for small numbers, the correct answer can be calculated with a multiplication or division of two float-representable integers
	// there are some numbers where the mul or div rounds to exactly halfway between two floats, but to my knowledge,
	//  they all have significand or exponent out of range for floats and need the slow path
	// same may happen on i386 if fpcw is set to float80 rather than float64; I did not investigate this either
	if (LIKELY(abs(exponent) <= fi->max_int_exp && significand <= fi->max_sig && n_digits < ilog10(UINT64_MAX)))
	{
		bool neg_exp = (exponent < 0);
		exponent = abs(exponent);
		
		double exp_adjust = 1;
		if (exponent & 1) exp_adjust *= 10.0;
		if (exponent & 2) exp_adjust *= 100.0;
		if (exponent & 4) exp_adjust *= 10000.0;
		if (exponent & 8) exp_adjust *= 100000000.0;
		if (exponent & 16) exp_adjust *= 10000000000000000.0;
		
		double ret;
		if (!neg_exp)
			ret = (double)significand * exp_adjust;
		else
			ret = (double)significand / exp_adjust;
		if (neg) out = -ret;
		else out = ret;
		return true;
	}
	
	// for large numbers, use a slower algorithm
	// some float parser implementations have three different algorithms with different speed and limitations, but two's enough for me
	
	if (significand_sum == 0) // 0e+999 will go here, as will 0.000000000000000000000000 (it exceeds n_digits)
	{
		if (neg) out = -out;
		return true;
	}
	// inputs:
	// - start .. digits_end - digits of the input. The dot, if any, should be ignored.
	//    Contains at least one nonzero digit. Contains nothing but digits and the dot.
	// - exponent - Number of steps to shift the given digits.
	// - n_digits - Number of digits, though some may be leading or trailing zeroes. Not useful anymore.
	// - neg - Whether the input is negative.
	// - fi - Information about the float format.
	// Other variables should be ignored or discarded.
	
	// the digits need to be multiplied or divided such that the exponent is zero, then take the leftmost bits
	// for subnormals, the number of digits to round to is variable
	
	iter = start;
	while (*iter <= '0') iter++; // the only possible non-digit, ., is 0x2E <= '0' (0x30), and there's known to be a nonzero digit
	
	uint32_t limbs[100] = {}; // the traditional implementation is u8[800], but that's just wasteful. better pack eight digits in a u32
	// 9 would fit, but 8 makes the math easier
	// I could count how many limbs I'm using, but that'd be a lot more complexity for no reasonable benefit
	
	int limb_at = 0;
	uint32_t tmp = 0;
	while (limb_at < 800 && iter < digits_end)
	{
		if (*iter == '.') { iter++; continue; }
		tmp = tmp*10 + *iter-'0';
		if (limb_at%8 == 7)
		{
			limbs[limb_at/8] = tmp;
			tmp = 0;
		}
		limb_at++;
		iter++;
	}
	exponent += limb_at;
	if (tmp)
	{
		if ((8-limb_at)&4) tmp *= 10000;
		if ((8-limb_at)&2) tmp *= 100;
		if ((8-limb_at)&1) tmp *= 10;
		limbs[limb_at/8] = tmp;
	}
	
	while (iter < digits_end)
	{
		if (*iter > '0') limbs[99] |= 1; // some algorithms maintain an 'is truncated' bit, but it's easier to round up
		iter++;
		if (*iter != '.') exponent++;
	}
	
	// at the end of this process, I need
	// - limbs[0] concat limbs[1] concat limbs[2]/1000000 to be the output bits of the double
	// - limbs[0] >= 20000000
	// - every limb <= 99999999
	// - bin_exponent to tell how many bits it was shifted
	// min     4503599627370496
	// max 18446744073709551616
	// if output is 10.0, the limbs must be 10 << n such that 10<<n >= 200000000000000000, <= 999999999999999999
	// there two allowed limb values are
	// n=56 72057594 03792793 60+000000
	// n=55 36028797 01896396 80+000000
	// same limbs for 5, 20, 40, 80, 160, etc, just different bin_exponent
	
	int bin_exponent = 0;
	
	exponent -= 18; // need 18 decimal digits of mantissa
	if (exponent & 7)
	{
		// first limb is known >= 10000000, so multiplying by any power of ten will make it overflow and require shifting the limbs
		
		uint64_t mul = 1;
		if (exponent&4) mul *= 10000;
		if (exponent&2) mul *= 100;
		if (exponent&1) mul *= 10;
		exponent &= ~7;
		exponent += 8;
		
		uint64_t carry = 0;
		for (int i=0;i<100;i++)
		{
			uint64_t cur = carry + limbs[i]*mul;
			limbs[i] = cur/100000000;
			carry = cur%100000000*100000000;
		}
		if (carry) limbs[99] |= 1; // sticky overflow
	}
	
	// the top limb must be nonzero at all times
	while (true)
	{
		if (exponent > 0)
		{
			// divide by the smallest power of 2 that zeroes the top limb, and multiply by 1e8 (shift the limbs one step)
			int shift_bits = ilog2(limbs[0])+1;
			bin_exponent += shift_bits;
			exponent -= 8;
			
			uint32_t carry_mask = (1<<shift_bits)-1;
			
			// 64bit multiplication and division everywhere... sucks for 32bit cpus
			// I could change it to uint16[200], but not much point. 32bit is on its way out
			uint64_t carry = limbs[0]; // carry is NOT divided by shift_bits
			for (int i=1;i<100;i++)
			{
				uint64_t cur = (uint64_t)limbs[i] + carry*100000000;
				limbs[i-1] = cur >> shift_bits;
				carry = cur & carry_mask;
			}
			limbs[99] = carry; // this is too much, but it won't affect the result
		}
		else if (exponent < 0 && limbs[0] >= 2)
		{
			// multiply by 1<<26, and divide by 1e8
			int shift_bits = 26;
			bin_exponent -= shift_bits;
			exponent += 8;
			
			uint64_t cur = ((uint64_t)limbs[99] << shift_bits);
			
			uint64_t carry = cur/100000000 | (cur%100000000 ? 1 : 0);
			for (int i=98;i>=0;i--)
			{
				uint64_t cur = ((uint64_t)limbs[i] << shift_bits) + carry;
				limbs[i+1] = cur%100000000;
				carry = cur/100000000;
			}
			limbs[0] = carry;
		}
		else if (limbs[0] < 20000000)
		{
			// multiply by a low power of 2 so the top limb goes a bit higher
			int shift_bits = 25-ilog2(limbs[0]);
			bin_exponent -= shift_bits;
			
			uint64_t carry = 0;
			for (int i=99;i>=0;i--)
			{
				uint64_t cur = ((uint64_t)limbs[i] << shift_bits) + carry;
				limbs[i] = cur%100000000;
				carry = cur/100000000;
			}
		}
		else break;
	}
	
	bool ret = true;
	
	uint64_t mantissa = (uint64_t)limbs[0]*10000000000 + (uint64_t)limbs[1]*100 + limbs[2]/1000000;
	if (mantissa == 0)
		goto done;
	
	{
		int error = 60-ilog2(mantissa);
		mantissa <<= error;
		bin_exponent -= error;
	}
	
	if (bin_exponent+1023+60-1 <= -60) // too small, round to zero
		goto done;
	
	{
		uint64_t prev_mantissa = mantissa;
		int shift = 0;
		if (bin_exponent+1023+60-1 < 0)
		{
			shift = -(bin_exponent+1023+60-1);
		}
		if (fi->bits_mantissa != 52)
		{
			if (bin_exponent+209 <= 23)
				shift = 52-(bin_exponent+209);
			else
				shift = 52-23;
		}
		
		mantissa >>= shift;
		bin_exponent += shift;
		
		if (mantissa << shift != prev_mantissa)
			mantissa |= 1;
	}
	
	// if it's at least halfway to next number,
	if (mantissa & 0x80)
	{
		// and if bottom bit of output mantissa is set, any bit below half-mantissa is set,
		// or if any bit in the limbs outside the mantissa is set,
		// then round up (bits below that will be discarded)
		if ((mantissa & 0x017F) || limbs[2]%1000000)
		{
			mantissa += 0x80;
		}
		else
		{
			for (int i=3;i<100;i++)
			{
				if (limbs[i])
				{
					mantissa += 0x80;
					break;
				}
			}
		}
	}
	
	{
		int error = 61-ilog2(mantissa); // it's probably same power of two as last time, but not guaranteed; must renormalize
		mantissa <<= error;
		bin_exponent -= error;
	}
	
	if (bin_exponent+1023+61 <= 0) // subnormal (or rounded to zero)
	{
		mantissa >>= -(bin_exponent+1023+61);
		out = reinterpret<double>(mantissa>>10);
	}
	else if (bin_exponent >= fi->max_bin_exp) // infinite
	{
		out = HUGE_VAL;
		ret = false;
	}
	else // normal (or subnormal float - the double/float cast will handle that)
	{
		out = reinterpret<double>(((uint64_t)(bin_exponent+1023+61)<<52) | (mantissa<<3>>12));
	}
	
done:
	if (neg) out = -out;
	return ret;
}
bool fromstring(cstring s, double& out)
{
	return fromstring_float(s, out, &fi_double);
}
bool fromstring(cstring s, float& out)
{
	double tmp;
	bool ret = fromstring_float(s, tmp, &fi_float);
	out = tmp;
	return ret;
}
#endif


// Arlib's float->string functions yield the shortest possible string that roundtrips, like Python,
//  except I omit the fraction for integers, and I don't zero pad the exponent.
// Like Python, I use decimal for 0.0001 to 9999999999999998 (inclusive), and scientific for anything else.
// Their rationale <https://github.com/python/cpython/blob/v3.8.0/Python/pystrtod.c#L1116> doesn't apply to this implementation
//  (if it's integer, I format it as integer; if it's non-integer, it needs significant digits beyond the comma),
//  and they don't seem to have a rationale at all for lower, but I can't find a compelling reason to deviate from their choice.
// The functions do, of course, respect the input type; tostring(0.1f) is 0.1, not 0.10000000149011612.

#ifdef HAVE_TO_CHARS

template<typename T>
string tostring_float(T f)
{
	// to_chars is quite close to my desired output format
	static_assert(std::numeric_limits<T>::is_iec559);
	
	// can be replaced with if (isnan(f)) f = fabs(f); under libstdc++,
	// but it's implementation defined if the weird ones are nan or nan(123), or inf or infinity, so let's just hardcode them
	if (UNLIKELY(isnan(f))) return "nan";
	if (UNLIKELY(isinf(f))) return signbit(f) ? "-inf" : "inf";
	
	bool fixed = ((fabs(f) >= (T)0.0001 && fabs(f) < (T)10000000000000000.0) || f == 0);
	
	char buf[32]; // longest possible is 24, give it a little extra
	char* end;
	if (fixed)
	{
		auto ret = std::to_chars(buf, buf+sizeof(buf), f, std::chars_format::fixed);
		end = ret.ptr;
	}
	else
	{
		auto ret = std::to_chars(buf, buf+sizeof(buf), f, std::chars_format::scientific);
		end = ret.ptr;
		if (end[-2] == '0' && end[-4] == 'e') // can't underflow, shortest possible output in scientific is 1e+01
		{
			end[-2] = end[-1];
			end--;
		}
	}
	return bytesr((uint8_t*)buf, end-buf);
}

#else

#include "deps/dragonbox.h"
static string tostring_float_part2(bool negative, bool fixed, int exponent, uint64_t significand);

template<typename T>
string tostring_float(T f)
{
	static_assert(std::numeric_limits<T>::is_iec559);
	
	if (UNLIKELY(isnan(f))) return "nan";
	if (UNLIKELY(isinf(f))) return signbit(f) ? "-inf" : "inf";
	
	// special case integers
	// contains a few weird pieces to help the compiler optimize some common subexpressions
	if (fabs(f) < (T)10000000000000000 && (T)(int64_t)fabs(f) == fabs(f))
	{
		// the function gets confused by fixed=true exponent>0, but it ends up doing what's needed
		return tostring_float_part2(signbit(f), true, 1, (int64_t)fabs(f));
	}
	
	bool fixed = (fabs(f) >= (T)0.0001 && fabs(f) < (T)10000000000000000);
	namespace dbp = jkj::dragonbox::policy;
	auto r = jkj::dragonbox::to_decimal<T>(f, dbp::cache::compact, dbp::trailing_zero::ignore, dbp::sign::ignore);
	return tostring_float_part2(signbit(f), fixed, r.exponent, r.significand);
}

// separate function so the template doesn't duplicate it
static string tostring_float_part2(bool negative, bool fixed, int exponent, uint64_t significand)
{
	if (!fixed || exponent < 0)
	{
		// this is an infinite loop if significand == 0, but zero is an integer, so exponent=1 and this isn't entered
		while (significand%10 == 0)
		{
			significand /= 10;
			exponent++;
		}
	}
	
	char buf[23+5]; // float uses max 9 chars left and 4 right (-1.2345678e+23), or 17 left and 0 right (-1234567948140544)
	char* out = buf+23; // double uses max 19+5 (-1.2345678901234567e+123) or 23+0 (-0.00012345678901234567)
	char* outend = buf+23;
	
	if (fixed)
	{
		do {
			if (exponent++ == 0) *--out = '.';
			*--out = '0'+significand%10;
			significand /= 10;
		} while (significand);
		if (exponent <= 0)
		{
			while (++exponent <= 0)
				*--out = '0';
			*--out = '.';
			*--out = '0';
		}
	}
	else
	{
		if (significand >= 10)
		{
			while (significand >= 10)
			{
				*--out = '0'+significand%10;
				significand /= 10;
				exponent++;
			}
			*--out = '.';
		}
		*--out = '0'+significand%10;
		*(outend++) = 'e';
		*(outend++) = (exponent < 0 ? '-' : '+');
		exponent = abs(exponent);
		if (exponent < 10) outend += 1; // only possible for negative exponents
		else if (exponent < 100) outend += 2;
		else outend += 3;
		
		char* expend = outend;
		while (exponent)
		{
			*--expend = '0'+exponent%10;
			exponent /= 10;
		}
	}
	if (negative) *--out = '-';
	return bytesr((uint8_t*)out, outend-out);
}
#endif

string tostring(double f) { return tostring_float<double>(f); }
string tostring(float f) { return tostring_float<float>(f); }


#include "test.h"

template<typename T> void testundec(cstring S, T V, bool ret = true)
{
	testctx(S)
	{
		T a;
		assert_eq(fromstring(S, a), ret);
		
		// bitcast it - -0.0 and nan equality is funny
		using Ti = std::conditional_t<sizeof(T)==sizeof(uint32_t), uint32_t, uint64_t>;
		static_assert(sizeof(T) == sizeof(Ti));
		Ti ai = reinterpret<Ti>(a);
		Ti Vi = reinterpret<Ti>(V);
		if (ai != Vi)
		{
			test_nothrow
			{
				string as = tostring(a)+" "+tostringhex<sizeof(T)*2>(ai);
				string Vs = tostring(V)+" "+tostringhex<sizeof(T)*2>(Vi);
				assert_eq(as, Vs);
			}
		}
	}
}
#define testfromfloat(...) testcall(testundec<float>(__VA_ARGS__))
#define testfromdouble(...) testcall(testundec<double>(__VA_ARGS__))

test("string conversion - string to float", "", "string")
{
	testfromdouble("123", 123);
	testfromdouble("0123", 123); // no octal allowed
	testfromdouble("00123", 123);
	testfromdouble("000123", 123);
	testfromdouble("0", 0);
	testfromdouble("0000", 0);
	testfromdouble("0e1", 0);
	testfromdouble("0e-1", 0);
	testfromdouble("0e+1", 0);
	testfromdouble("11e1", 110);
	testfromdouble("11e+1", 110);
	testfromdouble("11e-1", 1.1);
	testfromdouble("1E1", 10);
	testfromdouble("-1", -1);
	testfromdouble("inf", HUGE_VAL);
	testfromdouble("-inf", -HUGE_VAL);
	testfromfloat("+inf", 0.0f, false);
	testfromfloat("infinity", 0.0f, false);
	testfromfloat("NAN", 0.0f, false);
	testfromfloat("INF", 0.0f, false);
	testfromfloat("-INF", 0.0f, false);
	testfromfloat("inf"+string::nul(), 0.0f, false);
	testfromfloat("-inf"+string::nul(), 0.0f, false);
	testfromfloat("nan"+string::nul(), 0.0f, false);
	testfromdouble("nan", NAN);
	testfromdouble("-0", -0.0);
	testfromdouble("1.2e+08", 120000000.0); // no octal allowed in exponent either
	testfromdouble("1.2e+0", 1.2);
	testfromdouble("0", 0.0);
	testfromdouble("0.0", 0.0);
	testfromdouble("-0", -0.0);
	testfromdouble("-0.0", -0.0);
	testfromfloat("2.5", 2.5);
	testfromfloat("2.5e+1", 25);
	testfromfloat("nan", NAN);
	
	testfromfloat("+1", 0.0f, false); // no + prefix allowed
	testfromfloat(" 42", 0.0f, false);
	testfromfloat("0x123", 0.0f, false);
	testfromfloat("+0x123", 0.0f, false);
	testfromfloat("-0x123", 0.0f, false);
	testfromfloat("0X123", 0.0f, false);
	testfromfloat("+0X123", 0.0f, false);
	testfromfloat("-0X123", 0.0f, false);
	testfromfloat("00x123", 0.0f, false);
	testfromfloat("+00x123", 0.0f, false);
	testfromfloat("-00x123", 0.0f, false);
	testfromfloat("1e", 0.0f, false);
	testfromfloat("1e+", 0.0f, false);
	testfromfloat("1e-", 0.0f, false);
	testfromfloat("1.", 0.0f, false);
	testfromfloat(".1", 0.0f, false);
	testfromfloat(".", 0.0f, false);
	testfromfloat("1.e+1", 0.0f, false);
	testfromfloat(".1e+1", 0.0f, false);
	testfromfloat("1.2.3", 0.0f, false);
	testfromfloat("0e99999999999999999999n", 0.0f, false);
	testfromfloat("", 0.0f, false);
	testfromfloat("2,5", 0.0f, false); // this is not the decimal separator, has never been and will never be
	testfromfloat("42"+string::nul(), 0.0f, false);
	
	testfromdouble("1e-999", 0);
	testfromdouble("-1e-999", -0.0);
	
	testfromdouble("0e99999999999999999999", 0.0);
	testfromdouble("0e-99999999999999999999", 0.0);
	testfromdouble("-0e999", -0.0);
	testfromdouble("-0e-999", -0.0);
	testfromdouble("-0e99999999999999999999", -0.0);
	testfromdouble("-0e-99999999999999999999", -0.0);
	testfromfloat("33554446", 33554448.0); // should round to even mantissa
	testfromfloat("33554450", 33554448.0);
	testfromfloat("33554451", 33554452.0);
	
	// 0.0000000000000000000000000703853100000000000000000000000000000000 <- input
	// 0.0000000000000000000000000703853069185120912085918801714030697411 <- float closest to the input (rounded)
	// 0.0000000000000000000000000703853100000000022281692450609677778769 <- double closest to the input (rounded)
	// 0.0000000000000000000000000703853130814879132477466099505324860128 <- float closest to the above double (rounded)
	// this is the most well known such problematic number, one of few that's a possible output from tostring(float),
	//  and the only one with <= 7 significand digits
	testfromfloat("7.038531e-26", 7.038531e-26f);
	static_assert(7.038531e-26f != (float)7.038531e-26); // test the compiler too
	// but there are many more, for example
	// 0.0000000000967498269000000000000000000000000000000000000000000000 <- input
	// 0.0000000000967498234305530502297187922522425651550292968750000000 <- float closest to the input (exact)
	// 0.0000000000967498269000000021833329810760915279388427734375000000 <- double closest to the input
	// 0.0000000000967498303694469541369471698999404907226562500000000000 <- float closest to the above double
	testfromfloat("9.67498269e-11", 9.67498269e-11f);
	static_assert(9.67498269e-11f != (float)9.67498269e-11);
	testfromfloat("1.199999988079071",     // 1.1999999880790710000000000000 <- input
	                  1.199999988079071f); // 1.1999999284744262695312500000 <- float closest to the input
	static_assert(1.199999988079071f !=    // 1.1999999880790710449218750000 <- double closest to the input
	       (float)1.199999988079071);      // 1.2000000476837158203125000000 <- float closest to the above double
	testfromfloat("1591091017e+10",     // 15910910170000000000.0 <- input
	                  1591091017e+10f); // 15910910719755812864.0 <- float closest to the input
	static_assert(1591091017e+10f !=    // 15910910169999998976.0 <- double closest to the input
	       (float)1591091017e+10);      // 15910909620244185088.0 <- float closest to the above double
	// which also have the property that both significand and 10**abs(exponent) are exactly representable as double,
	//  so the correct double can be calculated with two integer to double conversions and a multiplication or division,
	//  but correct float would yield double rounding
	
	testfromfloat("340282346638528859811704183484516925440", 340282346638528859811704183484516925440.0f); // max possible float; a few higher values round down to that,
	testfromfloat("340282356779733661637539395458142568448", HUGE_VALF, false); // but anything too big should be rejected
	testfromfloat("355555555555555555555555555555555555555", HUGE_VALF, false); // ensure it's inf and not nan
	testfromfloat("955555555555555555555555555555555555555", HUGE_VALF, false);
	testfromfloat("1e+10000", HUGE_VALF, false);
	testfromfloat("1e-10000", 0);
	testfromfloat("0e+10000", 0);
	testfromfloat("1e9999999999", HUGE_VALF, false);
	testfromfloat("18446744073709551616e+1000000", HUGE_VALF, false);
	testfromfloat("1e+18446744073709551616", HUGE_VALF, false);
	testfromfloat("340282366920938463463374607431768211456", HUGE_VALF, false); // 2**128 - infinity
	
	// also add the lowest two float subnormals, their midpoint, and a tiny sliver off from their midpoint
	// highest double
	testfromdouble("1797693134862315708145274237317043567980705675258449965989174768031572607800285387605895586327668781715"
	               "4045895351438246423432132688946418276846754670353751698604991057655128207624549009038932894407586850845"
	               "5133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368",
	               1.7976931348623157e+308);
	// second highest double
	testfromdouble("1797693134862315508561243283845062402343434371574593359244048724485818457545561143884706399431262203219"
	               "6080402715737157080985288496451174304408766276760090959433192772823707887618876057953256376869865406482"
	               "5262115771015791463983014857704008123419459386245141723703148097529108423358883457665451722744025579520",
	               1.7976931348623155e+308);
	// 2**1024 - infinity
	testfromdouble("1797693134862315907729305190789024733617976978942306572734300811577326758055009631327084773224075360211"
	               "2011387987139335765878976881441662249284743063947412437776789342486548527630221960124609411945308295208"
	               "5005768838150682342462881473913110540827237163350510684586298239947245938479716304835356329624224137216",
	               HUGE_VAL, false);
	// more than the above, ensure it's inf and not nan
	testfromdouble("1999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999"
	               "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	               "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
	               HUGE_VAL, false);
	testfromdouble("9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999"
	               "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	               "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
	               HUGE_VAL, false);
	// halfway between the highest two doubles - round to even
	testfromdouble("1797693134862315608353258760581052985162070023416521662616611746258695532672923265745300992879465492467"
	               "5063149033587701752208710592698796290627760473556921329019091915239418047621712533496094635638726128664"
	               "0198029037799514183602981511756283727771403830521483963923935633133642802139091669457927874464075218944",
	               1.7976931348623155e+308);
	// halfway between infinity and highest double - round to infinity
	testfromdouble("1797693134862315807937289714053034150799341327100378269361737789804449682927647509466490179775872070963"
	               "3028641669288791094655554785194040263065748867150582068190890200070838367627385484581771153176447573027"
	               "0069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497792",
	               HUGE_VAL, false);
	// slightly above halfway between the highest two doubles - round to closest (odd)
	testfromdouble("1797693134862315608353258760581052985162070023416521662616611746258695532672923265745300992879465492467"
	               "5063149033587701752208710592698796290627760473556921329019091915239418047621712533496094635638726128664"
	               "0198029037799514183602981511756283727771403830521483963923935633133642802139091669457927874464075218945",
	               1.7976931348623157e+308);
	// slightly below halfway between infinity and highest double - round to closest (odd)
	testfromdouble("1797693134862315807937289714053034150799341327100378269361737789804449682927647509466490179775872070963"
	               "3028641669288791094655554785194040263065748867150582068190890200070838367627385484581771153176447573027"
	               "0069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497791",
	               1.7976931348623157e+308);
	// a few perfectly normal numbers, just dubiously long
	testfromdouble("1.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
	               1.0);
	testfromdouble("10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "e-1000",
	               1.0);
	testfromdouble("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001"
	                 "e+1000",
	               1.0);
	// extra digits - should be discarded
	testfromdouble("1.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                 "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                 "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                 "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                 "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                 "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                 "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                 "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                 "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890",
	               1.0);
	testfromdouble("10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
	                "e-1000",
	               1.0);
	
	// smallest possible subnormal double
	testfromdouble("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000049406564584124654417656879286822137236505980261432476442558568250067550727020"
	                 "8751865299836361635992379796564695445717730926656710355939796398774796010781878126300713190311404527"
	                 "8458171678489821036887186360569987307230500063874091535649843873124733972731696151400317153853980741"
	                 "2623856559117102665855668676818703956031062493194527159149245532930545654440112748012970999954193198"
	                 "9409080416563324524757147869014726780159355238611550134803526493472019379026810710749170333222684475"
	                 "3335720832431936092382893458368060106011506169809753078342277318329247904982524730776375927247874656"
	                 "0847782037344696995336470179726777175851256605511991315048911014510378627381672509558373897335989936"
	                 "64809941164205702637090279242767544565229087538682506419718265533447265625",
	                 5e-324);
	// half of the above, should round to even i.e. to exactly 0.0
	testfromdouble("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000024703282292062327208828439643411068618252990130716238221279284125033775363510"
	                 "4375932649918180817996189898282347722858865463328355177969898199387398005390939063150356595155702263"
	                 "9229085839244910518443593180284993653615250031937045767824921936562366986365848075700158576926990370"
	                 "6311928279558551332927834338409351978015531246597263579574622766465272827220056374006485499977096599"
	                 "4704540208281662262378573934507363390079677619305775067401763246736009689513405355374585166611342237"
	                 "6667860416215968046191446729184030053005753084904876539171138659164623952491262365388187963623937328"
	                 "0423891018672348497668235089863388587925628302755995657524455507255189313690836254779186948667994968"
	                 "324049705821028513185451396213837722826145437693412532098591327667236328125",
	                 0.0);
	// slightly more than the above
	testfromdouble("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000024703282292062327208828439643411068618252990130716238221279284125033775363510"
	                 "4375932649918180817996189898282347722858865463328355177969898199387398005390939063150356595155702263"
	                 "9229085839244910518443593180284993653615250031937045767824921936562366986365848075700158576926990370"
	                 "6311928279558551332927834338409351978015531246597263579574622766465272827220056374006485499977096599"
	                 "4704540208281662262378573934507363390079677619305775067401763246736009689513405355374585166611342237"
	                 "6667860416215968046191446729184030053005753084904876539171138659164623952491262365388187963623937328"
	                 "0423891018672348497668235089863388587925628302755995657524455507255189313690836254779186948667994968"
	                 "32404970582102851318545139621383772282614543769341253209859132766723632812500000000000001",
	                 5e-324);
	// largest subnormal
	testfromdouble("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000222507385850720088902458687608585988765042311224095946549352480256244000922823569517877588880"
	                 "3759155264230978095043431208587738715835729182199302029437922422355981982750124204178896957131179108"
	                 "2261043971979604000454897391938079198936081525613113376149842043271751033627391549782731594143828136"
	                 "2751138386040942494649422863166954291050802018159266421349966065178030950759130587198464239060686371"
	                 "0200510872328278467884363194451586613504122347901479236958520832159762106637540161373658304419360371"
	                 "4778355306682834535634005074073040135602968046375918583163124224521599262546494300836851861719422417"
	                 "6464551371354201322170313704965832101546540680353974179060225895030235019375197730309457631732108525"
	                 "07299305089761582519159720757232455434770912461317493580281734466552734375",
	                 2.225073858507201e-308);
	// second largest subnormal
	testfromdouble("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000222507385850720039495894103483931571108163024401958710043372218823767558364255319450326861859"
	                 "5007289964394616459051051412023043270117998255542591673498126023581185971968246077878183766819774580"
	                 "3802872293489782963567711031368091891705581461739021840499998170147017060895695398382414440289847395"
	                 "0127281826923839828793754186348250335019739524964739262200720532247485296319017839185493239106493172"
	                 "0791430455764953943127215325436859833344767109289929102154994338687742727610729450624487971196675896"
	                 "1442634474250898443251111615704980029591461876566165504820846906192351357563969570060475934471547761"
	                 "5616769334009504326833843525239054925695284074841982864011314880519856391993525220751083734396118588"
	                 "4248936392555587988206944151446491086954182492263498716056346893310546875",
	                 2.2250738585072004e-308);
	// smallest normal
	testfromdouble("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000222507385850720138309023271733240406421921598046233183055332741688720443481391819585428315901"
	                 "2511020564067339731035811005152434161553460108856012385377718821130777993532002330479610147442583636"
	                 "0719215650469425037342083752508066506166581589487204911799685916396485006359087701183048747997808877"
	                 "5374994945158045160505091539985658247081864511353793580499211598108576605199243335211435239014879569"
	                 "9609591288891602992641511063466313393663477586513029371762047325631781485664350872122828637642044846"
	                 "8114076139114770628016898532441100241614474216185671661505401542850847167529019031613227788967297073"
	                 "7312333408698898317506783884692609277397797285865965494109136909540613646756870239867831529068098461"
	                 "7210924625396728515625",
	                 2.2250738585072014e-308);
	// midway between two largest subnormals - should round to even, aka the smaller one
	// if any one of the 768 digits (or the 308 leading zeroes) is incremented, or there's a nonzero digit afterwards, it's another value
	testfromdouble("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000222507385850720064199176395546258779936602667813027328296362349540005779643539444484102225369"
	                 "9383222614312797277047241310305390992976863718870946851468024222968583977359185141028540361975476844"
	                 "3031958132734693482011304211653085545320831493676067608324920106709384047261543474082573017216837765"
	                 "6439210106482391161721588524757602313035270771562002841775343298712758123539074213191978739083589771"
	                 "5495970664046616205505789259944223223424444728595704169556757585423752417124134805999073137808018133"
	                 "8110494890466866489442558344889010082597214961471042043991985565356975310055231935448663898095485089"
	                 "6040660352681852824502078615102443513620912377597978521535770387775045705684361475530270683064113556"
	                 "748943345076587312006145811358486831521563686919762403704226016998291015625",
	                 2.2250738585072004e-308);
	// same as above, but with over 800 digits
	testfromdouble("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000222507385850720064199176395546258779936602667813027328296362349540005779643539444484102225369"
	                 "9383222614312797277047241310305390992976863718870946851468024222968583977359185141028540361975476844"
	                 "3031958132734693482011304211653085545320831493676067608324920106709384047261543474082573017216837765"
	                 "6439210106482391161721588524757602313035270771562002841775343298712758123539074213191978739083589771"
	                 "5495970664046616205505789259944223223424444728595704169556757585423752417124134805999073137808018133"
	                 "8110494890466866489442558344889010082597214961471042043991985565356975310055231935448663898095485089"
	                 "6040660352681852824502078615102443513620912377597978521535770387775045705684361475530270683064113556"
	                 "7489433450765873120061458113584868315215636869197624037042260169982910156250000000000000000000000000"
	                 "000000000000000000000000000000000000000",
	                 2.2250738585072004e-308);
	// slightly more than the above - now it's closer to the odd one
	testfromdouble("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000222507385850720064199176395546258779936602667813027328296362349540005779643539444484102225369"
	                 "9383222614312797277047241310305390992976863718870946851468024222968583977359185141028540361975476844"
	                 "3031958132734693482011304211653085545320831493676067608324920106709384047261543474082573017216837765"
	                 "6439210106482391161721588524757602313035270771562002841775343298712758123539074213191978739083589771"
	                 "5495970664046616205505789259944223223424444728595704169556757585423752417124134805999073137808018133"
	                 "8110494890466866489442558344889010082597214961471042043991985565356975310055231935448663898095485089"
	                 "6040660352681852824502078615102443513620912377597978521535770387775045705684361475530270683064113556"
	                 "7489433450765873120061458113584868315215636869197624037042260169982910156250000000000000000000000000"
	                 "000000000000000000000000000000000000001",
	                 2.225073858507201e-308);
	// midway between largest subnormal and smallest subnormal - should round to even, aka the larger one
	testfromdouble("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000222507385850720113605740979670913197593481954635164564802342610972482222202107694551652952390"
	                 "8135087914149158913039621106870086438694594645527657207407820621743379988141063267329253552286881372"
	                 "1490129811224514518898490572223072852551331557550159143974763979834118019993239625482890171070818506"
	                 "9063066665599493827577257201576306269066333264756530000924588831643303777979186961204949739037782970"
	                 "4905051080609940730262937128958950003583799967207254304360284078895771796150945516748243471030702609"
	                 "1446215722898802581825451803257070188608721131280795122334262883686223215037756666225039825343359745"
	                 "6888442390026549819838548794829220689472168983109969836584681402285424333066033985088644580400103493"
	                 "397042756718644338377048603786162277173854562306587467901408672332763671875",
	                 2.2250738585072014e-308);
	// slightly less than the above, now it's closer to the odd one
	testfromdouble("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                 "0000000222507385850720113605740979670913197593481954635164564802342610972482222202107694551652952390"
	                 "8135087914149158913039621106870086438694594645527657207407820621743379988141063267329253552286881372"
	                 "1490129811224514518898490572223072852551331557550159143974763979834118019993239625482890171070818506"
	                 "9063066665599493827577257201576306269066333264756530000924588831643303777979186961204949739037782970"
	                 "4905051080609940730262937128958950003583799967207254304360284078895771796150945516748243471030702609"
	                 "1446215722898802581825451803257070188608721131280795122334262883686223215037756666225039825343359745"
	                 "6888442390026549819838548794829220689472168983109969836584681402285424333066033985088644580400103493"
	                 "3970427567186443383770486037861622771738545623065874679014086723327636718749999999999999999999999999"
	                 "999999999999999999999999999999999999999",
	                 2.225073858507201e-308);
	
	// decimal point after the 800th digit (with some extra 1s to ensure it can't just trim zeroes)
	testfromdouble("10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	                "01000.0001e-805",
	                1.0);
	
	// https://www.exploringbinary.com/incorrectly-rounded-conversions-in-visual-c-plus-plus/
	// many other troublesome numbers can be found at exploringbinary.com
	// and https://github.com/ahrvoje/numerics/blob/master/strtod/strtod_tests.toml
	testfromdouble("9214843084008499.0", 9214843084008500.0);
	testfromdouble("0.500000000000000166533453693773481063544750213623046875", 0.500000000000000222045);
	testfromdouble("30078505129381147446200", 30078505129381149540352.0);
	testfromdouble("1777820000000000000001", 1777820000000000131072.0);
	testfromdouble("0.500000000000000166547006220929549868969843373633921146392822265625", 0.50000000000000022204);
	testfromdouble("0.50000000000000016656055874808561867439493653364479541778564453125", 0.50000000000000022204);
	testfromdouble("0.3932922657273", 0.39329226572730002776);
	testfromdouble("6929495644600919.5", 6929495644600920.0);
	testfromdouble("3.2378839133029012895883524125015321748630376694231080599012970495523019706706765657868357425877995578"
	               "6061577655983828343551439108415316925268919056439645957739461803892836530514346395510035669666562920"
	               "2017331344031730044369360205258345803431471660032699580731300954848363975548690010751530018881758184"
	               "1745696521731104736960227499346384253806233697747365600089974040609674980283891918789639685754392222"
	               "0641698146269011334252400272438594165105129355260142115533343022523729152384332233132613843147782359"
	               "1142408800030775170625915670728657003151953664260769822494937951845801530895238439819708403389937873"
	               "2414634842056080000272705311068273879077914449185347715987501628125488627684932015189916680282517302"
	               "99953143924168545708663913273994694463908672332763671875e-319", 3.2379e-319);
	
	testfromfloat("33554431", 33554432.0f); // rounds up to next power of two
	testfromfloat("0.0000000000000000000000000000000000000000000014012984643248170709237295832899161312802619418765157717"
	              "5706828388979108268586060148663818836212158203125", 1e-45f); // smallest subnormal float
	testfromfloat("0.0000000000000000000000000000000000000000000028025969286496341418474591665798322625605238837530315435"
	              "141365677795821653717212029732763767242431640625", 3e-45f); // second smallest
	testfromfloat("0.0000000000000000000000000000000000000000000021019476964872256063855943749348741969203929128147736576"
	              "35602425834686624028790902229957282543182373046875", 3e-45f); // halfway between the above, round to even
	testfromfloat("0.0000000000000000000000000000000000000000000007006492321624085354618647916449580656401309709382578858"
	              "78534141944895541342930300743319094181060791015625", 0); // half of smallest subnormal float = zero
	testfromfloat("0.0000000000000000000000000000000000000000000021019476964872256063855943749348741969203929128147736576"
	              "3560242583468662402879090222995728254318237304687499999999999999999999999999999999999999999999999999999",
	              1e-45f); // the above two plus or minus a tiny sliver
	testfromfloat("0.0000000000000000000000000000000000000000000007006492321624085354618647916449580656401309709382578858"
	              "7853414194489554134293030074331909418106079101562500000000000000000000000000000000000000000000000000001",
	              1e-45f);
}

test("string conversion - float to string", "", "string")
{
	assert_eq(tostring(1.0), "1");
	assert_eq(tostring(3.0), "3");
	assert_eq(tostring(10.0), "10");
	assert_eq(tostring(123.0), "123");
	assert_eq(tostring(0.1), "0.1");
	assert_eq(tostring(0.2), "0.2");
	assert_eq(tostring(0.3), "0.3");
	assert_eq(tostring(-0.1), "-0.1");
	assert_eq(tostring(0.0), "0");
	assert_eq(tostring(-0.0), "-0");
	assert_eq(tostring(1.0/0.0), "inf");
	assert_eq(tostring(-1.0/0.0), "-inf");
	assert_eq(tostring(0.0/0.0), "nan");
	assert_eq(tostring(0.1f), "0.1");
	assert_eq(tostring(-0.01), "-0.01");
	assert_eq(tostring(-0.0001), "-0.0001");
	assert_eq(tostring(-0.00001), "-1e-5");
	assert_eq(tostring(-1.0), "-1");
	assert_eq(tostring(-1000.0), "-1000");
	assert_eq(tostring(-10.01), "-10.01");
	assert_eq(tostring(-1000000000000000.0), "-1000000000000000");
	assert_eq(tostring(-10000000000000000.0), "-1e+16");
	assert_eq(tostring(reinterpret<float>(0x7F800000)), "inf");
	assert_eq(tostring(reinterpret<float>(0x7F800001)), "nan");
	assert_eq(tostring(reinterpret<float>(0x7FFFFFFF)), "nan");
	assert_eq(tostring(reinterpret<float>(0xFFFFFFFF)), "nan");
	
	assert_eq(tostring(0.1+0.2), "0.30000000000000004");
	assert_eq(tostring(0.7-0.4), "0.29999999999999993");
	assert_eq(tostring(0.9999999999999999), "0.9999999999999999"); // next representable double is 1
	assert_eq(tostring(4.999999999999999), "4.999999999999999"); // next is 5
	assert_eq(tostring(9.999999999999998), "9.999999999999998"); // next is 10
	assert_eq(tostring(999999999999999.9), "999999999999999.9"); // largest non-integer where next is a power of 10
	assert_eq(tostring(2251799813685247.8), "2251799813685247.8"); // prev is .5000, next is integer
	assert_eq(tostring(2251799813685247.2), "2251799813685247.2"); // another few where both rounding directions are equally far
	assert_eq(tostring(2251799813685246.8), "2251799813685246.8"); // should round last digit to even
	assert_eq(tostring(2251799813685246.2), "2251799813685246.2");
	assert_eq(tostring(4503599627370495.5), "4503599627370495.5"); // largest non-integer
	assert_eq(tostring(4503599627370494.5), "4503599627370494.5"); // second largest non-integer
	assert_eq(tostring(399999999999999.94), "399999999999999.94"); // next is integer, prev's fraction doesn't start with 9
	assert_eq(tostring(0.6822871999174), "0.6822871999174"); // glitchy in C# ToString R
	assert_eq(tostring(0.6822871999174001), "0.6822871999174001");
	assert_eq(tostring(0.84551240822557006), "0.8455124082255701");
	assert_eq(tostring((double)0.1f), "0.10000000149011612");
	assert_eq(tostring(1.7976931348623157081452742373170e+308), "1.7976931348623157e+308"); // max possible double
	assert_eq(tostring(1.7976931348623155085612432838451e+308), "1.7976931348623155e+308"); // second largest
	assert_eq(tostring(5e-324), "5e-324"); // smallest possible
	assert_eq(tostring(1e-323), "1e-323"); // second smallest
	assert_eq(tostring(1e+101), "1e+101"); // ensure it doesn't become 1e+11 if second-to-last char is zero
	assert_eq(tostring(0.00009999999999999999123964644632), "9.999999999999999e-5"); // the scientific notation cutoff points
	assert_eq(tostring(0.00010000000000000000479217360239), "0.0001");
	assert_eq(tostring(0.00010000000000000003189722791452), "0.00010000000000000003");
	assert_eq(tostring( 9999999999999998.0), "9999999999999998");
	assert_eq(tostring(10000000000000000.0), "1e+16");
	assert_eq(tostring(10000000000000002.0), "1.0000000000000002e+16");
	assert_eq(tostring(7.14169434645052e-92), "7.14169434645052e-92");
	assert_eq(tostring(-1.2345678901234567e+123), "-1.2345678901234567e+123"); // longest possible double in scientific
	assert_eq(tostring(-1.2345678901234567e-123), "-1.2345678901234567e-123"); // equally long
	assert_eq(tostring(-0.00012345678901234567), "-0.00012345678901234567"); // longest possible in decimal with this ruleset
	assert_eq(tostring(5.9604644775390618382e-8), "5.960464477539062e-8"); // middle is tricky to round correctly
	assert_eq(tostring(5.9604644775390625000e-8), "5.960464477539063e-8"); // printf to 16 digits is ...2 which is wrong,
	assert_eq(tostring(5.9604644775390638234e-8), "5.960464477539064e-8"); // but 16 is still possible
	assert_eq(tostring(5.3169119831396629013e+36), "5.316911983139663e+36"); // another increment-last
	assert_eq(tostring(5.3169119831396634916e+36), "5.316911983139664e+36"); // (four of these six are just the tricky ones' neighbors,
	assert_eq(tostring(5.3169119831396646722e+36), "5.316911983139665e+36"); //  for human readers)
	assert_eq(tostring(9223372036854775808.0), "9.223372036854776e+18"); // abs((int64_t)this) is less than the 10000000000000000 threshold
	
	assert_eq(tostring(0.000099999990197829902172088623046875f ), "9.999999e-5");
	assert_eq(tostring(0.0000999999974737875163555145263671875f), "0.0001"); // (float)0.0001 < 0.0001, but should be decimal anyways
	assert_eq(tostring(0.0001000000047497451305389404296875f   ), "0.000100000005"); // also test the two numbers around it
	assert_eq(tostring( 9999999198822400.0f), "9999999198822400");
	assert_eq(tostring(10000000000000000.0f), "1e+16");
	assert_eq(10000000000000000.0f, 10000000272564224.0f);
	assert_eq(tostring(10000001346306048.0f), "1.0000001e+16");
	assert_eq(tostring(0.0000000000000000000000000000000000000000000014012984643248170709f), "1e-45"); // smallest positive float
	assert_eq(tostring(340282346638528859811704183484516925440.0f), "3.4028235e+38"); // max possible float
	assert_eq(tostring(340282326356119256160033759537265639424.0f), "3.4028233e+38"); // second largest
	assert_eq(tostring(9.99e-43f), "9.99e-43");
	assert_eq(tostring(4.7019785e-38f), "4.7019785e-38");
	assert_eq(tostring(9.40397050112110050170108664354930e-38f), "9.40397e-38"); // printing to 6 decimals gives nonzero last
	assert_eq(tostring(0.00024414061044808477163314819336f), "0.00024414061");
	assert_eq(tostring(0.00024414062500000000000000000000f), "0.00024414062"); // last digit should round to even, to 2
	assert_eq(tostring(0.00024414065410383045673370361328f), "0.00024414065");
	assert_eq(tostring(0.00000002980718250000791158527136f), "2.9807183e-8");
	assert_eq(tostring(3.355445e+7f), "33554448"); // expanding shortest scientific notation to decimal gives wrong answer
	assert_eq(tostring(1.262177373e-29f), "1.2621774e-29");
	assert_eq(tostring(1.262177448e-29f), "1.2621775e-29"); // one of three increment-last floats in this ruleset
	assert_eq(tostring(1.262177598e-29f), "1.2621776e-29"); // (the other two are 1.5474251e+26f and 1.2379401e+27f)
	assert_eq(tostring(4.30373586499999995214071901727947988547384738922119140625e-15f), "4.303736e-15"); // easy to round wrong
	assert_eq(tostring(-1234567948140544.0f), "-1234567948140544"); // longest float
	assert_eq(tostring(-1.2345678e+23f), "-1.2345678e+23"); // longest float in exponential form
	assert_eq(tostring(9223372036854775808.0f), "9.223372e+18"); // INT64_MIN again
}
