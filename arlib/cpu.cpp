#include "global.h"
#include <cpuid.h>

uint32_t arlib_runtime_cpu[3] = {};

oninit_static_early()
{
	uint32_t cpuid_max = __get_cpuid_max(0, NULL);
#ifndef __SSE__ // I'll just assume that everything that supports SSE also supports CPUID and everything needed for SSE detection.
	if (cpuid_max < 1) return;
#endif
	
	uint32_t dummy1, dummy2;
	uint32_t l1ecx, l1edx;
	
	__cpuid(1, dummy1, dummy2, l1ecx, l1edx);
	
	uint32_t osxsave = 0;
#ifndef __AVX__
	if (l1ecx & 0x08000000)
#endif
	{
		// assembly is ugly, but the _xgetbv intrinsic needs target("xsave"), and I'd rather not add a separate function for that
		// intel intrinsics guide says there should be a bunch of named constants around there, but gcc doesn't seem to implement them
		__asm__("xgetbv" : "=a"(osxsave), "=d"(dummy1) : "c"(0));
	}
	
	// there are i386 OSes that don't support SSE, but I can't find how to detect that,
	//  so I'll just assume they all support SSE in this year.
	
	// AVX is supported on everything Arlib supports (Linux since 2.6.30 / june 2009, Windows since 7 SP1),
	//  but both support disabling it, seemingly mostly as workaround for increasingly obscure bugs.
	// I don't know if anyone still uses the AVX disable switch in this year, but no point finding out.
#ifndef __AVX__
	if (0x04&~osxsave) // Intel recommends also checking &2, SSE state, but that one is always true in this year.
		l1ecx &= ~(bit_AVX|bit_FMA);
#endif
	
	arlib_runtime_cpu[0] = l1ecx;
	arlib_runtime_cpu[1] = l1edx;
	
#ifndef __AVX2__
	if (cpuid_max >= 7)
#endif
	{
		uint32_t l7ebx, l7ecx, l7edx;
		__cpuid_count(7, 0, dummy1, l7ebx, l7ecx, l7edx);
		
#ifndef __AVX__
		if (0x04&~osxsave)
			l7ebx &= ~bit_AVX2;
#endif
		
		arlib_runtime_cpu[2] = l7ebx;
		//arlib_runtime_cpu[3] = l7ecx; // unused, they contain a few AVX512 subfeatures
		//arlib_runtime_cpu[4] = l7edx;
		
#ifndef __AVX512F__
		//if (0xE4&~osxsave) // AVX, opmask, ZMM, ZMM high 16 (why can those even be enabled separately)
		//	// I don't think opm/zmm/zmmhi bits differ on any plausible OS, but better not take pointless risks
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
	// these tests don't correctly test anything statically known true, like SSE2
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
