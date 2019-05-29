#include "punycode.h"
#include "linq.h"

#define base         36
#define tmin         1
#define tmax         26
#define skew         38
#define damp         700
#define initial_bias 72
#define initial_n    128

static size_t adapt(size_t delta, size_t numpoints, bool firsttime)
{
	if (firsttime) delta /= damp;
	else delta /= 2;
	delta += delta/numpoints;
	
	size_t k = 0;
	while (delta > ((base-tmin)*tmax) / 2)
	{
		delta = delta / (base-tmin);
		k += base;
	}
	return k + (((base-tmin+1)*delta) / (delta+skew));
}

string puny_decode_label(cstring puny)
{
	array<uint32_t> output;
	
	size_t inpos = 0;
	size_t prefix = puny.lastindexof("-");
	if (prefix != (size_t)-1)
	{
		if (prefix == 0) return "";
		while (inpos < prefix)
		{
			if ((uint8_t)puny[inpos] < 32 || (uint8_t)puny[inpos] >= 128) return "";
			output.append(puny[inpos]);
			inpos++;
		}
		inpos++;
	}
	
	uint32_t n = initial_n; // codepoint
	size_t i = 0; // string position
	size_t bias = initial_bias;
	
	while (inpos < puny.length())
	{
		size_t oldi = i;
		size_t w = 1;
		for (size_t k=base;true;k+=base)
		{
			uint8_t ch = puny[inpos++];
			uint8_t digit;
			if(0);
			else if ('a' <= ch && ch <= 'z') digit = ch-'a';
			else if ('0' <= ch && ch <= '9') digit = ch-'0'+26;
			else if ('Z' <= ch && ch <= 'Z') digit = ch-'A';
			else return ""; // puny[puny.len] is 0, which hits here
			
			i += digit*w;
			if (i >= SIZE_MAX/base) return ""; // overflow check
			
			size_t t;
			if(0);
			else if (k <= bias+tmin) t = tmin;
			else if (k >= bias+tmax) t = tmax;
			else t = k-bias;
			if (digit < t) break;
			
			if (w >= SIZE_MAX/(base-tmin)) return ""; // using tmin rather than t so the division can be optimized out
			w *= base-t;
		}
		
		bias = adapt(i-oldi, output.size()+1, (oldi==0));
		n += i / (output.size()+1);
		if (n > 0x10FFFF) return "";
		i = i % (output.size()+1);
		
		output.insert(i, n);
		
		i++;
	}
	
	return output.select([](uint32_t cp) { return string::codepoint(cp); }).as_array().join();
}

string puny_decode(cstring domain)
{
	if (!domain.contains("xn--")) return domain; // short circuit the common case
	array<string> parts = domain.split(".");
	for (size_t i=0;i<parts.size();i++)
	{
		if (parts[i].startswith("xn--"))
			parts[i] = puny_decode_label(parts[i].substr(4, ~0));
	}
	return parts.join(".");
}

/*
encoding process:
https://www.rfc-editor.org/rfc/rfc3492.txt

   let n = initial_n
   let delta = 0
   let bias = initial_bias
   let h = b = the number of basic code points in the input
   copy them to the output in order, followed by a delimiter if b > 0
   {if the input contains a non-basic code point < n then fail}
   while h < length(input) do begin
     let m = the minimum {non-basic} code point >= n in the input
     let delta = delta + (m - n) * (h + 1), fail on overflow
     let n = m
     for each code point c in the input (in order) do begin
       if c < n {or c is basic} then increment delta, fail on overflow
       if c == n then begin
         let q = delta
         for k = base to infinity in steps of base do begin
           let t = tmin if k <= bias {+ tmin}, or
                   tmax if k >= bias + tmax, or k - bias otherwise
           if q < t then break
           output the code point for digit t + ((q - t) mod (base - t))
           let q = (q - t) div (base - t)
         end
         output the code point for digit q
         let bias = adapt(delta, h + 1, test h equals b?)
         let delta = 0
         increment h
       end
     end
     increment delta and n
   end
*/

#undef base
#include "test.h"
test("punycode","string","punycode")
{
	assert_eq(puny_decode_label("ls8h"), "💩");
	assert_eq(puny_decode_label("Mnchen-3ya"), "München");
	assert_eq(puny_decode_label("smrgsrka-5zah8p"), "smörgåsräka");
	assert_eq(puny_decode_label("p8j0a4a7god8ggaz5e8o5h"), "ウィキペディアへようこそ");
	assert_eq(puny_decode_label("-> $1.00 <--"), "-> $1.00 <-");
	assert_eq(puny_decode("xn--n28h.xn--n28h"), "😉.😉");
}
