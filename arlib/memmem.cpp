#include "global.h"
#include "simd.h"
#ifdef ARLIB_TEST // include these before #undef memmem
#include "test.h"
#include "os.h"
#endif

#if defined(_WIN32) || defined(runtime__SSE4_2__) || defined(ARLIB_TEST)
// Unlike musl and glibc, this program uses a rolling hash, not the twoway algorithm.
// Twoway is sometimes faster, sometimes slower; average is roughly the same, and rolling has more predictable performance.
// (Unless you repeatedly get hash collisions, but that requires a repetitive haystack and an adversarial needle.)

// I could write a SWAR program for small needles, but Arlib currently doesn't target any non-x86 platform, so no need.
// (And even without cmpestri, real SIMD instructions are almost always faster than SWAR, and exist on all modern platforms.)

#undef memmem

#ifdef runtime__SSE4_2__
#include <immintrin.h>

// Loads len (1 to 16) bytes from src to a __m128i. src doesn't need any particular alignment. The result's high bytes are undefined.
__attribute__((target("sse2")))
static forceinline __m128i load_sse2_small_highundef(const uint8_t * src, size_t len)
{
#ifndef ARLIB_OPT
	// Valgrind *really* hates the fast version.
	// To start with, reading out of bounds like this is all kinds of undefined behavior, and Valgrind rightfully complains.
	// Worse, it also doesn't correctly track validity through the _mm_cmpestri instruction;
	//  any undefined byte, including beyond the length, makes it claim the entire result is undefined,
	//  and this incorrectly-undefined value is then propagated across the entire program, throwing errors everywhere,
	//  so I can't just add a suppression to memmem.
	// Judging by the incomplete cmpestri handler, fixing it is most likely not going to get prioritized.
	// So I'll switch to a slower but safe version if unoptimized, and if optimized, just leave the errors.
	// Valgrind doesn't work very well with optimizations anyways.
	uint8_t tmp[16] __attribute__((aligned(16))) = {};
	memcpy(tmp, src, len);
	return _mm_load_si128((__m128i*)tmp);
#else
	// as disgusting as this optimization is, it ~doubles my score on some of the benchmarks
	
	// rule 1: do not touch a 4096-aligned page that the safe version does not
	//  (4096 can safely be hardcoded, SSE2 is x86 only and x86 page size ain't changing anytime soon)
	// rule 2: do not permit the compiler to prove any part of this program is UB
	// rule 3: as fast as possible
	
	// doing two tests is usually slower, but the first half is true 99.6% of the time,
	//  and simplifying the common case outweighs making the rare case more expensive
	// combined test: ((4080-src)&4095) < 4080+len
	// the obvious test: !(((src+15)^(src+len-1)) & 4096)
	if (LIKELY(((uintptr_t(src)>>4)+1)&(4095>>4)) || ((-(uintptr_t)src)&15) < len)
	{
		__asm__("" : "+r"(src)); // make sure compiler can't prove anything about the below load
		return _mm_loadu_si128((__m128i*)src);
	}
	else
	{
		// if an extended read would inappropriately hit the next page, copy it to the stack, then do an unaligned read
		// going via memory is ugly, but the better instruction (_mm_alignr_epi8) only exists with constant offset
		// machine-wise, it'd be safe to shrink tmp to 16 bytes, saving the higher 16 for return address or whatever,
		//  but that saves nothing in practice, and would give gcc wider license to deem it UB and optimize it out
		uint8_t tmp[32] __attribute__((aligned(16)));
		__asm__("" : "+r"(src), "=m"(tmp)); // reading uninitialized variables is UB too, confuse gcc some more
		_mm_store_si128((__m128i*)tmp, _mm_load_si128((__m128i*)(~15&(uintptr_t)src)));
		return _mm_loadu_si128((__m128i*)(tmp+(15&(uintptr_t)src)));
	}
#endif
}

