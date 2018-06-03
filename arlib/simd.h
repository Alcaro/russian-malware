#pragma once
#include "global.h"

#ifdef __SSE2__
#include <emmintrin.h>
#define HAVE_SIMD
#define HAVE_SIMD128

//dereferencing pointers to SIMD objects is forbidden, use loada/loadu instead
class simd128 {
	__m128i val;
	
	void x() { static_assert(sizeof(simd128) == sizeof(__m128i)); }
	
public:
	simd128(__m128i val) : val(val) {} // better not use these, but they exist if you need them
	operator __m128i() { return val; }
	
	static const size_t count8  = sizeof(__m128i)/sizeof(uint8_t);
	static const size_t count16 = sizeof(__m128i)/sizeof(uint16_t);
	static const size_t count32 = sizeof(__m128i)/sizeof(uint32_t);
	static const size_t count64 = sizeof(__m128i)/sizeof(uint64_t);
	
	//aligned / unaligned
	static simd128 loada(const simd128* ptr) { return _mm_load_si128((__m128i*)ptr); }
	static simd128 loadu(const simd128* ptr) { return _mm_loadu_si128((__m128i*)ptr); }
	
	void storea(simd128* ptr) { _mm_store_si128((__m128i*)ptr, val); }
	void storeu(simd128* ptr) { _mm_storeu_si128((__m128i*)ptr, val); }
	
	static simd128 zero() { return _mm_setzero_si128(); }
	static simd128 create8(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f, uint8_t g, uint8_t h,
	                       uint8_t i, uint8_t j, uint8_t k, uint8_t l, uint8_t m, uint8_t n, uint8_t o, uint8_t p)
	{
		return _mm_set_epi8(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p);
	}
	static simd128 create16(uint16_t a, uint16_t b, uint16_t c, uint16_t d,
	                        uint16_t e, uint16_t f, uint16_t g, uint16_t h)
	{
		return _mm_set_epi16(a,b,c,d,e,f,g,h);
	}
	static simd128 create32(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
	{
		return _mm_set_epi32(a,b,c,d);
	}
	static simd128 create64(uint64_t a, uint64_t b) { return _mm_set_epi64x(a, b); }
	
	static simd128 repeat8(uint8_t a) { return _mm_set1_epi8(a); }
	static simd128 repeat16(uint16_t a) { return _mm_set1_epi16(a); }
	static simd128 repeat32(uint32_t a) { return _mm_set1_epi32(a); }
	static simd128 repeat64(uint64_t a) { return _mm_set1_epi64x(a); }
	
	//and/or/xor/bitand are reserved keywords in c++, something with iso646
	//cleanest way to avoid those is requiring a word size
	simd128 and8( simd128 other) { return _mm_and_si128(val, other.val); }
	simd128 and16(simd128 other) { return _mm_and_si128(val, other.val); }
	simd128 and32(simd128 other) { return _mm_and_si128(val, other.val); }
	simd128 and64(simd128 other) { return _mm_and_si128(val, other.val); }
	simd128 or8( simd128 other) { return _mm_or_si128(val, other.val); }
	simd128 or16(simd128 other) { return _mm_or_si128(val, other.val); }
	simd128 or32(simd128 other) { return _mm_or_si128(val, other.val); }
	simd128 or64(simd128 other) { return _mm_or_si128(val, other.val); }
	simd128 xor8( simd128 other) { return _mm_xor_si128(val, other.val); }
	simd128 xor16(simd128 other) { return _mm_xor_si128(val, other.val); }
	simd128 xor32(simd128 other) { return _mm_xor_si128(val, other.val); }
	simd128 xor64(simd128 other) { return _mm_xor_si128(val, other.val); }
	
	simd128 lshift16(int bits) { return _mm_slli_epi16(val, bits); }
	simd128 lshift32(int bits) { return _mm_slli_epi32(val, bits); }
	simd128 lshift64(int bits) { return _mm_slli_epi64(val, bits); }
	simd128 lshift128(int bits) { return _mm_slli_si128(val, bits); }
	//right shift signed, sign extend (as opposed to zero extend)
	simd128 rshifts16(int bits) { return _mm_srai_epi16(val, bits); }
	simd128 rshifts32(int bits) { return _mm_srai_epi32(val, bits); }
	simd128 rshiftu16(int bits) { return _mm_srli_epi16(val, bits); }
	simd128 rshiftu32(int bits) { return _mm_srli_epi32(val, bits); }
	simd128 rshiftu64(int bits) { return _mm_srli_epi64(val, bits); }
	simd128 rshiftu128(int bits) { return _mm_srli_si128(val, bits); }
	
	simd128 add8( simd128 other) { return _mm_add_epi8(val, other.val); }
	simd128 add16(simd128 other) { return _mm_add_epi16(val, other.val); }
	simd128 add32(simd128 other) { return _mm_add_epi32(val, other.val); }
	simd128 add64(simd128 other) { return _mm_add_epi64(val, other.val); }
	simd128 sub8( simd128 other) { return _mm_sub_epi8(val, other.val); }
	simd128 sub16(simd128 other) { return _mm_sub_epi16(val, other.val); }
	simd128 sub32(simd128 other) { return _mm_sub_epi32(val, other.val); }
	simd128 sub64(simd128 other) { return _mm_sub_epi64(val, other.val); }
	simd128 mul16(simd128 other) { return _mm_mullo_epi16(val, other.val); }
	//multiplication high, signed
	//returns a*b>>16
	//there's no low-signed, result is the same whether it's signed or not
	simd128 mulhis16(simd128 other) { return _mm_mulhi_epi16(val, other.val); }
	simd128 mulhiu16(simd128 other) { return _mm_mulhi_epu16(val, other.val); }
	
	//true = -1, false = 0
	//compare less than, signed
	//there is no less-than instruction, only greater-than, but it's easy to swap them
	simd128 cmplts8( simd128 other) { return _mm_cmpgt_epi8( other.val, val); }
	simd128 cmpgts8( simd128 other) { return _mm_cmpgt_epi8( val, other.val); }
	simd128 cmplts16(simd128 other) { return _mm_cmpgt_epi16(other.val, val); }
	simd128 cmpgts16(simd128 other) { return _mm_cmpgt_epi16(val, other.val); }
	simd128 cmplts32(simd128 other) { return _mm_cmpgt_epi32(other.val, val); }
	simd128 cmpgts32(simd128 other) { return _mm_cmpgt_epi32(val, other.val); }
	//64bit doesn't exist
	//TODO: unsigned is signed, with an xor repeat(0x80000000) before both operands
	simd128 cmpeq8( simd128 other) { return _mm_cmpeq_epi8( val, other.val); }
	simd128 cmpeq16(simd128 other) { return _mm_cmpeq_epi16(val, other.val); }
	simd128 cmpeq32(simd128 other) { return _mm_cmpeq_epi32(val, other.val); }
	
	//Zero extends the first 8 u8s of the target, returning them in as u16s. The latter 8 u8s are ignored.
	simd128 extend8uto16() { return _mm_unpacklo_epi8(val, _mm_setzero_si128()); }
	//Returns first byte of 'this', then first byte of 'other', then second byte of 'this', etc. The latter halves are ignored.
	simd128 interleave8to16(simd128 other) { return _mm_unpacklo_epi8(val, other.val); }
	//Compresses the u16s of the input to u8s. If out of range, returns 0xFF.
	//If 'high' is given, returns that after the compressed callee; if not, zeroes.
	simd128 compress16to8u(simd128 next = zero()) { return _mm_packus_epi16(val, next.val); }
	//Compresses the i16s of the input to i8s. If out of range, returns the closest representable value.
	simd128 compress16to8s(simd128 next = zero()) { return _mm_packs_epi16(val, next.val); }
	
	template<uint8_t words> // has to be a template, or gcc gets whiny if optimizations are disabled
	simd128 shuffle32() { return _mm_shuffle_epi32(val, words); }
	template<uint8_t a, uint8_t b, uint8_t c, uint8_t d>
	simd128 shuffle32() { return shuffle32<a | b<<2 | c<<4 | d<<6>(); }
	uint32_t low32() { return _mm_cvtsi128_si32(val); }
	uint64_t low64() { return _mm_cvtsi128_si64(val); }
};
#undef simdmax
#define simdmax simd128
#endif

//this doesn't yield any measurable speedup over the 128bit SIMD, and requires an extra compile flag
//not worth the effort
#ifdef __AVX2__ggg
#include <immintrin.h>
#define HAVE_SIMD
#define HAVE_SIMD256

class simd256 {
	__m256i val;
	simd256(__m256i val) : val(val) {}
	
public:
	template<typename T> static size_t count() { return sizeof(simd256)/sizeof(T); }
	static size_t count8() { return sizeof(simd256)/sizeof(uint8_t); }
	static size_t count16() { return sizeof(simd256)/sizeof(uint16_t); }
	static size_t count32() { return sizeof(simd256)/sizeof(uint32_t); }
	static size_t count64() { return sizeof(simd256)/sizeof(uint64_t); }
	
