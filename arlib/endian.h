#pragma once
#include "global.h"
#include "array.h"
#include <stdint.h>

//This file defines:
//Macros END_LITTLE and END_BIG
//  Defined to 1 if that's the current machine's endian, otherwise 0.
//end_swap{8,16,32,64}()
//  Byteswaps an integer. All functions here exist for uint8_t too, even though they're trivial; they exist mostly for consistency.
//end_swap()
//  Calls the appropriate end_swapN depending on argument size.
//end_{le,be,nat}{,8,16,32,64}_to_{le,be,nat}() (only combinations with exactly one 'nat')
//  Byteswaps an integer or returns it unmodified, depending on the host's native endianness.
//  end_natN_to_le() is identical to end_leN_to_nat(). It's redundant, but it's the best name I could think of.
//  Like end_swap, the bit count defaults to input argument if absent.
//readu_{le,be}{8,16,32,64}()
//  Reads and returns an X-endian uintN_t from the given pointer. Accepts unaligned input.
//writeu_{le,be}{8,16,32,64}()
//  The inverse of readu; writes an X-endian uintN_t into the given pointer. Accepts unaligned input.
//pack_{le,be}{8,16,32,64}()
//  Like writeu, but instead of taking a pointer, it returns an sarray<uint8_t,N>.

// first line is for GCC 4.6 and Clang 3.2, second is for MSVC
#if (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || \
    defined(_M_IX86) || defined(_M_AMD64) || defined(_M_ARM) || defined(_M_ARM64) || defined(_M_ALPHA)
// per https://devblogs.microsoft.com/oldnewthing/20170814-00/?p=96806, a u16 on _M_ALPHA at 0x1357 has the low byte at 0x1357 -> LE
# define END_LITTLE 1
#elif (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) || \
    defined(_M_PPC)
# define END_BIG 1
#else
// MSVC can run on _M_IA64 too, but I don't know its endian
# error "unknown platform, can't determine endianness"
#endif

#ifndef END_LITTLE
# define END_LITTLE 0
#endif
#ifndef END_BIG
# define END_BIG 0
#endif

static inline uint8_t end_swap8(uint8_t n) { return n; }
#if defined(__GNUC__)
//This branch is useless, GCC detects the pattern and optimizes it.
//But MSVC doesn't, so I need the intrinsics. Might as well use both sets.
static inline uint16_t end_swap16(uint16_t n) { return __builtin_bswap16(n); }
static inline uint32_t end_swap32(uint32_t n) { return __builtin_bswap32(n); }
static inline uint64_t end_swap64(uint64_t n) { return __builtin_bswap64(n); }
#elif defined(_MSC_VER)
static inline uint16_t end_swap16(uint16_t n) { return _byteswap_ushort(n); }
static inline uint32_t end_swap32(uint32_t n) { return _byteswap_ulong(n); }
static inline uint64_t end_swap64(uint64_t n) { return _byteswap_uint64(n); }
#else
static inline uint16_t end_swap16(uint16_t n) { return n>>8 | n<<8; }
static inline uint32_t end_swap32(uint32_t n)
{
	n = n>>16 | n<<16;
	n = (n&0x00FF00FF)<<8 | (n&0xFF00FF00)>>8;
	return n;
}
static inline uint64_t end_swap64(uint64_t n)
{
	n = n>>32 | n<<32;
	n = (n&0x0000FFFF0000FFFF)<<16 | (n&0xFFFF0000FFFF0000)>>16;
	n = (n&0x00FF00FF00FF00FF)<<8  | (n&0xFF00FF00FF00FF00)>>8;
	return n;
}
#endif

static inline uint8_t  end_swap(uint8_t  n) { return end_swap8(n);  }
static inline uint16_t end_swap(uint16_t n) { return end_swap16(n); }
static inline uint32_t end_swap(uint32_t n) { return end_swap32(n); }
static inline uint64_t end_swap(uint64_t n) { return end_swap64(n); }
static inline int8_t  end_swap(int8_t  n) { return (int8_t )end_swap((uint8_t )n); }
static inline int16_t end_swap(int16_t n) { return (int16_t)end_swap((uint16_t)n); }
static inline int32_t end_swap(int32_t n) { return (int32_t)end_swap((uint32_t)n); }
static inline int64_t end_swap(int64_t n) { return (int64_t)end_swap((uint64_t)n); }

#if END_LITTLE
template<typename T> static inline T end_nat_to_le(T val) { return val; }
template<typename T> static inline T end_nat_to_be(T val) { return end_swap(val); }
template<typename T> static inline T end_le_to_nat(T val) { return val; }
template<typename T> static inline T end_be_to_nat(T val) { return end_swap(val); }
#elif END_BIG
template<typename T> static inline T end_nat_to_le(T val) { return end_swap(val); }
template<typename T> static inline T end_nat_to_be(T val) { return val; }
template<typename T> static inline T end_le_to_nat(T val) { return end_swap(val); }
template<typename T> static inline T end_be_to_nat(T val) { return val; }
#endif