// Works on needles of length 2 through 16, but it gets slower for big ones.
__attribute__((target("sse4.2")))
static const uint8_t * memmem_sse42(const uint8_t * haystack, size_t haystacklen, const uint8_t * needle, size_t needlelen)
{
	__m128i needle_sse = load_sse2_small_highundef(needle, needlelen);
	
	__m128i firstbyte = needle_sse;
	firstbyte = _mm_unpacklo_epi8(firstbyte, firstbyte); // duplicate bottom byte, ignore upper 14
	firstbyte = _mm_unpacklo_epi16(firstbyte, firstbyte); // duplicate bottom two bytes, ignore upper 12
	firstbyte = _mm_shuffle_epi32(firstbyte, _MM_SHUFFLE(0,0,0,0)); // and duplicate bottom four to all 16
	
	size_t step = 16+1-needlelen;
	
#define CMPSTR_FLAGS (_SIDD_UBYTE_OPS|_SIDD_CMP_EQUAL_ORDERED|_SIDD_LEAST_SIGNIFICANT)
	while (haystacklen >= 16)
	{
		__m128i haystack_sse = _mm_loadu_si128((__m128i*)haystack);
		if (_mm_movemask_epi8(_mm_cmpeq_epi8(haystack_sse, firstbyte)) == 0)
		{
			// look for first byte first; if not found, skip cmpestri, it's slow
			haystack += 16;
			haystacklen -= 16;
			continue;
		}
		
		int pos = _mm_cmpestri(needle_sse, needlelen, haystack_sse, 16, CMPSTR_FLAGS);
		
		if (pos < (int)step)
			return haystack + pos;
		
		haystack += step; // using 'pos' instead reduces iteration count, but using 'step' is faster. cmpestri latency is high
		haystacklen -= step;
	}
	
	// load_sse2_small_highundef fails for len=0, but haystacklen is known >= 1
	// the only way for it to be 0 is if step=16, which means needlelen=1, but that returns after memchr
	__m128i haystack_sse = load_sse2_small_highundef(haystack, haystacklen);
	int pos = _mm_cmpestri(needle_sse, needlelen, haystack_sse, haystacklen, CMPSTR_FLAGS);
	if (pos < (int)(haystacklen+1-needlelen))
		return haystack + pos;
#undef CMPSTR_FLAGS
	
	return NULL;
}
#endif


static const uint8_t * memmem_rollhash(const uint8_t * haystack, size_t haystacklen, const uint8_t * needle, size_t needlelen)
{
	const size_t hash_in = 283;
	// 283 is the prime closest to 256+31
	// >= 256 guarantees the hash is perfect for haystacklen <= sizeof(size_t)
	// 31 is the usual constant for string hashing; I don't know why it's chosen, but it's as good as any
	
	size_t needle_hash = 0;
	uint32_t bytes_used[256/32] = {};
	for (size_t n : range(needlelen))
	{
		uint8_t ch = needle[n];
		needle_hash = needle_hash*hash_in + ch;
		bytes_used[ch/32] |= 1u<<(ch&31);
	}
	
	const uint8_t * haystackend = haystack+haystacklen;
again:
	if (haystack+needlelen > haystackend)
		return NULL;
	
	size_t haystack_hash = 0;
	size_t hash_out = 1;
	for (size_t n = needlelen; n--; )
	{
		uint8_t ch = haystack[n];
		if (!(bytes_used[ch/32] & (1u<<(ch&31))))
		{
			haystack = haystack+n+1;
			goto again;
		}
		
		haystack_hash += ch * hash_out;
		hash_out *= hash_in;
	}
	
	size_t pos = 0;
	while (true)
	{
		// trying to SIMD this memcmp yields no measurable difference
		if (needle_hash == haystack_hash && !memcmp(haystack+pos, needle, needlelen))
			return haystack + pos;
		
		if (haystack+pos+needlelen >= haystackend)
			return NULL;
		
		haystack_hash *= hash_in;
		haystack_hash -= haystack[pos]*hash_out;
		haystack_hash += haystack[pos+needlelen];
		
		uint8_t ch = haystack[pos+needlelen];
		if (!(bytes_used[ch/32] & (1u<<(ch&31))))
		{
			haystack = haystack+pos+needlelen;
			goto again;
		}
		
		pos++;
	}
}

void* memmem_arlib(const void * haystack, size_t haystacklen, const void * needle, size_t needlelen)
{
	if (UNLIKELY(needlelen == 0)) return (void*)haystack;
	
	const void * hay_orig = haystack;
	haystack = memchr(haystack, *(uint8_t*)needle, haystacklen);
	if (!haystack) return NULL;
	if (needlelen == 1) return (void*)haystack;
	
	haystacklen -= (uint8_t*)haystack - (uint8_t*)hay_orig;
	if (UNLIKELY(needlelen > haystacklen)) return NULL;
	
#ifdef runtime__SSE4_2__
	// don't use this for needle length 16, both do one byte per iteration and _mm_cmpestri is slow
	if (needlelen <= 15 && runtime__SSE4_2__) return (void*)memmem_sse42((uint8_t*)haystack, haystacklen, (uint8_t*)needle, needlelen);
#endif
	
#if !defined(_WIN32) && !defined(ARLIB_TEST) // for long needles, use libc; we're roughly equally fast, and code reuse means smaller
	return memmem(haystack, haystacklen, needle, needlelen);
#endif
	
	return (void*)memmem_rollhash((uint8_t*)haystack, haystacklen, (uint8_t*)needle, needlelen);
}




