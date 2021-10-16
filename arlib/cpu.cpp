#include "global.h"

#ifdef __GNUC__
#include <cpuid.h>
#define cpuid __cpuid
#define cpuid_count __cpuid_count

static inline uint32_t get_cpuid_max(uint32_t ext, uint32_t* sig)
{
#if defined(__x86_64__) || !defined(__i686__) // __get_cpuid_max unnecessarily checks EFLAGS on __i686__ (correctly omitted on __x86_64__)
	return __get_cpuid_max(ext, sig);
#else
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	
	__cpuid(ext, eax, ebx, ecx, edx);
	if (sig) *sig = ebx;
	return eax;
#endif
}

// assembly is ugly, but the _xgetbv intrinsic needs target("xsave"), and a separate function for that would inhibit optimizations
#ifndef __AVX__
// extra volatile to ensure it's not optimized to unconditional xgetbv plus conditional move (not sure if gcc does that, but it could)
// similar guards are unnecessary for cpuid; cpuid(eax = out of range) returns garbage values, not SIGILL (if cpuid exists at all)
// it's unclear to me if gcc can hoist asms like that (it obviously can't hoist a division whose condition checks if divisor is zero),
//  but it may be possible if the asm has no non-constant inputs; better not take any risks
# define xgetbv(feat, a, d) __asm__ volatile("xgetbv" : "=a"(a), "=d"(d) : "c"(feat))
#else
# define xgetbv(feat, a, d) __asm__         ("xgetbv" : "=a"(a), "=d"(d) : "c"(feat))
#endif

#endif

#ifdef _MSC_VER
#include <intrin.h>
#include <immintrin.h>

static inline void cpuid(uint32_t level, uint32_t& eax, uint32_t& ebx, uint32_t& ecx, uint32_t& edx)
{
	int info[4];
	__cpuid(info, level);
	eax = info[0];
	ebx = info[1];
	ecx = info[2];
	edx = info[3];
}

static inline void cpuid_count(uint32_t level, uint32_t count, uint32_t& eax, uint32_t& ebx, uint32_t& ecx, uint32_t& edx)
{
	int info[4];
	__cpuidex(info, level, count);
	eax = info[0];
	ebx = info[1];
	ecx = info[2];
	edx = info[3];
}

static inline uint32_t get_cpuid_max(uint32_t ext, uint32_t* sig)
{
#ifdef _M_IX86
	if (!__readeflags() & 0x00200000) return 0; // check if cpuid exists (always does on 64bit)
#endif
	
	int info[4];
	__cpuid(info, ext);
	if (sig) *sig = info[1];
	return info[0];
}

// not inline, I don't know how aggressively msvc optimizes
static void xgetbv(uint32_t feat, uint32_t& a, uint32_t& d)
{
	uint64_t ret = _xgetbv(feat);
	a = ret;
	d = ret>>32;
}

#define bit_OSXSAVE (1 << 27) // l1ecx
#define bit_AVX     (1 << 28) // l1ecx
#define bit_FMA     (1 << 12) // l1ecx
#define bit_AVX2    (1 << 5)  // l7ebx
#endif

#ifndef _XCR_XFEATURE_ENABLED_MASK
#define _XCR_XFEATURE_ENABLED_MASK 0 // intel docs say this should exist, but I'm using an old gcc where it's gone
#endif

// unclear who invented these names, or which header they should be in
#define XSTATE_FP     0x1
#define XSTATE_SSE    0x2
#define XSTATE_YMM    0x4
#define XSTATE_OPMASK 0x20
#define XSTATE_ZMM    0x40
#define XSTATE_HI_ZMM 0x80

uint32_t arlib_cpuid_l1ecx = 0;
uint32_t arlib_cpuid_l1edx = 0;
uint32_t arlib_cpuid_l7ebx = 0;

