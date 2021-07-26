#pragma once
#include "global.h"
#include "array.h"
#include <stdint.h>

//This file defines:
//Macros END_LITTLE and END_BIG
//  Defined to 1 if that's the current machine's endian, otherwise 0. Arlib does not support mixed-endian or middle-endian systems.
//readu_{le,be}{8,16,32,64,f32,f64}()
//  Reads and returns an X-endian uintN_t or float/double from the given pointer. Accepts misaligned input.
//  The 8bit ones are trivial, but exist for consistency.
//writeu_{le,be}{8,16,32,64,f32,f64}()
//  The inverse of readu; writes an X-endian uintN_t into the given pointer. Accepts misaligned input.
//pack_{le,be}{8,16,32,64,f32,f64}()
//  Like writeu, but instead of taking a pointer, it returns an sarray<uint8_t,N>.

// first line is for GCC 4.6 and Clang 3.2, second is for MSVC
#if (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || \
    defined(_M_IX86) || defined(_M_AMD64) || defined(_M_ARM) || defined(_M_ARM64) || defined(_M_ALPHA) || defined(_M_IA64)
// per https://devblogs.microsoft.com/oldnewthing/20170814-00/?p=96806, a u16 on _M_ALPHA at 0x1357 has the low byte at 0x1357 -> LE
// googling _M_IA64 says it too is little endian
# define END_LITTLE 1
# define END_BIG 0
#elif (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) || \
    defined(_M_PPC)
# define END_BIG 1
# define END_LITTLE 0
#elif (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_PDP_ENDIAN__)
# error "too weird platform, not supported"
#else
# error "unknown platform, can't determine endianness"
#endif
#if defined(__FLOAT_WORD_ORDER__) && __BYTE_ORDER__ != __FLOAT_WORD_ORDER__
// can't find any evidence of this being reachable on any platform, dead or not, other than 'the C standard allows it',
//  circular references, and some weird ARM thing that I can't determine if it ever actually existed
// (C standard allows a lot of DeathStation 9000 that will never exist, like reverse PDP endian, so that's a meaningless argument)
// such platforms are so rare Clang doesn't even define __FLOAT_WORD_ORDER__ on any platform
# error "too weird platform, not supported"
#endif

#if defined(__GNUC__)
//GCC detects the pattern and optimizes it, but MSVC doesn't, so I need the intrinsics. No reason not to use both.
#define end_swap16 __builtin_bswap16
#define end_swap32 __builtin_bswap32
#define end_swap64 __builtin_bswap64
#elif defined(_MSC_VER)
#define end_swap16 _byteswap_ushort
#define end_swap32 _byteswap_ulong
#define end_swap64 _byteswap_uint64
#else
static uint16_t end_swap16(uint16_t n) { return n>>8 | n<<8; }
static uint32_t end_swap32(uint32_t n)
{
	n = n>>16 | n<<16;
	n = (n&0x00FF00FF)<<8 | (n&0xFF00FF00)>>8;
	return n;
}
static uint64_t end_swap64(uint64_t n)
{
	n = n>>32 | n<<32;
	n = (n&0x0000FFFF0000FFFF)<<16 | (n&0xFFFF0000FFFF0000)>>16;
	n = (n&0x00FF00FF00FF00FF)<<8  | (n&0xFF00FF00FF00FF00)>>8;
	return n;
}
#endif

forceinline uint8_t  readu_le8( const uint8_t* in) { return *in; }
forceinline uint8_t  readu_be8( const uint8_t* in) { return *in; }
forceinline uint16_t readu_le16(const uint8_t* in) { uint16_t ret; memcpy(&ret, in, sizeof(ret)); return END_BIG ? end_swap16(ret) : ret; }
forceinline uint16_t readu_be16(const uint8_t* in) { uint16_t ret; memcpy(&ret, in, sizeof(ret)); return END_BIG ? ret : end_swap16(ret); }
forceinline uint32_t readu_le32(const uint8_t* in) { uint32_t ret; memcpy(&ret, in, sizeof(ret)); return END_BIG ? end_swap32(ret) : ret; }
forceinline uint32_t readu_be32(const uint8_t* in) { uint32_t ret; memcpy(&ret, in, sizeof(ret)); return END_BIG ? ret : end_swap32(ret); }
forceinline uint64_t readu_le64(const uint8_t* in) { uint64_t ret; memcpy(&ret, in, sizeof(ret)); return END_BIG ? end_swap64(ret) : ret; }
forceinline uint64_t readu_be64(const uint8_t* in) { uint64_t ret; memcpy(&ret, in, sizeof(ret)); return END_BIG ? ret : end_swap64(ret); }

forceinline float  readu_lef32(const uint8_t* in) { return reinterpret<float >(readu_le32(in)); }
forceinline float  readu_bef32(const uint8_t* in) { return reinterpret<float >(readu_be32(in)); }
forceinline double readu_lef64(const uint8_t* in) { return reinterpret<double>(readu_le64(in)); }
forceinline double readu_bef64(const uint8_t* in) { return reinterpret<double>(readu_be64(in)); }