#ifdef ARLIB_TEST
static bool do_bench = false;

typedef void*(*memmem_t)(const void * haystack, size_t haystacklen, const void * needle, size_t needlelen);

static void unpack(bytearray& out, const char * rule, size_t* star)
{
	out.reset();
	
	size_t start = 0;
	for (size_t n=0;rule[n];n++)
	{
		if (rule[n] == '(' || rule[n] == '*')
		{
			size_t outstart = out.size();
			out.resize(outstart + n-start);
			memcpy(out.ptr()+outstart, rule+start, n-start);
			start = n+1;
			if (rule[n] == '*' && star)
			{
				*star = out.size();
				star = NULL;
			}
		}
		if (rule[n] == ')')
		{
			size_t outstart = out.size();
			char* tmp;
			size_t repeats = strtol(rule+n+1, &tmp, 10);
			if (repeats > 1024 && !do_bench) repeats = repeats%1024 + 1024; // keep the correctness tests fast
			
			out.resize(outstart + (n-start)*repeats);
			for (size_t m : range(repeats))
				memcpy(out.ptr()+outstart+(n-start)*m, rule+start, n-start);
			
			n = tmp-rule-1;
			start = n+1;
		}
	}
	
	size_t outstart = out.size();
	out.resize(outstart + strlen(rule)-start);
	memcpy(out.ptr()+outstart, rule+start, strlen(rule)-start);
}
static uint64_t test1_raw(memmem_t memmem, void * haystack, size_t haystacklen, void * needle, size_t needlelen, void* expected)
{
	benchmark b(100000);
	while (b)
	{
		void* actual = memmem(haystack, haystacklen, needle, needlelen);
		assert_eq(actual, expected);
	}
	return b.per_second();
}

static void test1(const char * haystack, const char * needle)
{
	size_t match_pos = -1;
	bytearray full_hay;
	unpack(full_hay, haystack, &match_pos);
	bytearray full_needle;
	unpack(full_needle, needle, NULL);
	
	void* expected = (match_pos == (size_t)-1 ? NULL : full_hay.ptr()+match_pos);
	
	if (do_bench)
	{
		// Arlib and glibc are roughly tied on needle length 1 (we both just jump to memchr, the difference seems to just be noise),
		// but glibc doesn't use SSE4.2, so Arlib easily wins on most length tests (unless memchr takes most of the time for both)
		// for longer needles, whether Arlib's rolling hash or glibc's twoway wins seems mostly random;
		// glibc wins with large margin for (ab)100000 / ba(ab)32, but Arlib wins on
		// (ab)100000 / (ab)16ba(ab)16 and (ab)100000 / (ab)32ba
		// Arlib has same performance for those three, while glibc has a factor 20 difference
#ifndef _WIN32
		uint64_t perf_libc = test1_raw(memmem, full_hay.ptr(), full_hay.size(), full_needle.ptr(), full_needle.size(), expected);
#else
		uint64_t perf_libc = 0;
#endif
		uint64_t perf_arlib = test1_raw(memmem_arlib, full_hay.ptr(), full_hay.size(), full_needle.ptr(), full_needle.size(), expected);
		
		const char * winner = " | ";
		if ((double)perf_libc / perf_arlib > 1.5) winner = "*| ";
		if ((double)perf_arlib / perf_libc > 1.5) winner = " |*";
		printf("%8u%s%8u | %s / %s\n", (unsigned)perf_libc, winner, (unsigned)perf_arlib, haystack, needle);
	}
	else
	{
		void* actual = memmem_arlib(full_hay.ptr(), full_hay.size(), full_needle.ptr(), full_needle.size());
		assert_eq(actual, expected);
	}
}

