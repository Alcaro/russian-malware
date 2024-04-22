#pragma once
#include <stdint.h>

// __builtin_cpu_supports relies on a setup function that takes about 2KB of statically linked machine code,
//   mostly to make up its own feature bit order, and to calculate cpu model and other rarely useful information
// it also optimizes poorly
// better reinvent it
#define __builtin_cpu_supports ("use runtime__SSE2__ instead"/0)

#ifdef _MSC_VER
# ifdef _M_I386
#  define __i386__ 1
#  if _M_IX86_FP >= 1
#   define __SSE__ 1
#  endif
#  if _M_IX86_FP >= 2
#   define __SSE2__ 1
#  endif
# endif
# ifdef _M_AMD64
#  define __x86_64__ 1
#  define __SSE__ 1
#  define __SSE2__ 1
# endif
// msvc has a /d2archSSE42 flag, but I don't know if it sets any #defines. And I don't care too much about undocumented flags anyways.
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
#endif

#if defined(__i386__) || defined(__x86_64__)

extern uint32_t arlib_cpuid_l1ecx;
extern uint32_t arlib_cpuid_l1edx;
extern uint32_t arlib_cpuid_l7ebx;

// sorted by approximate date of introduction (no real research done)

// ideally I'd have a way to return constant false for unsupported features if the binary isn't intended for distribution
// (for example -march=native and Gentoo), but I'm not sure how to implement that, and it's unlikely to be worthwhile

#ifdef __MMX__
# define runtime__MMX__ 1
#else
# define runtime__MMX__ (arlib_cpuid_l1edx & 0x00800000)
#endif

#ifdef __SSE__
# define runtime__SSE__ 1
#else
# define runtime__SSE__ (arlib_cpuid_l1edx & 0x02000000)
#endif

#ifdef __SSE2__
# define runtime__SSE2__ 1
#else
# define runtime__SSE2__ (arlib_cpuid_l1edx & 0x04000000)
#endif

#ifdef __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16
# define __CMPXCHG16B__ 1        // gcc doesn't have a name for this, it's named after the bit in cpuid.h
# define runtime__CMPXCHG16B__ 1 // there's no intrinsic for it either, only the gcc __sync builtins,
#else                            // which emit a runtime function call if not -mcx16
# define runtime__CMPXCHG16B__ (arlib_cpuid_l1ecx & 0x00002000)
#endif

#ifdef __POPCNT__
# define runtime__POPCNT__ 1
#else
# define runtime__POPCNT__ (arlib_cpuid_l1ecx & 0x00800000)
#endif

#ifdef __AES__
# define runtime__AES__ 1
#else
# define runtime__AES__ (arlib_cpuid_l1ecx & 0x02000000)
#endif

#ifdef __PCLMUL__
# define runtime__PCLMUL__ 1
#else
# define runtime__PCLMUL__ (arlib_cpuid_l1ecx & 0x00000002)
#endif

#ifdef __SSE3__
# define runtime__SSE3__ 1
#else
# define runtime__SSE3__ (arlib_cpuid_l1ecx & 0x00000001)
#endif

#ifdef __SSSE3__
# define runtime__SSSE3__ 1
#else
# define runtime__SSSE3__ (arlib_cpuid_l1ecx & 0x00000200)
#endif

#ifdef __SSE4_1__
# define runtime__SSE4_1__ 1
#else
# define runtime__SSE4_1__ (arlib_cpuid_l1ecx & 0x00080000)
#endif

#ifdef __SSE4_2__
# define runtime__SSE4_2__ 1
#else
# define runtime__SSE4_2__ (arlib_cpuid_l1ecx & 0x00100000)
#endif

#ifdef __AVX__
# define runtime__AVX__ 1
#else
# define runtime__AVX__ (arlib_cpuid_l1ecx & 0x10000000)
#endif

#ifdef __LZCNT__
# define runtime__LZCNT__ 1
#else
# define runtime__LZCNT__ (arlib_cpuid_l1ecx & 0x00000020)
#endif

#ifdef __FMA__
# define runtime__FMA__ 1
#else
# define runtime__FMA__ (arlib_cpuid_l1ecx & 0x00001000)
#endif

