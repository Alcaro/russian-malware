#pragma once
#include "global.h"
#include "array.h"
#include "hash.h"
#include <string.h>

// define my own ctype, because table lookup is faster than libc call that probably ends up in a table lookup anyways,
//  and so I can define weird whitespace (\f\v) to not space (several Arlib modules require that, better centralize it)
// this means they don't obey locale, but all modern locales use UTF-8, for which isctype() has no useful answer
// locale shouldn't be in libc anyways; localization is complex enough to belong in a separate library that updates faster than libc,
//  and its global-state-based design interacts badly with libraries, logging, threading, text-based formats like JSON, etc

#ifdef _WIN32
#include <ctype.h> // include this one before windows.h does, the defines below confuse it
#endif
#define iscntrl my_iscntrl // define them all away, then don't bother implementing the ones I don't use
#define isprint my_isprint
#define isspace my_isspace
#define isblank my_isblank
#define isgraph my_isgraph
#define ispunct my_ispunct
#define isalnum my_isalnum
#define isalpha my_isalpha
#define isupper my_isupper
#define islower my_islower
#define isdigit my_isdigit
#define isxdigit my_isxdigit
#define tolower my_tolower
#define toupper my_toupper

extern const uint8_t char_props[256];
// bit meanings:
// 0x80 - space (\t\n\r ) - contrary to libc isspace, \f\v are not considered space
// 0x40 - digit (0-9)
// 0x20 - letter (A-Za-z) - tolower/toupper needs 0x20 to be letter,
// 0x10 - unused             and 0x80 is cheaper to test on some platforms, so it goes to the most common test (space)
// 0x08 - unused            other bit assignments are arbitrary
// 0x04 - lowercase (a-z)   also contrary to libc, these functions handle byte values only;
// 0x02 - uppercase (A-Z)    EOF is not a valid input (EOF feels like a poor design)
// 0x01 - hex digit (0-9A-Fa-f)
static inline bool isspace(uint8_t c) { return char_props[c] & 0x80; }
static inline bool isdigit(uint8_t c) { return char_props[c] & 0x40; }
static inline bool isalpha(uint8_t c) { return char_props[c] & 0x20; }
static inline bool islower(uint8_t c) { return char_props[c] & 0x04; }
static inline bool isupper(uint8_t c) { return char_props[c] & 0x02; }
static inline bool isalnum(uint8_t c) { return char_props[c] & 0x60; }
static inline bool isxdigit(uint8_t c) { return char_props[c] & 0x01; }
static inline uint8_t tolower(uint8_t c) { return c|(char_props[c]&0x20); }
static inline uint8_t toupper(uint8_t c) { return c&~(char_props[c]&0x20); }



// A string is a mutable byte sequence. It usually represents UTF-8 text, but can be arbitrary binary data, including NULs.
// All string functions taking or returning a char* assume/guarantee NUL termination. Anything using uint8_t* does not.

// cstring is an immutable sequence of bytes that does not own its storage; it usually points to a string constant, or part of a string.
// In most contexts, it's called stringview, but I feel that's too long.
// Long ago, cstring was just a typedef to 'const string&', hence its name.

// The child classes put various constraints on their contents that the parent does not; they do not obey the Liskov substitution principle.
// Any attempt to turn a string into cstring&, then call operator= or otherwise mutate it, is object slicing and undefined behavior.

class cstring;
class cstrnul;
class string;

#define OBJ_SIZE 16 // maximum 120, or the inline length overflows
                    // (127 would fit, but that requires an extra alignment byte, which throws the sizeof assert)
                    // minimum 16 on 64bit, 12 on 32bit
                    // most strings are short, so let's keep it small; 16 for all
#define MAX_INLINE (OBJ_SIZE-1) // macros instead of static const to make gdb not print them every time

class cstring {
	friend class string;
	friend class cstrnul;
	friend bool operator==(const cstring& left, const cstring& right);
#if __GNUC__ == 7
	template<size_t N> friend inline bool operator==(const cstring& left, const char (&right)[N]);
#else
	friend inline bool operator==(const cstring& left, const char * right);
#endif
	