// TODO: do I need this?
/*
template<typename T> forceinline T readu_le(const uint8_t* in)
{
	static_assert(std::is_arithmetic_v<T>);
	if (std::is_same_v<bool,std::remove_cv_t<T>>) // reinterpret<bool>(2) is UB, and sizeof(bool) != 1 on PowerPC; override it
		return readu_le8(in);
	if (sizeof(T) == 1) return reinterpret<T>(readu_le8( in));
	if (sizeof(T) == 2) return reinterpret<T>(readu_le16(in));
	if (sizeof(T) == 4) return reinterpret<T>(readu_le32(in));
	if (sizeof(T) == 8) return reinterpret<T>(readu_le64(in));
}
template<typename T> forceinline T readu_be(const uint8_t* in)
{
	static_assert(std::is_arithmetic_v<T>);
	if (std::is_same_v<bool,std::remove_cv_t<T>>)
		return readu_be8(in);
	if (sizeof(T) == 1) return reinterpret<T>(readu_be8( in));
	if (sizeof(T) == 2) return reinterpret<T>(readu_be16(in));
	if (sizeof(T) == 4) return reinterpret<T>(readu_be32(in));
	if (sizeof(T) == 8) return reinterpret<T>(readu_be64(in));
}
*/

forceinline void writeu_le8( uint8_t* target, uint8_t  n) { *target = n; }
forceinline void writeu_be8( uint8_t* target, uint8_t  n) { *target = n; }
forceinline void writeu_le16(uint8_t* target, uint16_t n) { n = END_BIG ? end_swap16(n) : n; memcpy(target, &n, 2); }
forceinline void writeu_be16(uint8_t* target, uint16_t n) { n = END_BIG ? n : end_swap16(n); memcpy(target, &n, 2); }
forceinline void writeu_le32(uint8_t* target, uint32_t n) { n = END_BIG ? end_swap32(n) : n; memcpy(target, &n, 4); }
forceinline void writeu_be32(uint8_t* target, uint32_t n) { n = END_BIG ? n : end_swap32(n); memcpy(target, &n, 4); }
forceinline void writeu_le64(uint8_t* target, uint64_t n) { n = END_BIG ? end_swap64(n) : n; memcpy(target, &n, 8); }
forceinline void writeu_be64(uint8_t* target, uint64_t n) { n = END_BIG ? n : end_swap64(n); memcpy(target, &n, 8); }

forceinline void writeu_lef32(uint8_t* target, float  n) { writeu_le32(target, reinterpret<uint32_t>(n)); }
forceinline void writeu_bef32(uint8_t* target, float  n) { writeu_be32(target, reinterpret<uint32_t>(n)); }
forceinline void writeu_lef64(uint8_t* target, double n) { writeu_le64(target, reinterpret<uint64_t>(n)); }
forceinline void writeu_bef64(uint8_t* target, double n) { writeu_be64(target, reinterpret<uint64_t>(n)); }

forceinline sarray<uint8_t,1> pack_le8( uint8_t  n) { sarray<uint8_t,1> ret; writeu_le8( ret.ptr(), n); return ret; }
forceinline sarray<uint8_t,1> pack_be8( uint8_t  n) { sarray<uint8_t,1> ret; writeu_be8( ret.ptr(), n); return ret; }
forceinline sarray<uint8_t,2> pack_le16(uint16_t n) { sarray<uint8_t,2> ret; writeu_le16(ret.ptr(), n); return ret; }
forceinline sarray<uint8_t,2> pack_be16(uint16_t n) { sarray<uint8_t,2> ret; writeu_be16(ret.ptr(), n); return ret; }
forceinline sarray<uint8_t,4> pack_le32(uint32_t n) { sarray<uint8_t,4> ret; writeu_le32(ret.ptr(), n); return ret; }
forceinline sarray<uint8_t,4> pack_be32(uint32_t n) { sarray<uint8_t,4> ret; writeu_be32(ret.ptr(), n); return ret; }
forceinline sarray<uint8_t,8> pack_le64(uint64_t n) { sarray<uint8_t,8> ret; writeu_le64(ret.ptr(), n); return ret; }
forceinline sarray<uint8_t,8> pack_be64(uint64_t n) { sarray<uint8_t,8> ret; writeu_be64(ret.ptr(), n); return ret; }

forceinline sarray<uint8_t,4> pack_lef32(float  n) { return pack_le32(reinterpret<uint32_t>(n)); }
forceinline sarray<uint8_t,4> pack_bef32(float  n) { return pack_be32(reinterpret<uint32_t>(n)); }
forceinline sarray<uint8_t,8> pack_lef64(double n) { return pack_le64(reinterpret<uint64_t>(n)); }
forceinline sarray<uint8_t,8> pack_bef64(double n) { return pack_be64(reinterpret<uint64_t>(n)); }

#undef end_swap16 // delete these, so callers are forced to use the functions instead
#undef end_swap32
#undef end_swap64
