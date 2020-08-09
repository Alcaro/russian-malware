// anything SIMDed wants maximum performance, which requires using all instructions that exist and no others
// the integer SIMD instruction sets vary a lot between platforms, so any platform-neutral integer SIMD would just be a waste of time
// (it could make sense for trivial operations, but they're trivial to reimplement for each platform anyways - or autovectorize)
// the floating-point SIMD instruction sets vary a lot less between platforms, but offering only float is inconsistent

// however, flattening MSVC/GCC differences, and offering some debug tools, is a useful endeavor

#if defined(_MSC_VER)
# error check whether that test works
// && defined(_M_AMD64)
# define __SSE2__
#endif

#if defined(__i386__) || defined(__x86_64__)
# define MAYBE_SSE2
#endif

#ifdef __SSE2__
# include <emmintrin.h>

# ifdef __i386__
// malloc is guaranteed to have 16-byte alignment on 64bit, but only 8 on 32bit
// I want to use the aligned instructions on 64bit, not ifdef it any further, not rewrite array<>, and not invent custom names,
//  so the best solution is defining the aligned ones to unaligned on 32bit
// (some of the intrinsics don't have unaligned equivalents, but those map to move+shufps, so I shouldn't use them anyways)
#  define _mm_load_pd      _mm_loadu_pd
#  define _mm_load_ps      _mm_loadu_ps
#  define _mm_loadr_pd      _mm_error
#  define _mm_loadr_ps      _mm_error
#  define _mm_load_si128   _mm_loadu_si128
#  define _mm_store1_pd     _mm_error
#  define _mm_store1_ps     _mm_error
#  define _mm_store_pd     _mm_storeu_pd
#  define _mm_store_pd1     _mm_error
#  define _mm_store_ps     _mm_storeu_ps
#  define _mm_store_ps1     _mm_error
#  define _mm_storer_pd     _mm_error
#  define _mm_storer_ps     _mm_error
#  define _mm_store_si128  _mm_storeu_si128
#  define _mm_stream_pd    _mm_storeu_pd
#  define _mm_stream_ps    _mm_storeu_ps
#  define _mm_stream_si128 _mm_storeu_si128
# endif

#include "stringconv.h"
inline void debug8(__m128i a){uint8_t n[16];memcpy(n,&a,16);puts(tostringhex(n));}
inline void debug16(__m128i a){int16_t n[8];memcpy(n,&a,16);printf("%d %d %d %d %d %d %d %d\n",n[0],n[1],n[2],n[3],n[4],n[5],n[6],n[7]);}
inline void debug32(__m128i a){int32_t n[4];memcpy(n,&a,16);printf("%d %d %d %d\n",n[0],n[1],n[2],n[3]);}
inline void debug32f(__m128 a){float n[4];memcpy(n,&a,16);printf("%f %f %f %f\n",n[0],n[1],n[2],n[3]);}
inline void debug8(const char*n,__m128i a){printf("%s ",n);debug8(a);}
inline void debug16(const char*n,__m128i a){printf("%s ",n);debug16(a);}
inline void debug32(const char*n,__m128i a){printf("%s ",n);debug32(a);}
inline void debug32f(const char*n,__m128 a){printf("%s ",n);debug32f(a);}
#endif