	static uint32_t max_inline() { return MAX_INLINE; }
	
	union {
		struct {
			uint8_t m_inline[MAX_INLINE+1];
			// last byte is how many bytes are unused by the raw string data
			// if all bytes are used, there are zero unused bytes - which also serves as the NUL
			// if not inlined, it's -1
		};
		struct {
			uint8_t* m_data;
			uint32_t m_len; // always > MAX_INLINE, if not inlined; some of the operator== demand that
			bool m_nul; // whether the string is properly terminated (always true for string, possibly false for cstring)
			// 2 unused bytes here
			uint8_t m_reserved; // reserve space for the last byte of the inline data; never ever access this
		};
	};
	
	uint8_t& m_inline_len_w() { return m_inline[MAX_INLINE]; }
	int8_t m_inline_len() const { return m_inline[MAX_INLINE]; }
	
	forceinline bool inlined() const
	{
		static_assert(sizeof(cstring)==OBJ_SIZE);
		return m_inline_len() >= 0;
	}
	
	forceinline uint8_t len_if_inline() const
	{
		static_assert((MAX_INLINE & (MAX_INLINE+1)) == 0); // this xor trick only works for power of two minus 1
		return MAX_INLINE^m_inline_len();
	}
	
	forceinline const uint8_t * ptr() const
	{
		if (inlined()) return m_inline;
		else return m_data;
	}
	
	forceinline arrayvieww<uint8_t> bytes_raw() const
	{
		if (inlined())
			return arrayvieww<uint8_t>((uint8_t*)m_inline, len_if_inline());
		else
			return arrayvieww<uint8_t>(m_data, m_len);
	}
	
public:
	forceinline uint32_t length() const
	{
		if (inlined()) return len_if_inline();
		else return m_len;
	}
	
	forceinline arrayview<uint8_t> bytes() const { return bytes_raw(); }
	//If this is true, bytes()[bytes().size()] is '\0'. If false, it's undefined behavior.
	//this[this->length()] is always '\0', even if this is false.
	forceinline bool bytes_hasterm() const
	{
		return (inlined() || m_nul);
	}
	
private:
	forceinline void init_empty()
	{
		m_inline_len_w() = MAX_INLINE;
		m_inline[0] = '\0';
	}
	void init_from_nocopy(const char * str)
	{
		init_from_nocopy((uint8_t*)str, strlen(str), true);
	}
	void init_from_nocopy(const uint8_t * str, size_t len, bool has_nul = false)
	{
		if (len <= MAX_INLINE)
		{
			for (uint32_t i=0;i<len;i++) m_inline[i] = str[i]; // memcpy's constant overhead is huge if len is unknown
			m_inline[len] = '\0';
			m_inline_len_w() = MAX_INLINE-len;
		}
		else
		{
			if (len > 0xFFFFFFFF) abort();
			m_inline_len_w() = -1;
			
			m_data = (uint8_t*)str;
			m_len = len;
			m_nul = has_nul;
		}
	}
	void init_from_nocopy(arrayview<uint8_t> data, bool has_nul = false) { init_from_nocopy(data.ptr(), data.size(), has_nul); }
	void init_from_nocopy(const cstring& other) { *this = other; }
	
	// TODO: make some of these ctors constexpr, so gcc can optimize them into the data section (Clang already does)
	// partial or no initialization is c++20 only, so not until then
	// may need removing the union and memcpying things to/from a struct
	class noinit {};
	cstring(noinit) {}
	
public:
	cstring() { init_empty(); }
	cstring(const cstring& other) = default;
	
	cstring(const char * str) { init_from_nocopy(str); }
	cstring(const char8_t * str) { init_from_nocopy((char*)str); }
	cstring(arrayview<uint8_t> bytes) { init_from_nocopy(bytes); }
	cstring(arrayview<char> chars) { init_from_nocopy(chars.reinterpret<uint8_t>()); }
	cstring(nullptr_t) { init_empty(); }
	// If has_nul, then bytes[bytes.size()] is zero. (Undefined behavior does not count as zero.)
	cstring(arrayview<uint8_t> bytes, bool has_nul) { init_from_nocopy(bytes, has_nul); }
	cstring& operator=(const cstring& other) = default;
	cstring& operator=(const char * str) { init_from_nocopy(str); return *this; }
	cstring& operator=(const char8_t * str) { init_from_nocopy((char*)str); return *this; }
	cstring& operator=(nullptr_t) { init_empty(); return *this; }
	
