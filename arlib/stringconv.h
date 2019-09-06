#pragma once
#include "global.h"
#include "string.h"
#include <stdio.h>

inline string tostring(string s) { return s; }
inline string tostring(cstring s) { return s; }
//I'd use int123_t, but the set {8, 16, 32, 64} is smaller than {char, short, int, long, long long}, so one disappears
//if this one shows up (for example time_t = long on Windows), error
//printf has PRIi32, but the native ones are defined in terms of int/long
inline string tostring(  signed char val)     { char ret[32]; sprintf(ret, "%d",   val); return ret; } // the C++ standard says
inline string tostring(unsigned char val)     { char ret[32]; sprintf(ret, "%u",   val); return ret; } // (un)signed char/short are
//signless char isn't integral, so not here
inline string tostring(  signed short val)    { char ret[32]; sprintf(ret, "%d",   val); return ret; } // promoted to (un)signed int
inline string tostring(unsigned short val)    { char ret[32]; sprintf(ret, "%u",   val); return ret; } // in ellipsis
inline string tostring(  signed int val)      { char ret[32]; sprintf(ret, "%d",   val); return ret; }
inline string tostring(unsigned int val)      { char ret[32]; sprintf(ret, "%u",   val); return ret; }
inline string tostring(  signed long val)     { char ret[32]; sprintf(ret, "%ld",  val); return ret; }
inline string tostring(unsigned long val)     { char ret[32]; sprintf(ret, "%lu",  val); return ret; }
inline string tostringhex(unsigned char val)  { char ret[32]; sprintf(ret, "%X",   val); return ret; }
inline string tostringhex(unsigned short val) { char ret[32]; sprintf(ret, "%X",   val); return ret; }
inline string tostringhex(unsigned int val)   { char ret[32]; sprintf(ret, "%X",   val); return ret; }
inline string tostringhex(unsigned long val)  { char ret[32]; sprintf(ret, "%lX",  val); return ret; }
#ifdef _WIN32
# ifdef __GNUC__ // my GCC doesn't recognize I64, but msvcrt does
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wformat"
# endif
inline string tostring(  signed long long val)    { char ret[32]; sprintf(ret, "%I64d", val); return ret; }
inline string tostring(unsigned long long val)    { char ret[32]; sprintf(ret, "%I64u", val); return ret; }
inline string tostringhex(unsigned long long val) { char ret[32]; sprintf(ret, "%I64X", val); return ret; }
# ifdef __GNUC__
#  pragma GCC diagnostic pop
# endif
#else
inline string tostring(  signed long long val)    { char ret[32]; sprintf(ret, "%lld", val); return ret; }
inline string tostring(unsigned long long val)    { char ret[32]; sprintf(ret, "%llu", val); return ret; }
inline string tostringhex(unsigned long long val) { char ret[32]; sprintf(ret, "%llX", val); return ret; }
#endif
template<int n> inline string tostring(unsigned long val)    { char ret[32]; sprintf(ret, "%.*lu", n, val); return ret; }
template<int n> inline string tostringhex(unsigned long val) { char ret[32]; sprintf(ret, "%.*lX", n, val); return ret; }

string tostring(double val);
inline string tostring(float val) { return tostring((double)val); }
inline string tostring(bool val) { return val ? "true" : "false"; }
//inline string tostring(char val); // not sure if this one makes sense

inline string tostring(const char * s) { return s; } // only exists as tostring, fromstring would be a memory leak

template<typename T>
typename std::enable_if<
	sizeof((string)std::declval<T>()) != 0
	, string>::type
tostring(const T& val)
{
	return (string)val;
}


inline bool fromstring(cstring s, string& out) { out=s; return true; }
inline bool fromstring(cstring s, cstring& out) { out=s; return true; }
bool fromstring(cstring s,   signed char & out);
bool fromstring(cstring s, unsigned char & out);
bool fromstring(cstring s,   signed short & out);
bool fromstring(cstring s, unsigned short & out);
bool fromstring(cstring s,   signed int & out);
bool fromstring(cstring s, unsigned int & out);
bool fromstring(cstring s,   signed long & out);
bool fromstring(cstring s, unsigned long & out);
bool fromstring(cstring s,   signed long long & out);
bool fromstring(cstring s, unsigned long long & out);
bool fromstring(cstring s, float& out);
bool fromstring(cstring s, double& out);
bool fromstring(cstring s, bool& out);

bool fromstringhex(cstring s, unsigned char & out);
bool fromstringhex(cstring s, unsigned short & out);
bool fromstringhex(cstring s, unsigned int & out);
bool fromstringhex(cstring s, unsigned long & out);
bool fromstringhex(cstring s, unsigned long long & out);

string tostringhex(arrayview<byte> val);
bool fromstringhex(cstring s, arrayvieww<byte> val);
bool fromstringhex(cstring s, array<byte>& val);

#define ALLSTRINGABLE(x) \
	ALLNUMS(x) \
	x(string) \
	x(cstring) \
	x(bool)


//Same as fromstring, but can't report failure; in exchange, it also tries T::operator=(cstring).
template<typename T>
typename std::enable_if<
	std::is_assignable_v<T, string>
	>::type
try_fromstring(cstring s, T& out)
{
	out = s;
}

template<typename T>
typename std::enable_if<
	std::is_same<
		decltype(fromstring(std::declval<cstring>(), std::declval<T&>())),
		bool // it returning bool isn't necessary, but I couldn't find a std::can_call
	>::value && !std::is_assignable_v<T, string>>::type // extra is_assignable test so try_fromstring(cstring,string&) isn't ambiguous
try_fromstring(cstring s, T& out)
{
	fromstring(s, out);
}