	//aligned / unaligned
	//if index is nonzero, that many vec256s are added to the pointer before casting; this is because 
	static simd256 loada(const simd256* ptr) { return _mm256_load_si256((__m256i*)ptr); }
	static simd256 loadu(const simd256* ptr) { return _mm256_loadu_si256((__m256i*)ptr); }
	
	void storea(simd256* ptr) { _mm256_store_si256((__m256i*)ptr, val); }
	void storeu(simd256* ptr) { _mm256_storeu_si256((__m256i*)ptr, val); }
	
	static simd256 zero() { return _mm256_setzero_si256(); }
	static simd256 repeat8(uint8_t a) { return _mm256_set1_epi8(a); }
	static simd256 repeat16(uint16_t a) { return _mm256_set1_epi16(a); }
	static simd256 repeat32(uint32_t a) { return _mm256_set1_epi32(a); }
	static simd256 repeat64(uint64_t a) { return _mm256_set1_epi64x(a); }
	
	//and/or/xor/bitand are reserved keywords in c++, something with iso646
	simd256 and8( simd256 other) { return _mm256_and_si256(val, other.val); }
	simd256 and16(simd256 other) { return _mm256_and_si256(val, other.val); }
	simd256 and32(simd256 other) { return _mm256_and_si256(val, other.val); }
	simd256 and64(simd256 other) { return _mm256_and_si256(val, other.val); }
	simd256 or8( simd256 other) { return _mm256_or_si256(val, other.val); }
	simd256 or16(simd256 other) { return _mm256_or_si256(val, other.val); }
	simd256 or32(simd256 other) { return _mm256_or_si256(val, other.val); }
	simd256 or64(simd256 other) { return _mm256_or_si256(val, other.val); }
	simd256 xor8( simd256 other) { return _mm256_xor_si256(val, other.val); }
	simd256 xor16(simd256 other) { return _mm256_xor_si256(val, other.val); }
	simd256 xor32(simd256 other) { return _mm256_xor_si256(val, other.val); }
	simd256 xor64(simd256 other) { return _mm256_xor_si256(val, other.val); }
	
	//true = -1, false = 0
	//compare less than, signed
	simd256 cmplts32(simd256 other) { return _mm256_cmpgt_epi32(other.val, val); }
	simd256 cmpgts32(simd256 other) { return _mm256_cmpgt_epi32(other.val, val); }
};
#undef simdmax
#define simdmax simd256
#endif