	explicit operator bool() const { return length() != 0; }
	
	char operator[](int index) const { return ptr()[index]; }
	
	//~0 means end of the string, ~1 is last character
	//don't try to make -1 the last character, it breaks str.substr(x, ~0)
	//this shorthand exists only for substr()
	int32_t realpos(int32_t pos) const
	{
		if (pos >= 0) return pos;
		else return length()-~pos;
	}
	cstring substr(int32_t start, int32_t end) const
	{
		start = realpos(start);
		end = realpos(end);
		return cstring(arrayview<uint8_t>(ptr()+start, end-start), (bytes_hasterm() && (uint32_t)end == length()));
	}
	
	bool contains(cstring other) const
	{
		return memmem(this->ptr(), this->length(), other.ptr(), other.length()) != NULL;
	}
	size_t indexof(cstring other, size_t start = 0) const; // Returns -1 if not found.
	size_t lastindexof(cstring other) const;
	bool startswith(cstring other) const;
	bool endswith(cstring other) const;
	
	size_t iindexof(cstring other, size_t start = 0) const;
	size_t ilastindexof(cstring other) const;
	bool icontains(cstring other) const;
	bool istartswith(cstring other) const;
	bool iendswith(cstring other) const;
	bool iequals(cstring other) const;
	
	string replace(cstring in, cstring out) const;
	
	//crsplitwi - cstring-returning backwards-counting split on word boundaries, inclusive
	//cstring-returning - obvious
	//backwards-counting - splits at the rightmost opportunity, "a b c d".rsplit<1>(" ") is ["a b c", "d"]
	//word boundary - isspace()
	//inclusive - the boundary string is included in the output, "a\nb\n".spliti("\n") is ["a\n", "b\n"]
	//all subsets of splitting options are supported
	
	array<cstring> csplit(cstring sep, size_t limit) const;
	template<size_t limit = SIZE_MAX>
	array<cstring> csplit(cstring sep) const { return csplit(sep, limit); }
	
	array<cstring> crsplit(cstring sep, size_t limit) const;
	template<size_t limit>
	array<cstring> crsplit(cstring sep) const { return crsplit(sep, limit); }
	
	array<string> split(cstring sep, size_t limit) const { return csplit(sep, limit).cast<string>(); }
	template<size_t limit = SIZE_MAX>
	array<string> split(cstring sep) const { return split(sep, limit); }
	
	array<string> rsplit(cstring sep, size_t limit) const { return crsplit(sep, limit).cast<string>(); }
	template<size_t limit>
	array<string> rsplit(cstring sep) const { return rsplit(sep, limit); }
	
	
	array<cstring> cspliti(cstring sep, size_t limit) const;
	template<size_t limit = SIZE_MAX>
	array<cstring> cspliti(cstring sep) const { return cspliti(sep, limit); }
	
	array<cstring> crspliti(cstring sep, size_t limit) const;
	template<size_t limit>
	array<cstring> crspliti(cstring sep) const { return crspliti(sep, limit); }
	
	array<string> spliti(cstring sep, size_t limit) const { return cspliti(sep, limit).cast<string>(); }
	template<size_t limit = SIZE_MAX>
	array<string> spliti(cstring sep) const { return spliti(sep, limit); }
	
