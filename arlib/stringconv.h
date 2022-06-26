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
string tostringhex(unsigned long long val, size_t mindigits = 0); // mindigits must be <= 2*sizeof; tostringhex(uint8, 3) is illegal

// signless char isn't integral, so not here (or anywhere else, I don't know what it should be)
inline string tostring(  signed char val)  { return tostring((signed long long)val); }
inline string tostring(unsigned char val)  { return tostring((unsigned long long)val); }
inline string tostring(  signed short val) { return tostring((signed long long)val); }
inline string tostring(unsigned short val) { return tostring((unsigned long long)val); }
inline string tostring(  signed int val)   { return tostring((signed long long)val); }
inline string tostring(unsigned int val)   { return tostring((unsigned long long)val); }
inline string tostring(  signed long val)  { return tostring((signed long long)val); }
inline string tostring(unsigned long val)  { return tostring((unsigned long long)val); }
inline string tostringhex(unsigned char val, size_t mindigits = 0)  { return tostringhex((unsigned long long)val, mindigits); }
inline string tostringhex(unsigned short val, size_t mindigits = 0) { return tostringhex((unsigned long long)val, mindigits); }
inline string tostringhex(unsigned int val, size_t mindigits = 0)   { return tostringhex((unsigned long long)val, mindigits); }
inline string tostringhex(unsigned long val, size_t mindigits = 0)  { return tostringhex((unsigned long long)val, mindigits); }
template<size_t mindigits, typename T> inline string tostringhex(T val) { return tostringhex((std::make_unsigned_t<T>)val, mindigits); }

string tostring(double val);
string tostring(float val);
inline string tostring(bool val) { return val ? "true" : "false"; }

inline string tostring(const char * s) { return s; } // only exists as tostring, fromstring would be a memory leak

// todo: change to template <typename T> requires sizeof((string)std::declval<T>())!=0 string tostring(const T& val) in c++20
template<typename T>
typename std::enable_if_t<sizeof((string)std::declval<T>()) != 0, string>
tostring(const T& val)
{
	return (string)val;
}


size_t tostring_len(  signed long long val);
size_t tostring_len(unsigned long long val);
inline size_t tostring_len(  signed char val)  { return tostring_len((  signed long long)val); } // max 4 (-128)
inline size_t tostring_len(unsigned char val)  { return tostring_len((unsigned long long)val); } // max 3 (255)
inline size_t tostring_len(  signed short val) { return tostring_len((  signed long long)val); } // max 6 (-32768)
inline size_t tostring_len(unsigned short val) { return tostring_len((unsigned long long)val); } // max 5 (65535)
inline size_t tostring_len(  signed int val)   { return tostring_len((  signed long long)val); } // max 11 (-2147483648)
inline size_t tostring_len(unsigned int val)   { return tostring_len((unsigned long long)val); } // max 10 (4294967295)
inline size_t tostring_len(  signed long val)  { return tostring_len((  signed long long)val); } // max 20 (-9223372036854775808)
inline size_t tostring_len(unsigned long val)  { return tostring_len((unsigned long long)val); } // max 20 (18446744073709551615)

inline size_t tostring_len(bool val) { return 5-val; } // 5 or 4
inline size_t tostring_len(const char * val) { return strlen(val); }
inline size_t tostring_len(cstring val) { return val.length(); }

// These two will overestimate, and tostring_ptr will return the actual length used.
inline size_t tostring_len(float val) { return 17; }
inline size_t tostring_len(double val) { return 24; }