test("memmem", "", "string")
{
	//do_bench = true;
	
	if (do_bench)
		puts("\nlibc     | Arlib");
	
	testcall(test1("*aaaaaaaa", "aaaaaaa"));
	
	testcall(test1("(b)100000", "a")); // at least one test must be nonexistent needle of length 1, and haystack length a multiple of 16
	testcall(test1("(b)100000*a", "a")); // memmem_sse42() fails if given that input, must ensure it isn't
	testcall(test1("(b)100000b*a", "a"));
	testcall(test1("(b)100000bb*a", "a"));
	testcall(test1("(b)100000bbb*a", "a"));
	testcall(test1("(b)100000bbbb*a", "a"));
	testcall(test1("(b)100000*abbbb", "a"));
	testcall(test1("(b)100000b*abbb", "a"));
	testcall(test1("(b)100000bb*abb", "a"));
	testcall(test1("(b)100000bbb*ab", "a"));
	testcall(test1("(b)100000bbbb*a", "a"));
	
	testcall(test1("(ab)100000a", "aa"));
	testcall(test1("(ab)100000*aa", "aa"));
	testcall(test1("(ab)100000b*aa", "aa"));
	testcall(test1("(ab)100000*aab", "aa"));
	testcall(test1("(ab)100000*aa(bb)31", "aa"));
	testcall(test1("(ab)100000*(aa)16", "aa"));
	testcall(test1("babababababababababababababab*aa", "aa"));
	testcall(test1("ababababababab*aa", "aa"));
	
	testcall(test1("a(bb)100000", "aa"));
	testcall(test1("a(bb)100000*aa", "aa"));
	testcall(test1("a(bb)100000b*aa", "aa"));
	testcall(test1("a(bb)100000*aab", "aa"));
	testcall(test1("a(bb)100000*aa(bb)31", "aa"));
	testcall(test1("a(bb)100000*(aa)16", "aa"));
	testcall(test1("babababababababababababababab*aa", "aa"));
	testcall(test1("ababababababab*aa", "aa"));
	testcall(test1("(bb)100000a", "aa"));
	testcall(test1("(bb)100000abbbbbbbbb", "aa"));
	
	testcall(test1("(ab)100000", "ba(ab)32"));
	testcall(test1("(ab)100000", "(ab)16ba(ab)16"));
	testcall(test1("(ab)100000", "(ab)32ba"));
	testcall(test1("b(a)100000", "(b)1000"));
	
	testcall(test1("(ab)1000000", "(ab)10000aa(ab)10000"));
	testcall(test1("(ab)10000ab(ab)10000", "(ab)10000aa(ab)10000"));
	testcall(test1("*(ab)10000aa(ab)10000", "(ab)10000aa(ab)10000"));
	testcall(test1("*(a)256", "(a)16"));
	
	testcall(test1("(cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
	                "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
	                "abababababababababababababababababababababababababababababababab"
	                "abababababababababababababababababababababababababababababababab)10000", "ababababbbabababa"));
}

#ifdef __SSE2__
test("load_sse2_small_highundef","","")
{
#ifndef _WIN32
	uint8_t* page;
	if (posix_memalign((void**)&page, 4096, 8192) != 0) abort();
	autofree<uint8_t> holder = page;
	
	for (int i=0;i<8192;i++)
		page[i] = i&31;
	
	auto test1 = [&](int start, int size) {
		assert_gte(start, size);
		uint8_t tmp[16];
		_mm_storeu_si128((__m128i*)tmp, load_sse2_small_highundef(page+8192-start, size));
		
		bytesr expected(page+8192-start, size);
		bytesr actual(tmp, size);
		assert_eq(actual, expected);
	};
	
	test1(13, 13);
	test1(16, 16);
	test1(16, 8);
	test1(16, 11);
	test1(27, 16);
	test1(4100, 16);
#endif
	
	// used to verify the condition in load_sse2_small_highundef
	/*
	int n=0;
	for (int src=0;src<64;src++)
	for (int len=1;len<=16;len++)
	{
		bool safe1 = LIKELY((63&(uintptr_t)src) <= 48) || ((48-(uintptr_t)src)&63) < 48+(uintptr_t)len;
		bool safe2 = LIKELY((63&(uintptr_t)src) <= 48) || ((-src)&15) < len;
		printf("%c", "uUSs"[safe1+safe1+safe2]);
		if (len==16) printf(" src=%d\n",src);
		if (safe1 != safe2) n++;
	}
	printf("%d failures\n",n);
	*/
}
#endif
#endif
#endif