	array<string> rspliti(cstring sep, size_t limit) const { return crspliti(sep, limit).cast<string>(); }
	template<size_t limit>
	array<string> rspliti(cstring sep) const { return rspliti(sep, limit); }
	
private:
	// Input: Three pointers, start <= at <= end. The found match must be within the incoming at..end.
	// Output: Set at/end.
	array<cstring> csplit(bool(*find)(const uint8_t * start, const uint8_t * & at, const uint8_t * & end), size_t limit) const;
	
public:
	template<typename T>
	std::enable_if_t<sizeof(T::match(nullptr,nullptr,nullptr))!=0, array<cstring>>
	csplit(T regex, size_t limit) const
	{
		return csplit([](const uint8_t * start, const uint8_t * & at, const uint8_t * & end)->bool {
			auto cap = T::match((char*)start, (char*)at, (char*)end);
			if (!cap) return false;
			at = (uint8_t*)cap[0].start;
			end = (uint8_t*)cap[0].end;
			return true;
		}, limit);
	}
	template<size_t limit = SIZE_MAX, typename T>
	std::enable_if_t<sizeof(T::match(nullptr,nullptr,nullptr))!=0, array<cstring>>
	csplit(T regex) const { return csplit(regex, limit); }
	
	template<typename T>
	std::enable_if_t<sizeof(T::match(nullptr,nullptr,nullptr))!=0, array<string>>
	split(T regex, size_t limit) const
	{
		return csplit(regex, limit).template cast<string>();
	}
	template<size_t limit = SIZE_MAX, typename T>
	std::enable_if_t<sizeof(T::match(nullptr,nullptr,nullptr))!=0, array<string>>
	split(T regex) const { return split(regex, limit); }
	
	string upper() const; // Only considers ASCII, will not change ø. Will capitalize a decomposed ñ, but not a precomposed one.
	string lower() const;
	cstring trim() const; // Deletes whitespace at start and end. Does not do anything to consecutive whitespace in the middle.
	bool contains_nul() const;
	
	bool isutf8() const; // NUL is considered valid UTF-8. Modified UTF-8, CESU-8, WTF-8, etc are not.
	// The index is updated to point to the next codepoint. Initialize it to zero; stop when it equals the string's length.
	// If invalid UTF-8, or descynchronized index, returns U+DC80 through U+DCFF; callers are welcome to treat this as an error.
	uint32_t codepoint_at(uint32_t& index) const;
	
	//Whether the string matches a glob pattern. ? in 'pat' matches any byte (not utf8 codepoint), * matches zero or more bytes.
	//NUL bytes are treated as any other byte, in both strings.
	bool matches_glob(cstring pat) const __attribute__((pure)) { return matches_glob(pat, false); }
	// Case insensitive. Considers ASCII only, øØ are considered nonequal.
	bool matches_globi(cstring pat) const __attribute__((pure)) { return matches_glob(pat, true); }
private:
	bool matches_glob(cstring pat, bool case_insensitive) const __attribute__((pure));
public:
	
	string leftPad (size_t len, uint8_t ch = ' ') const;
	
	size_t hash() const { return ::hash(ptr(), length()); }
	
private:
	class c_string {
		char* ptr;
		bool do_free;
	public:
		
		c_string(arrayview<uint8_t> data, bool has_term)
		{
			if (has_term)
			{
				ptr = (char*)data.ptr();
				do_free = false;
			}
			else
			{
				ptr = (char*)xmalloc(data.size()+1);
				memcpy(ptr, data.ptr(), data.size());
				ptr[data.size()] = '\0';
				do_free = true;
			}
		}
		operator const char *() const { return ptr; }
		const char * c_str() const { return ptr; }
		inline operator cstrnul() const;
		~c_string() { if (do_free) free(ptr); }
	};
public:
	//no operator const char *, a cstring doesn't necessarily have a NUL terminator
	c_string c_str() const { return c_string(bytes(), bytes_hasterm()); }
};


// Like cstring, but guaranteed to have a nul terminator.
class cstrnul : public cstring {
	friend class cstring;
	friend class string;
	forceinline const char * ptr_withnul() const { return (char*)ptr(); }
	
	class has_nul {};
	cstrnul(noinit) : cstring(noinit()) {}
	cstrnul(arrayview<uint8_t> bytes, has_nul) : cstring(noinit()) { init_from_nocopy(bytes, true); }
	
public:
	cstrnul() { init_empty(); }
	cstrnul(const cstrnul& other) = default;
	cstrnul(const char * str) { init_from_nocopy(str); }
	
