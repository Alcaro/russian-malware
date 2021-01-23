#pragma once
#include "global.h"
#include "array.h"
#include <stdint.h>

//This file defines:
//Macros END_LITTLE and END_BIG
//  Defined to 1 if that's the current machine's endian, otherwise 0.
//readu_{le,be}{8,16,32,64}()
//  Reads and returns an X-endian uintN_t from the given pointer. Accepts unaligned input.
//  The 8bit one is trivial, it exists mostly for consistency.
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

inline uint8_t  readu_le8( const uint8_t* in) { return *in; }
inline uint8_t  readu_be8( const uint8_t* in) { return *in; }
inline uint16_t readu_le16(const uint8_t* in) { uint16_t ret; memcpy(&ret, in, sizeof(ret)); return END_BIG ? end_swap16(ret) : ret; }
inline uint16_t readu_be16(const uint8_t* in) { uint16_t ret; memcpy(&ret, in, sizeof(ret)); return END_BIG ? ret : end_swap16(ret); }
inline uint32_t readu_le32(const uint8_t* in) { uint32_t ret; memcpy(&ret, in, sizeof(ret)); return END_BIG ? end_swap32(ret) : ret; }
inline uint32_t readu_be32(const uint8_t* in) { uint32_t ret; memcpy(&ret, in, sizeof(ret)); return END_BIG ? ret : end_swap32(ret); }
inline uint64_t readu_le64(const uint8_t* in) { uint64_t ret; memcpy(&ret, in, sizeof(ret)); return END_BIG ? end_swap64(ret) : ret; }
inline uint64_t readu_be64(const uint8_t* in) { uint64_t ret; memcpy(&ret, in, sizeof(ret)); return END_BIG ? ret : end_swap64(ret); }

inline void writeu_le8( uint8_t* target, uint8_t  n) { *target = n; }
inline void writeu_be8( uint8_t* target, uint8_t  n) { *target = n; }
inline void writeu_le16(uint8_t* target, uint16_t n) { n = END_BIG ? end_swap16(n) : n; memcpy(target, &n, 2); }
inline void writeu_be16(uint8_t* target, uint16_t n) { n = END_BIG ? n : end_swap16(n); memcpy(target, &n, 2); }
inline void writeu_le32(uint8_t* target, uint32_t n) { n = END_BIG ? end_swap32(n) : n; memcpy(target, &n, 4); }
inline void writeu_be32(uint8_t* target, uint32_t n) { n = END_BIG ? n : end_swap32(n); memcpy(target, &n, 4); }
inline void writeu_le64(uint8_t* target, uint64_t n) { n = END_BIG ? end_swap64(n) : n; memcpy(target, &n, 8); }
inline void writeu_be64(uint8_t* target, uint64_t n) { n = END_BIG ? n : end_swap64(n); memcpy(target, &n, 8); }

inline sarray<uint8_t,1> pack_le8( uint8_t  n) { sarray<uint8_t,1> ret; writeu_le8( ret.ptr(), n); return ret; }
inline sarray<uint8_t,1> pack_be8( uint8_t  n) { sarray<uint8_t,1> ret; writeu_be8( ret.ptr(), n); return ret; }
inline sarray<uint8_t,2> pack_le16(uint16_t n) { sarray<uint8_t,2> ret; writeu_le16(ret.ptr(), n); return ret; }
inline sarray<uint8_t,2> pack_be16(uint16_t n) { sarray<uint8_t,2> ret; writeu_be16(ret.ptr(), n); return ret; }
inline sarray<uint8_t,4> pack_le32(uint32_t n) { sarray<uint8_t,4> ret; writeu_le32(ret.ptr(), n); return ret; }
inline sarray<uint8_t,4> pack_be32(uint32_t n) { sarray<uint8_t,4> ret; writeu_be32(ret.ptr(), n); return ret; }
inline sarray<uint8_t,8> pack_le64(uint64_t n) { sarray<uint8_t,8> ret; writeu_le64(ret.ptr(), n); return ret; }
inline sarray<uint8_t,8> pack_be64(uint64_t n) { sarray<uint8_t,8> ret; writeu_be64(ret.ptr(), n); return ret; }

#undef end_swap16 // delete these, so callers are forced to use the functions instead
#undef end_swap32
#undef end_swap64
