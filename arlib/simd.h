// anything SIMDed wants maximum performance, which requires using all instructions that exist and no others
// the integer SIMD instruction sets vary a lot between platforms, so any platform-neutral integer SIMD would just be a waste of time
// (it could make sense for trivial operations, but they're trivial to reimplement for each platform anyways - or autovectorize)
// the floating-point SIMD instruction sets vary a lot less between platforms, but offering only float is inconsistent

// however, flattening MSVC/GCC differences, and offering some debug tools, is a useful endeavor

#pragma once

#include "cpu.h"

#ifdef runtime__SSE2__
# include <emmintrin.h>

// https://godbolt.org/z/jbnsf4nEv
// upstream report seems to be https://gcc.gnu.org/bugzilla/show_bug.cgi?id=103897#c1 and/or #c2
# if __GNUC__ == 12
#  define _mm_undefined_si128 _mm_undefined_si128_alt
static inline __attribute__((target("sse2"), always_inline)) __m128i _mm_undefined_si128_alt()
{
	__m128i ret;
	__asm__ volatile("" : "=x"(ret));
	return ret;
}
# endif

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

// implemented using macros in misc.cpp
// I'd prefer passing __m128i directly, but that makes 32bit mingw (and possibly others) throw warnings about
// 'note: The ABI for passing parameters with 16-byte alignment has changed in GCC 4.6'
// that I can't figure out how to shut up
void debugd8(const __m128i& vals);  void debugu8(const __m128i& vals);  void debugx8(const __m128i& vals);
void debugd16(const __m128i& vals); void debugu16(const __m128i& vals); void debugx16(const __m128i& vals);
void debugd32(const __m128i& vals); void debugu32(const __m128i& vals); void debugx32(const __m128i& vals);
void debugd64(const __m128i& vals); void debugu64(const __m128i& vals); void debugx64(const __m128i& vals);

void debugd8(const char * prefix, const __m128i& vals); void debugd16(const char * prefix, const __m128i& vals); 
void debugu8(const char * prefix, const __m128i& vals); void debugu16(const char * prefix, const __m128i& vals); 
void debugx8(const char * prefix, const __m128i& vals); void debugx16(const char * prefix, const __m128i& vals); 
void debugd32(const char * prefix, const __m128i& vals); void debugd64(const char * prefix, const __m128i& vals); 
void debugu32(const char * prefix, const __m128i& vals); void debugu64(const char * prefix, const __m128i& vals); 
void debugx32(const char * prefix, const __m128i& vals); void debugx64(const char * prefix, const __m128i& vals); 

void debugf32(const __m128& vals);  void debugf32(const char * prefix, const __m128& vals);
void debugf64(const __m128d& vals); void debugf64(const char * prefix, const __m128d& vals); 
void debugc8(const __m128i& vals); void debugc8(const char * prefix, const __m128i& vals);
#endif