	cstrnul(nullptr_t) { init_empty(); }
	cstrnul& operator=(const cstring& other) = delete;
	cstrnul& operator=(const cstrnul& other) = default;
	cstrnul& operator=(const char * str) { init_from_nocopy(str); return *this; }
	cstrnul& operator=(nullptr_t) { init_empty(); return *this; }
	
	explicit operator bool() const { return length() != 0; }
	operator const char * () const { return ptr_withnul(); }
	
	cstrnul substr_nul(int32_t start) const
	{
		start = realpos(start);
		return cstrnul(arrayview<uint8_t>(ptr()+start, length()-start), has_nul());
	}
};


class string : public cstrnul {
	friend class cstring;
	friend class cstrnul;
	
	static size_t bytes_for(size_t len) { return bitround(len+1); }
	forceinline uint8_t * ptr() { return (uint8_t*)cstring::ptr(); }
	forceinline const uint8_t * ptr() const { return cstring::ptr(); }
	void resize(size_t newlen);
	
	void init_from(const char * str)
	{
		//if (!str) str = "";
		init_from((uint8_t*)str, strlen(str));
	}
	forceinline void init_from(const uint8_t * str, size_t len)
	{
		if (__builtin_constant_p(len))
		{
			if (len <= MAX_INLINE)
			{
				memcpy(m_inline, str, len);
				m_inline[len] = '\0';
				m_inline_len_w() = max_inline()-len;
			}
			else init_from_large(str, len);
		}
		else init_from_outline(str, len);
	}
	forceinline void init_from(arrayview<uint8_t> data) { init_from(data.ptr(), data.size()); }
	void init_from_outline(const uint8_t * str, size_t len);
	void init_from_large(const uint8_t * str, size_t len);
	void init_from(const cstring& other);
	void init_from(string&& other)
	{
		memcpy((void*)this, (void*)&other, sizeof(*this));
		other.init_empty();
	}
	
	void reinit_from(const char * str)
	{
		if (!str) str = "";
		reinit_from(arrayview<uint8_t>((uint8_t*)str, strlen(str)));
	}
	void reinit_from(arrayview<uint8_t> data);
	void reinit_from(cstring other)
	{
		reinit_from(other.bytes());
	}
	void reinit_from(string&& other)
	{
		deinit();
		memcpy((void*)this, (void*)&other, sizeof(*this));
		other.init_empty();
	}
	
	void deinit()
	{
		if (!inlined()) free(m_data);
	}
	
	void append(arrayview<uint8_t> newdat);
	
	void append(uint8_t newch)
	{
		uint32_t oldlength = length();
		resize(oldlength + 1);
		ptr()[oldlength] = newch;
	}
	
public:
	//Resizes the string to a suitable size, then allows the caller to fill it in. Initial contents are undefined.
	arrayvieww<uint8_t> construct(size_t len)
	{
		resize(len);
		return bytes();
	}
	
	string& operator+=(const char * right)
	{
		append(arrayview<uint8_t>((uint8_t*)right, strlen(right)));
		return *this;
	}
	
	string& operator+=(cstring right)
	{
		append(right.bytes());
		return *this;
	}
	
	
	string& operator+=(char right)
	{
		append((uint8_t)right);
		return *this;
	}
	
	string& operator+=(uint8_t right)
	{
		append(right);
		return *this;
	}
	
	// for other integer types, fail (short/long/etc will be ambiguous)
	string& operator+=(int right) = delete;
	string& operator+=(unsigned right) = delete;
	
	
	string() : cstrnul(noinit()) { init_empty(); }
	string(const string& other) : cstrnul(noinit()) { init_from(other); }
	string(string&& other) : cstrnul(noinit()) { init_from(std::move(other)); }
	
