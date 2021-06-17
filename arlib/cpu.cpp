#include "global.h"

#ifdef __GNUC__
#include <cpuid.h>
#define cpuid __cpuid
#define cpuid_count __cpuid_count
#define get_cpuid_max __get_cpuid_max

// assembly is ugly, but the _xgetbv intrinsic needs target("xsave"), and I'd rather not add a separate function for that
// volatile to ensure it's not hoisted out of anywhere
#define xgetbv(feat, a, d) __asm__ volatile("xgetbv" : "=a"(a), "=d"(d) : "c"(feat))

#endif

#ifdef _MSC_VER
#include <intrin.h>
#include <immintrin.h>

static void cpuid(uint32_t level, uint32_t& eax, uint32_t& ebx, uint32_t& ecx, uint32_t& edx)
{
	int info[4];
	__cpuid(info, level);
	eax = info[0];
	ebx = info[1];
	ecx = info[2];
	edx = info[3];
}

static void cpuid_count(uint32_t level, uint32_t count, uint32_t& eax, uint32_t& ebx, uint32_t& ecx, uint32_t& edx)
{
	int info[4];
	__cpuidex(info, level, count);
	eax = info[0];
	ebx = info[1];
	ecx = info[2];
	edx = info[3];
}

static void xgetbv(uint32_t feat, uint32_t& a, uint32_t& d)
{
	uint64_t ret = _xgetbv(feat);
	a = ret;
	d = ret>>32;
}

static uint32_t get_cpuid_max(uint32_t ext, uint32_t* sig)
{
#ifdef _M_IX86
	if (!__readeflags() & 0x00200000) return 0; // check if cpuid exists (always does on 64bit)
#endif
	
	int info[4];
	__cpuid(info, ext);
	if (sig) *sig = info[1];
	return info[0];
}

#define bit_OSXSAVE (1 << 27)
#define bit_AVX     (1 << 28)
#define bit_FMA     (1 << 12)
#define bit_AVX2    (1 << 5)
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

uint32_t arlib_runtime_cpu[3] = {0,0,0};

oninit_static_early()
{
	uint32_t cpuid_max = get_cpuid_max(0, NULL);
#ifndef __SSE__ // I'll just assume that everything that supports SSE also supports CPUID and everything needed for SSE detection.
	if (cpuid_max < 1) return; // comments in cpuid.h say x86_64 always has cpuid
#endif
	
	uint32_t dummy1, dummy2;
	uint32_t l1ecx, l1edx;
	
	cpuid(1, dummy1, dummy2, l1ecx, l1edx);
	
	uint32_t osxsave = 0;
#ifndef __AVX__
	if (l1ecx & bit_OSXSAVE)
#endif
	{
		xgetbv(_XCR_XFEATURE_ENABLED_MASK, osxsave, dummy1);
	}
	
	// AVX is supported on Linux since 2.6.30 / june 2009, Windows since 7 SP1,
	//  but both support disabling it, seemingly mostly as workaround for increasingly obscure bugs.
	// I don't know if anyone still uses the AVX disable switch in this year, but no point finding out.
	// But I don't care about disabling SSE (osxsave&2), that one is always true in this year.
#ifndef __AVX__
	if (XSTATE_YMM&~osxsave)
		l1ecx &= ~(bit_AVX|bit_FMA);
#endif
	
	arlib_runtime_cpu[0] = l1ecx;
	arlib_runtime_cpu[1] = l1edx;
	
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
		
		arlib_runtime_cpu[2] = l7ebx;
		//arlib_runtime_cpu[3] = l7ecx; // unused, they contain a few AVX512 subfeatures
		//arlib_runtime_cpu[4] = l7edx;
		
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
	uint32_t arlib_runtime_cpu[5] = {}; // shadow the global
#define l1ecx 0
#define l1edx 1
#define l7ebx 2
#define l7ecx 3
#define l7edx 4
#define test1(reg, name) { arlib_runtime_cpu[reg] = bit_##name; assert(runtime__##name##__); arlib_runtime_cpu[reg] = 0; }
	// things statically known true, like SSE2, aren't properly tested here
	test1(l7ebx, ADX);
	test1(l1ecx, AES);
	test1(l1ecx, AVX);
	test1(l7ebx, AVX2);
	test1(l7ebx, BMI);
	test1(l7ebx, BMI2);
	test1(l7ebx, CLFLUSHOPT);
	test1(l1ecx, F16C);
	test1(l1ecx, FMA);
	test1(l7ebx, FSGSBASE);
	test1(l1ecx, LZCNT);
	test1(l1edx, MMX);
	test1(l1ecx, PCLMUL);
	test1(l1ecx, POPCNT);
	test1(l1ecx, RDRND);
	test1(l7ebx, RDSEED);
	test1(l1edx, SSE);
	test1(l1edx, SSE2);
	test1(l1ecx, SSE3);
	test1(l1ecx, SSE4_1);
	test1(l1ecx, SSE4_2);
	test1(l1ecx, SSSE3);
#undef test1
#undef l1ecx
#undef l1edx
#undef l7ebx
#undef l7ecx
#undef l7edx
}
