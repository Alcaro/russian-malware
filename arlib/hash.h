#pragma once
#include "global.h"

//Hash values are guaranteed stable within the process, but nothing else. Do not persist them outside the process.
//They are allowed to change along with the build target, Arlib version, build time, kernel version, etc.
//Don't rely on them for any security-related purpose either.

template<typename T>
typename std::enable_if_t<std::is_integral_v<T>, size_t> hash(T val)
{
	return val;
}
template<typename T>
typename std::enable_if_t<std::is_class_v<T>, size_t> hash(const T& val)
{
	return val.hash();
}
size_t hash(const uint8_t * val, size_t n);
static inline size_t hash(const char * val)
{
	return hash((uint8_t*)val, strlen(val));
}


//implementation from https://stackoverflow.com/a/263416
static inline size_t hashall() { return 2166136261; }
template<typename T, typename... Tnext> static inline size_t hashall(T first, Tnext... next)
{
	size_t tail = hash(first);
	size_t heads = hashall(next...);
	return (heads ^ tail) * 16777619;
}


//these two are reversible, but I have no usecase for a reverser so I didn't implement one
inline uint32_t hash_shuffle(uint32_t val)
{
	//https://code.google.com/p/smhasher/wiki/MurmurHash3
	val ^= val >> 16;
	val *= 0x85ebca6b;
	val ^= val >> 13;
	val *= 0xc2b2ae35;
	val ^= val >> 16;
	return val;
}

inline uint64_t hash_shuffle(uint64_t val)
{
	//http://zimbry.blogspot.se/2011/09/better-bit-mixing-improving-on.html Mix13
	val ^= val >> 30;
	val *= 0xbf58476d1ce4e5b9;
	val ^= val >> 27;
	val *= 0x94d049bb133111eb;
	val ^= val >> 31;
	return val;
}