	forceinline string(cstring other) : cstrnul(noinit()) { init_from(other); }
	forceinline string(arrayview<uint8_t> bytes) : cstrnul(noinit()) { init_from(bytes); }
	forceinline string(arrayview<char> chars) : cstrnul(noinit()) { init_from(chars.reinterpret<uint8_t>()); }
	forceinline string(const char * str) : cstrnul(noinit()) { init_from(str); }
	forceinline string(const char8_t * str) : cstrnul(noinit()) { init_from((char*)str); }
	string(array<uint8_t>&& bytes);
	forceinline string(nullptr_t) = delete;
	forceinline string& operator=(const string& other) { reinit_from(other); return *this; }
	forceinline string& operator=(const cstring& other) { reinit_from(other); return *this; }
	forceinline string& operator=(string&& other) { reinit_from(std::move(other)); return *this; }
	forceinline string& operator=(const char * str) { reinit_from(str); return *this; }
	forceinline string& operator=(const char8_t * str) { reinit_from((char*)str); return *this; }
	forceinline string& operator=(nullptr_t) { deinit(); init_empty(); return *this; }
	~string() { deinit(); }
	
	explicit operator bool() const { return length() != 0; }
	operator const char * () const { return ptr_withnul(); }
	
	//Reading the NUL terminator is fine. Writing the terminator, or poking beyond the NUL, is undefined behavior.
	forceinline uint8_t& operator[](int index) { return ptr()[index]; }
	forceinline uint8_t operator[](int index) const { return ptr()[index]; }
	
	forceinline arrayview<uint8_t> bytes() const { return bytes_raw(); }
	forceinline arrayvieww<uint8_t> bytes() { return bytes_raw(); }
	
	//Takes ownership of the given pointer. Will free() it when done.
	static string create_usurp(char * str);
	static string create_usurp(array<uint8_t>&& in) { return string(std::move(in)); }
	
	//Returns a string containing a single NUL.
	static cstring nul() { return arrayview<uint8_t>((uint8_t*)"", 1); }
	
	//Returns U+FFFD for UTF16-reserved codepoints and other forbidden codepoints. 0 yields a NUL byte.
	static string codepoint(uint32_t cp);
	// Returns number of bytes written. Buffer must be at least 4 bytes. Does not NUL terminate.
	// May write garbage between out+return and out+4.
	static size_t codepoint(uint8_t* out, uint32_t cp);
	
	//3-way comparison. If a comes first, return value is negative; if equal, zero; if b comes first, positive.
	//Comparison is bytewise. End goes before NUL, so the empty string comes before everything else.
	//The return value is not guaranteed to be in [-1..1]. It's not even guaranteed to fit in anything smaller than int.
	static int compare3(cstring a, cstring b);
	//Like the above, but case insensitive (treat every letter as uppercase). Considers ASCII only, øØ are considered nonequal.
	//If the strings are case-insensitively equal, uppercase goes first.
	static int icompare3(cstring a, cstring b);
	static bool less(cstring a, cstring b) { return compare3(a, b) < 0; }
	static bool iless(cstring a, cstring b) { return icompare3(a, b) < 0; }
	
	//Natural comparison; "8" < "10". Other than that, same as above.
	//Exact rules:
	//  Strings are compared component by component. A component is either a digit sequence, or a non-digit. 8 < 10, 2 = 02
	//  - and . are not part of the digit sequence. -1 < -2, 1.2 < 1.03
	//  If the strings are otherwise equal, repeat the comparison, but with 2 < 02. If still equal, repeat case sensitively (if applicable).
	//  Digits (0x30-0x39) belong after $ (0x24), but before @ (0x40).
	//Correct sorting is a$ a1 a2 a02 a2a a2a1 a02a2 a2a3 a2b a02b A3A A3a a3A a3a A03A A03a a03A a03a a10 a11 aa a@
	//It's named snat... instead of nat... because case sensitive natural comparison is probably a mistake; it shouldn't be the default.
	static int snatcompare3(cstring a, cstring b) { return string::natcompare3(a, b, false); }
	static int inatcompare3(cstring a, cstring b) { return string::natcompare3(a, b, true); }
	static bool snatless(cstring a, cstring b) { return snatcompare3(a, b) < 0; }
	static bool inatless(cstring a, cstring b) { return inatcompare3(a, b) < 0; }
private:
	static int natcompare3(cstring a, cstring b, bool case_insensitive);
public:
};
cstring::c_string::operator cstrnul() const { return ptr; }