// For strings and negative integers, length must be equal to tostring_len. Return value is same length.
// For zero, positive, unsigned or hex integers, length can be bigger, and will be zero padded. Return value is the length written.
// For floats, returns the actual length used, which can be less than the input.
// None of them are nul terminated.
size_t tostring_ptr(char* buf,   signed long long val, size_t len);
size_t tostring_ptr(char* buf, unsigned long long val, size_t len);
inline size_t tostring_ptr(char* buf,   signed char val, size_t len) { return tostring_ptr(buf, (signed long long)val, len); }
inline size_t tostring_ptr(char* buf, unsigned char val, size_t len) { return tostring_ptr(buf, (unsigned long long)val, len); }
inline size_t tostring_ptr(char* buf,   signed short val,size_t len) { return tostring_ptr(buf, (signed long long)val, len); }
inline size_t tostring_ptr(char* buf, unsigned short val,size_t len) { return tostring_ptr(buf, (unsigned long long)val, len); }
inline size_t tostring_ptr(char* buf,   signed int val,  size_t len) { return tostring_ptr(buf, (signed long long)val, len); }
inline size_t tostring_ptr(char* buf, unsigned int val,  size_t len) { return tostring_ptr(buf, (unsigned long long)val, len); }
inline size_t tostring_ptr(char* buf,   signed long val, size_t len) { return tostring_ptr(buf, (signed long long)val, len); }
inline size_t tostring_ptr(char* buf, unsigned long val, size_t len) { return tostring_ptr(buf, (unsigned long long)val, len); }

size_t tostringhex_ptr(char* buf, unsigned long long val, size_t len); // no overloads here

inline size_t tostring_ptr(char* buf, const char * val, size_t len) { memcpy(buf, val, len); return len; }
inline size_t tostring_ptr(char* buf, cstring val, size_t len) { memcpy(buf, val.bytes().ptr(), len); return len; }

size_t tostring_ptr(char* buf, float val);
size_t tostring_ptr(char* buf, double val);
inline size_t tostring_ptr(char* buf, float val, size_t len) { return tostring_ptr(buf, val); }
inline size_t tostring_ptr(char* buf, double val, size_t len) { return tostring_ptr(buf, val); }

template<typename T> struct fmt_pad_t { T v; size_t n; fmt_pad_t(T v, size_t n) : v(v), n(n) {} };
template<typename T> size_t tostring_len(fmt_pad_t<T> val) { return val.n; }
template<typename T> size_t tostring_ptr(char* buf, fmt_pad_t<T> val, size_t len) { return tostring_ptr(buf, val.v, val.n); }
template<size_t len, typename T> std::enable_if_t<std::is_unsigned_v<T>, fmt_pad_t<T>> fmt_pad(T in) { return { in, len }; }

template<typename T> struct fmt_hex_t { T v; size_t n; fmt_hex_t(T v, size_t n) : v(v), n(n) {} };
template<typename T> size_t tostring_len(fmt_hex_t<T> val) { return val.n; }
template<typename T> size_t tostring_ptr(char* buf, fmt_hex_t<T> val, size_t len) { return tostringhex_ptr(buf, val.v, val.n); }
template<size_t len = 0, typename T> std::enable_if_t<std::is_unsigned_v<T>, fmt_hex_t<T>> fmt_hex(T in) { return { in, len ? len : sizeof(T)*2 }; }

template<typename... Ts> forceinline string format(const Ts&... args)
{
	size_t n = 0;
	size_t lens[sizeof...(args)];
	size_t len = 0;
	((len += lens[n++] = tostring_len(args)), ...); // be careful with this thing, it easily confuses the optimizer
	string ret;
	char* ptr = (char*)ret.construct(len).ptr();
	char* iter = ptr;
	n = 0;
	((iter += tostring_ptr(iter, args, lens[n++])), ...);
	if ((std::is_floating_point_v<Ts> || ...))
		ret = ret.substr(0, iter-ptr);
	return ret;
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

bool fromstringhex_ptr(const char * s, size_t len, unsigned char & out);
bool fromstringhex_ptr(const char * s, size_t len, unsigned short & out);
bool fromstringhex_ptr(const char * s, size_t len, unsigned int & out);
bool fromstringhex_ptr(const char * s, size_t len, unsigned long & out);
bool fromstringhex_ptr(const char * s, size_t len, unsigned long long & out);

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