#ifdef __RDRND__
# define runtime__RDRND__ 1
#else
# define runtime__RDRND__ (arlib_cpuid_l1ecx & 0x40000000)
#endif

#ifdef __F16C__
# define runtime__F16C__ 1
#else
# define runtime__F16C__ (arlib_cpuid_l1ecx & 0x20000000)
#endif

#ifdef __BMI__
# define runtime__BMI__ 1
#else
# define runtime__BMI__ (arlib_cpuid_l7ebx & 0x00000008)
#endif

#ifdef __FSGSBASE__
# define runtime__FSGSBASE__ 1
#else
# define runtime__FSGSBASE__ (arlib_cpuid_l7ebx & 0x00000001)
#endif

#ifdef __AVX2__
# define runtime__AVX2__ 1
#else
# define runtime__AVX2__ (arlib_cpuid_l7ebx & 0x00000020)
#endif

#ifdef __BMI2__
# define runtime__BMI2__ 1
#else
# define runtime__BMI2__ (arlib_cpuid_l7ebx & 0x00000100)
#endif

#ifdef __RDSEED__
# define runtime__RDSEED__ 1
#else
# define runtime__RDSEED__ (arlib_cpuid_l7ebx & 0x00040000)
#endif

#ifdef __ADX__
# define runtime__ADX__ 1
#else
# define runtime__ADX__ (arlib_cpuid_l7ebx & 0x00080000)
#endif

#ifdef __CLFLUSHOPT__
# define runtime__CLFLUSHOPT__ 1
#else
# define runtime__CLFLUSHOPT__ (arlib_cpuid_l7ebx & 0x00800000)
#endif

// half of the cpuid bits refer to hardware features only the kernel should care about, so they're absent above
// the PKU/OSPKE features are usable in userspace, but only if enabled with syscalls, so better try it and see what kernel says
// supported on Linux >= 4.9 (dec 2016), syscall wrappers in glibc >= 2.27 (feb 2018); also FreeBSD >= 13.0 (apr 2021)
// as of aug 2023, Windows doesn't seem to support it
// (though Microsoft Research has done something with it https://www.microsoft.com/en-us/research/publication/libmpk-software-abstraction-for-intel-memory-protection-keys-intel-mpk/ )

// AVX512 lives mostly in cpuid leaves I'm not loading, and given that AVX10 has been announced, AVX512 is not future-proof
// I'm not touching 512 nor 10 until compiler devs reach an agreement on how to target 'x86-64-v4 but 256bit only', and similar

// in theory, the avx512 features don't depend on each other (except F, and half of functions depend on VL plus something else)
// in practice, it's a linear upgrade path; if it supports any of AVX512{F,CD,BW,DQ}, it supports all
// then each generation upgrade gives you one of {VBMI,IFMA}, VNNI, BF16, {VPOPCNTDQ,VBMI2,BITALG}, FP16, (none)
// (the VBMI2 step also adds VAES, GFNI and VPCLMULQDQ, outside of AVX512)
// https://cdrdv2.intel.com/v1/dl/getContent/784343 The Converged Vector ISA: Intel Advanced Vector Extensions 10 Technical Paper
// (oddly enough, the table does not contain AVX512VL, but since x86-64-v4 contains VL, I'm gonna guess it's part of the same group as F)
// AMD supports everything up to the VBMI2 level in Zen 4, and nothing before that; as of aug 2023, no AMD chip supports FP16

// x86-64: CMOV, CMPXCHG8B, FPU, FXSR, MMX, FXSR, SCE, SSE, SSE2
// x86-64-v2: (close to Nehalem) CMPXCHG16B, LAHF-SAHF, POPCNT, SSE3, SSE4.1, SSE4.2, SSSE3
// x86-64-v3: (close to Haswell) AVX, AVX2, BMI1, BMI2, F16C, FMA, LZCNT, MOVBE, XSAVE
// x86-64-v4: AVX512F, AVX512BW, AVX512CD, AVX512DQ, AVX512VL
// AES, RDRND, RDSEED, CLFLUSHOPT, ADX and PCLMUL are not in any of the above levels
//  first four are because cache and crypto stuff may turn out insecure and disabled, don't want to lose the entire level
//   https://gcc.gnu.org/pipermail/gcc/2020-July/233088.html
//  unclear why ADX and PCLMUL are absent
#endif