// TODO: I need a potentially-owning string class
// cstring never owns memory, string always does, new one has a flag for whether it does
// it's like a generalized cstring::c_str()
// will be immutable after creation, like cstring
// will be used for json/bml parsers, and most likely a lot more
// need to find a good name for it first
// also need to check how much time SSO saves once that class exists

#undef OBJ_SIZE
#undef MAX_INLINE


// using cstring rather than const cstring& is cleaner, but makes GCC 7 emit slightly worse code
// TODO: check if that's still true for GCC > 7
#if __GNUC__ == 7
// TODO: delete friend declaration when deleting this
template<size_t N> inline bool operator==(const cstring& left, char (&right)[N]) { return operator==(left, (const char*)right); }
template<size_t N> inline bool operator==(const cstring& left, const char (&right)[N])
{
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 8 && __GNUC__ <= 10 && __cplusplus >= 201103
	// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=91212
	return operator==(left, (const char*)right);
#else
	if (N-1 <= cstring::max_inline())
		return ((uint8_t)left.m_inline_len() == cstring::max_inline()-(N-1) && memeq(left.m_inline, right, N-1));
	else
		return (!left.inlined() && left.m_len == N-1 && memeq(left.m_data, right, N-1));
#endif
}
template<typename T, typename Ttest = std::enable_if_t<std::is_same_v<T,const char*> || std::is_same_v<T,char*>>>
inline bool operator==(const cstring& left, T right) { return left.bytes() == arrayview<uint8_t>((uint8_t*)right, strlen(right)); }
#else
forceinline bool operator==(const cstring& left, const char * right)
{
	size_t len = strlen(right);
	if (__builtin_constant_p(len))
	{
		if (len <= cstring::max_inline())
			return ((uint8_t)left.m_inline_len() == (cstring::max_inline()^len) && memeq(left.m_inline, right, len));
		else
			return (!left.inlined() && left.m_len == len && memeq(left.m_data, right, len));
	}
	else return left.bytes() == arrayview<uint8_t>((uint8_t*)right, len);
}
#endif

inline bool operator==(const char * left, const cstring& right) { return operator==(right, left); }
#ifdef __SSE2__
bool operator==(const cstring& left, const cstring& right);
#else
inline bool operator==(const cstring& left, const cstring& right)
{
	return left.bytes() == right.bytes();
}
#endif
inline bool operator!=(const cstring& left, const char * right  ) { return !operator==(left, right); }
inline bool operator!=(const cstring& left, const cstring& right) { return !operator==(left, right); }
inline bool operator!=(const char * left,   const cstring& right) { return !operator==(left, right); }

bool operator<(cstring left,      const char * right) = delete;
bool operator<(cstring left,      cstring right     ) = delete;
bool operator<(const char * left, cstring right     ) = delete;

inline string operator+(cstring left,      cstring right     ) { string ret = left; ret += right; return ret; }
inline string operator+(cstring left,      const char * right) { string ret = left; ret += right; return ret; }
inline string operator+(string&& left,     cstring right     ) { left += right; return left; }
inline string operator+(string&& left,     const char * right) { left += right; return left; }
inline string operator+(const char * left, cstring right     ) { string ret = left; ret += right; return ret; }

inline string operator+(string&& left, char right) = delete;
inline string operator+(cstring left, char right) = delete;
inline string operator+(char left, cstring right) = delete;

//Checks if needle is one of the 'separator'-separated words in the haystack. The needle may not contain 'separator' or be empty.
//For example, haystack "GL_EXT_FOO GL_EXT_BAR GL_EXT_QUUX" (with space as separator) contains needles
// 'GL_EXT_FOO', 'GL_EXT_BAR' and 'GL_EXT_QUUX', but not 'GL_EXT_QUU'.
bool strtoken(const char * haystack, const char * needle, char separator);

template<typename T>
cstring arrayview<T>::get_or(size_t n, const char * def) const
{
	if (n < count) return items[n];
	else return def;
};