static inline uint8_t  end_nat8_to_le( uint8_t  val) { return end_nat_to_le(val); }
static inline uint8_t  end_nat8_to_be( uint8_t  val) { return end_nat_to_be(val); }
static inline uint8_t  end_le8_to_nat( uint8_t  val) { return end_le_to_nat(val); }
static inline uint8_t  end_be8_to_nat( uint8_t  val) { return end_be_to_nat(val); }
static inline uint16_t end_nat16_to_le(uint16_t val) { return end_nat_to_le(val); }
static inline uint16_t end_nat16_to_be(uint16_t val) { return end_nat_to_be(val); }
static inline uint16_t end_le16_to_nat(uint16_t val) { return end_le_to_nat(val); }
static inline uint16_t end_be16_to_nat(uint16_t val) { return end_be_to_nat(val); }
static inline uint32_t end_nat32_to_le(uint32_t val) { return end_nat_to_le(val); }
static inline uint32_t end_nat32_to_be(uint32_t val) { return end_nat_to_be(val); }
static inline uint32_t end_le32_to_nat(uint32_t val) { return end_le_to_nat(val); }
static inline uint32_t end_be32_to_nat(uint32_t val) { return end_be_to_nat(val); }
static inline uint64_t end_nat64_to_le(uint64_t val) { return end_nat_to_le(val); }
static inline uint64_t end_nat64_to_be(uint64_t val) { return end_nat_to_be(val); }
static inline uint64_t end_le64_to_nat(uint64_t val) { return end_le_to_nat(val); }
static inline uint64_t end_be64_to_nat(uint64_t val) { return end_be_to_nat(val); }

inline uint8_t  readu_le8( const uint8_t* in) { uint8_t  ret; memcpy(&ret, in, sizeof(ret)); return end_le_to_nat(ret); }
inline uint8_t  readu_be8( const uint8_t* in) { uint8_t  ret; memcpy(&ret, in, sizeof(ret)); return end_be_to_nat(ret); }
inline uint16_t readu_le16(const uint8_t* in) { uint16_t ret; memcpy(&ret, in, sizeof(ret)); return end_le_to_nat(ret); }
inline uint16_t readu_be16(const uint8_t* in) { uint16_t ret; memcpy(&ret, in, sizeof(ret)); return end_be_to_nat(ret); }
inline uint32_t readu_le32(const uint8_t* in) { uint32_t ret; memcpy(&ret, in, sizeof(ret)); return end_le_to_nat(ret); }
inline uint32_t readu_be32(const uint8_t* in) { uint32_t ret; memcpy(&ret, in, sizeof(ret)); return end_be_to_nat(ret); }
inline uint64_t readu_le64(const uint8_t* in) { uint64_t ret; memcpy(&ret, in, sizeof(ret)); return end_le_to_nat(ret); }
inline uint64_t readu_be64(const uint8_t* in) { uint64_t ret; memcpy(&ret, in, sizeof(ret)); return end_be_to_nat(ret); }

inline void writeu_le8( uint8_t* target, uint8_t  n) { n = end_nat_to_le(n); memcpy(target, &n, 1); }
inline void writeu_be8( uint8_t* target, uint8_t  n) { n = end_nat_to_be(n); memcpy(target, &n, 1); }
inline void writeu_le16(uint8_t* target, uint16_t n) { n = end_nat_to_le(n); memcpy(target, &n, 2); }
inline void writeu_be16(uint8_t* target, uint16_t n) { n = end_nat_to_be(n); memcpy(target, &n, 2); }
inline void writeu_le32(uint8_t* target, uint32_t n) { n = end_nat_to_le(n); memcpy(target, &n, 4); }
inline void writeu_be32(uint8_t* target, uint32_t n) { n = end_nat_to_be(n); memcpy(target, &n, 4); }
inline void writeu_le64(uint8_t* target, uint64_t n) { n = end_nat_to_le(n); memcpy(target, &n, 8); }
inline void writeu_be64(uint8_t* target, uint64_t n) { n = end_nat_to_be(n); memcpy(target, &n, 8); }

inline sarray<uint8_t,1> pack_le8( uint8_t  n) { sarray<uint8_t,1> ret; writeu_le8( ret.ptr(), n); return ret; }
inline sarray<uint8_t,1> pack_be8( uint8_t  n) { sarray<uint8_t,1> ret; writeu_be8( ret.ptr(), n); return ret; }
inline sarray<uint8_t,2> pack_le16(uint16_t n) { sarray<uint8_t,2> ret; writeu_le16(ret.ptr(), n); return ret; }
inline sarray<uint8_t,2> pack_be16(uint16_t n) { sarray<uint8_t,2> ret; writeu_be16(ret.ptr(), n); return ret; }
inline sarray<uint8_t,4> pack_le32(uint32_t n) { sarray<uint8_t,4> ret; writeu_le32(ret.ptr(), n); return ret; }
inline sarray<uint8_t,4> pack_be32(uint32_t n) { sarray<uint8_t,4> ret; writeu_be32(ret.ptr(), n); return ret; }
inline sarray<uint8_t,8> pack_le64(uint64_t n) { sarray<uint8_t,8> ret; writeu_le64(ret.ptr(), n); return ret; }
inline sarray<uint8_t,8> pack_be64(uint64_t n) { sarray<uint8_t,8> ret; writeu_be64(ret.ptr(), n); return ret; }
