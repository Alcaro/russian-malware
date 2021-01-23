#pragma once
#include <stdint.h>

// __builtin_cpu_supports relies on a setup function that takes about 2KB of statically linked machine code,
//   mostly to make up its own feature bit order, and to calculate cpu model and other rarely useful information
// better reinvent it
#define __builtin_cpu_supports ("use runtime__SSE2__ instead"/0)

#ifdef _MSC_VER
# ifdef _M_I386
#  define __i386__ 1
# endif
# ifdef _M_AMD64
#  define __x86_64__ 1
# endif
# if _M_IX86_FP >= 1
#  define __SSE__ 1
# endif
# if _M_IX86_FP >= 2
#  define __SSE2__ 1
# endif
# ifdef __AVX__ // conveniently, msvc's AVX names match the GCC names
#  define __SSE3__ 1
#  define __SSSE3__ 1
#  define __SSE4_1__ 1
#  define __SSE4_2__ 1
#  define __POPCNT__ 1 // implied by SSE4.2; PCLMUL and AES are not implied by anything
# endif
// AVX2 implies nothing except itself and AVX
# ifdef __AVX512F__
#  define __FMA__ 1
# endif
# error check whether the above tests work, especially _M_IX86_FP on x64
#endif

extern uint32_t arlib_runtime_cpu[];

#if defined(__i386__) || defined(__x86_64__)

#ifdef __MMX__
# define runtime__MMX__ 1
#else
# define runtime__MMX__ (arlib_runtime_cpu[1] & 0x00800000)
#endif

#ifdef __SSE__
# define runtime__SSE__ 1
#else
# define runtime__SSE__ (arlib_runtime_cpu[1] & 0x02000000)
#endif

#ifdef __SSE2__
# define runtime__SSE2__ 1
#else
# define runtime__SSE2__ (arlib_runtime_cpu[1] & 0x04000000)
#endif

#ifdef __LZCNT__
# define runtime__LZCNT__ 1
#else
# define runtime__LZCNT__ (arlib_runtime_cpu[0] & 0x00000020)
#endif

#ifdef __POPCNT__
# define runtime__POPCNT__ 1
#else
# define runtime__POPCNT__ (arlib_runtime_cpu[0] & 0x00800000)
#endif

#ifdef __AES__
# define runtime__AES__ 1
#else
# define runtime__AES__ (arlib_runtime_cpu[0] & 0x02000000)
#endif

#ifdef __PCLMUL__
# define runtime__PCLMUL__ 1
#else
# define runtime__PCLMUL__ (arlib_runtime_cpu[0] & 0x00000002)
#endif

#ifdef __SSE3__
# define runtime__SSE3__ 1
#else
# define runtime__SSE3__ (arlib_runtime_cpu[0] & 0x00000001)
#endif

#ifdef __SSSE3__
# define runtime__SSSE3__ 1
#else
# define runtime__SSSE3__ (arlib_runtime_cpu[0] & 0x00000200)
#endif

#ifdef __SSE4_1__
# define runtime__SSE4_1__ 1
#else
# define runtime__SSE4_1__ (arlib_runtime_cpu[0] & 0x00080000)
#endif

#ifdef __SSE4_2__
# define runtime__SSE4_2__ 1
#else
# define runtime__SSE4_2__ (arlib_runtime_cpu[0] & 0x00100000)
#endif

#ifdef __AVX__
# define runtime__AVX__ 1
#else
# define runtime__AVX__ (arlib_runtime_cpu[0] & 0x10000000)
#endif

#ifdef __FMA__
# define runtime__FMA__ 1
#else
# define runtime__FMA__ (arlib_runtime_cpu[0] & 0x00001000)
#endif

#ifdef __RDRND__
# define runtime__RDRND__ 1
#else
# define runtime__RDRND__ (arlib_runtime_cpu[0] & 0x40000000)
#endif

#ifdef __F16C__
# define runtime__F16C__ 1
#else
# define runtime__F16C__ (arlib_runtime_cpu[0] & 0x20000000)
#endif

#ifdef __BMI__
# define runtime__BMI__ 1
#else
# define runtime__BMI__ (arlib_runtime_cpu[2] & 0x00000008)
#endif

#ifdef __FSGSBASE__
# define runtime__FSGSBASE__ 1
#else
# define runtime__FSGSBASE__ (arlib_runtime_cpu[2] & 0x00000001)
#endif

#ifdef __AVX2__
# define runtime__AVX2__ 1
#else
# define runtime__AVX2__ (arlib_runtime_cpu[2] & 0x00000020)
#endif

#ifdef __BMI2__
# define runtime__BMI2__ 1
#else
# define runtime__BMI2__ (arlib_runtime_cpu[2] & 0x00000100)
#endif

#ifdef __RDSEED__
# define runtime__RDSEED__ 1
#else
# define runtime__RDSEED__ (arlib_runtime_cpu[2] & 0x00040000)
#endif

#ifdef __ADX__
# define runtime__ADX__ 1
#else
# define runtime__ADX__ (arlib_runtime_cpu[2] & 0x00080000)
#endif

#ifdef __CLFLUSHOPT__
# define runtime__CLFLUSHOPT__ 1
#else
# define runtime__CLFLUSHOPT__ (arlib_runtime_cpu[2] & 0x00800000)
#endif

// AVX512 is not implemented, since I don't have a computer supporting that

#endif