oninit_static_early()
{
	uint32_t cpuid_max = get_cpuid_max(0, NULL);
#if !defined(__i686__) && !defined(__x86_64__)
	if (cpuid_max < 1) return;
#endif
	
	uint32_t dummy1, dummy2;
	uint32_t l1ecx, l1edx;
	
	cpuid(1, dummy1, dummy2, l1ecx, l1edx);
	
	uint32_t osxsave = 0;
#ifndef __AVX__
	if (l1ecx & bit_OSXSAVE)
#endif
		xgetbv(_XCR_XFEATURE_ENABLED_MASK, osxsave, dummy1);
	
	// AVX is supported on Linux since 2.6.30 / june 2009, Windows since 7 SP1,
	//  but both support disabling it, seemingly mostly as workaround for increasingly obscure bugs.
	// I don't know if anyone still uses the AVX disable switch in this year, but no point finding out.
	// But I don't care about disabling SSE (osxsave&2), that one is always true in this year.
#ifndef __AVX__
	if (XSTATE_YMM&~osxsave)
		l1ecx &= ~(bit_AVX|bit_FMA);
#endif
	
	arlib_cpuid_l1ecx = l1ecx;
	arlib_cpuid_l1edx = l1edx;
	
#ifndef __AVX2__
	if (cpuid_max >= 7)
#endif
	{
		uint32_t l7ebx, l7ecx, l7edx;
		cpuid_count(7, 0, dummy1, l7ebx, l7ecx, l7edx);
		
#ifndef __AVX__
		if (XSTATE_YMM&~osxsave)
			l7ebx &= ~bit_AVX2;
#endif
		
		arlib_cpuid_l7ebx = l7ebx;
		// l7ecx and l7edx contain a few AVX512 subfeatures, currently unused
		
#ifndef __AVX512F__
		//if (0xE4&~osxsave) // AVX, opmask, ZMM, ZMM high 16 (why can those even be enabled separately)
		//	// I don't think opm/zmm/zmmhi bits differ on any plausible OS, but better not take pointless risks
		
		//on mac/darwin, avx512 support is initially disabled in xgetbv, but gets enabled if any avx512 instruction is executed
		//this is an optimization so it only needs to save 832 bytes of regs on task switch, not 2688
		//https://github.com/apple/darwin-xnu/blob/0a798f6738bc1db01281fc08ae024145e84df927/osfmk/i386/fpu.c#L176
		//no point checking for that, x86 mac is on its way out anyways. However, something similar may show up on future Linux.
#endif
	}
}

#include "test.h"
test("cpu support", "", "")
{
	uint32_t arlib_cpuid_l1ecx = 0; (void)arlib_cpuid_l1ecx;
	uint32_t arlib_cpuid_l1edx = 0; (void)arlib_cpuid_l1edx;
	uint32_t arlib_cpuid_l7ebx = 0; (void)arlib_cpuid_l7ebx;
	uint32_t arlib_cpuid_l7ecx = 0; (void)arlib_cpuid_l7ecx;
	uint32_t arlib_cpuid_l7edx = 0; (void)arlib_cpuid_l7edx;
#define test1(reg, name) { arlib_cpuid_##reg = bit_##name; assert(runtime__##name##__); arlib_cpuid_##reg = 0; }
	// things statically known true, like SSE2 on 64bit, aren't properly tested here
	test1(l1ecx, AES);
	test1(l1ecx, AVX);
	test1(l1ecx, CMPXCHG16B);
	test1(l1ecx, F16C);
	test1(l1ecx, FMA);
	test1(l1ecx, LZCNT);
	test1(l1ecx, PCLMUL);
	test1(l1ecx, POPCNT);
	test1(l1ecx, RDRND);
	test1(l1ecx, SSE3);
	test1(l1ecx, SSE4_1);
	test1(l1ecx, SSE4_2);
	test1(l1ecx, SSSE3);
	test1(l1edx, MMX);
	test1(l1edx, SSE);
	test1(l1edx, SSE2);
	test1(l7ebx, ADX);
	test1(l7ebx, AVX2);
	test1(l7ebx, BMI);
	test1(l7ebx, BMI2);
	test1(l7ebx, CLFLUSHOPT);
	test1(l7ebx, FSGSBASE);
	test1(l7ebx, RDSEED);
#undef test1
}
