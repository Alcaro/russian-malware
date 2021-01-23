#pragma once
#include "global.h"
#include "string.h"
#include "set.h"
#include <stdio.h>

inline string tostring(string s) { return s; }
inline string tostring(cstring s) { return s; }

// as much as I prefer int123_t, the set {8, 16, 32, 64} is smaller than {char, short, int, long, long long}
// if any typedef of the missing one shows up (for example time_t = long on Windows), ambiguity errors
string tostring(  signed long long val);
string tostring(unsigned long long val);
string tostringhex(unsigned long long val, size_t mindigits = 0);
#if SIZE_MAX < ULLONG_MAX
string tostring(  signed long val); // long is 32 bits on every OS where size_t < long long,
string tostring(unsigned long val); // so long's absurd definition of 'uhhh pointer size except on windows' won't cause trouble here
string tostringhex(unsigned long val, size_t mindigits = 0);
#else
inline string tostring(  signed long val) { return tostring((signed long long)val); }
inline string tostring(unsigned long val) { return tostring((unsigned long long)val); }
inline string tostringhex(unsigned long val, size_t mindigits = 0) { return tostringhex((unsigned long long)val, mindigits); }
#endif
// signless char isn't integral, so not here (or anywhere else, I don't know what it should be)
inline string tostring(  signed char val)  { return tostring((signed long)val); }
inline string tostring(unsigned char val)  { return tostring((unsigned long)val); }
inline string tostring(  signed short val) { return tostring((signed long)val); }
inline string tostring(unsigned short val) { return tostring((unsigned long)val); }
inline string tostring(  signed int val)   { return tostring((signed long)val); }
inline string tostring(unsigned int val)   { return tostring((unsigned long)val); }
inline string tostringhex(unsigned char val, size_t mindigits = 0)  { return tostringhex((unsigned long)val, mindigits); }
inline string tostringhex(unsigned short val, size_t mindigits = 0) { return tostringhex((unsigned long)val, mindigits); }
inline string tostringhex(unsigned int val, size_t mindigits = 0)   { return tostringhex((unsigned long)val, mindigits); }
template<size_t mindigits> inline string tostringhex(unsigned val) { return tostringhex(val, mindigits); }

string tostring(unsigned val, size_t mindigits);
template<size_t mindigits> inline string tostring(unsigned val) { return tostring(val, mindigits); }

string tostring(double val);
string tostring(float val);
inline string tostring(bool val) { return val ? "true" : "false"; }

inline string tostring(const char * s) { return s; } // only exists as tostring, fromstring would be a memory leak

template<typename T>
typename std::enable_if_t<sizeof((string)std::declval<T>()) != 0, string>
tostring(const T& val)
{
	return (string)val;
}


// 'out' is guaranteed to be initialized to something even on failure. However, it's not guaranteed to be anything useful.
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
void tostring(bytesr) = delete;
void tostring(bytesw) = delete;
void tostring(bytearray) = delete;
template<typename T, size_t n> void tostring(const T(&)[n]) = delete;

bool fromstringhex(cstring s, unsigned char & out);
bool fromstringhex(cstring s, unsigned short & out);
bool fromstringhex(cstring s, unsigned int & out);
bool fromstringhex(cstring s, unsigned long & out);
bool fromstringhex(cstring s, unsigned long long & out);

string tostringhex(arrayview<uint8_t> val);
bool fromstringhex(cstring s, arrayvieww<uint8_t> val);
bool fromstringhex(cstring s, array<uint8_t>& val);


//Same as fromstring, but can't report failure. May be easier to use.
template<typename T> T try_fromstring(cstring s)
{
	T tmp;
	fromstring(s, tmp);
	return tmp;
}
template<typename T> T try_fromstring(cstring s, T&& def)
{
	T tmp;
	if (fromstring(s, tmp)) return tmp;
	else return def;
}


template<typename T> string tostring_dbg(const T& item) { return tostring(item); }

template<typename T>
string tostring_dbg(const arrayview<T>& item)
{
	return "[" + item.join((string)",", [](const T& i){ return tostring_dbg(i); }) + "]";
}
template<typename T> string tostring_dbg(const arrayvieww<T>& item) { return tostring_dbg((arrayview<T>)item); }
template<typename T> string tostring_dbg(const array<T>& item) { return tostring_dbg((arrayview<T>)item); }
template<typename T, size_t size> string tostring_dbg(T(&item)[size]) { return tostring_dbg(arrayview<T>(item)); }
template<size_t size> string tostring_dbg(const char(&item)[size]) { return item; }

template<typename Tkey, typename Tvalue>
string tostring_dbg(const map<Tkey,Tvalue>& item)
{
	return "{"+
		item
			.select([](const typename map<Tkey,Tvalue>::node& n){ return tostring_dbg(n.key)+" => "+tostring_dbg(n.value); })
			.as_array()
			.join(", ")
		+"}";
}

template<typename T>
string tostringhex_dbg(const T& item) { return tostringhex(item); }
static inline string tostringhex_dbg(const arrayview<uint8_t>& item)
{
	string ret = tostringhex(item)+" ";
	for (char c : item)
	{
		if (c >= 0x20 && c <= 0x7e) ret += c;
		else ret += '.';
	}
	return ret;
}
static inline string tostringhex_dbg(const arrayvieww<uint8_t>& item) { return tostringhex_dbg((arrayview<uint8_t>)item); }
static inline string tostringhex_dbg(const array<uint8_t>& item) { return tostringhex_dbg((arrayview<uint8_t>)item); }
