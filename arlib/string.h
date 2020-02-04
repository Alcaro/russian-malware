#pragma once
#include "global.h"
#include "array.h"
#include "hash.h"
#include <string.h>
#include <ctype.h>

//A string is a mutable byte container. It usually represents UTF-8 text, but can be arbitrary binary data, including NULs.
//All string functions taking or returning a char* assume/guarantee NUL termination. Anything using uint8_t* does not.

//cstring is an immutable sequence of bytes that does not own its storage. If the storage is modified, the cstring may not be used.
//In most contexts, it's called stringview, but I feel that's too long.
//Long ago, cstring was just a typedef to 'const string&', hence its name.

class string;

#define OBJ_SIZE 16 // maximum 120, or the inline length overflows
                    // (127 would fit, but that requires an extra alignment byte, which throws the sizeof assert)
                    // minimum 16 (pointer + various members + alignment)
                    //  (actually minimum 12 on 32bit, but who needs 32bit)
#define MAX_INLINE (OBJ_SIZE-1) // macros instead of static const to make gdb not print them every time

class cstring {
	friend class string;
	
	static uint32_t max_inline() { return MAX_INLINE; }
	
	union {
		struct {
			uint8_t m_inline[MAX_INLINE];
			
			//this is how many bytes are unused by the raw string data
			//if all bytes are used, there are zero unused bytes - which also serves as the NUL
			//if not inlined, it's -1
			int8_t m_inline_len;
		};
		struct {
			uint8_t* m_data;
			uint32_t m_len;
			bool m_nul; // whether the string is properly terminated (always true for string, possibly false for cstring)
			uint8_t reserved; // reserve space for the last byte of the inline data; never ever access this
		};
	};
	
	forceinline bool inlined() const
	{
		static_assert(sizeof(cstring)==OBJ_SIZE);
		
		return m_inline_len >= 0;
	}
	
	const uint8_t * ptr() const
	{
		if (inlined()) return m_inline;
		else return m_data;
	}
	
public:
	uint32_t length() const
	{
		if (inlined()) return MAX_INLINE-m_inline_len;
		else return m_len;
	}
	
	arrayview<byte> bytes() const
	{
		if (inlined())
			return arrayview<byte>(m_inline, MAX_INLINE-m_inline_len);
		else
			return arrayview<byte>(m_data, m_len);
	}
	//If this is true, bytes()[bytes().length()] is '\0'. If false, it's undefined behavior.
	bool bytes_hasterm() const
	{
		return (inlined() || m_nul);
	}
	
private:
	forceinline void init_empty()
	{
		m_inline_len = MAX_INLINE;
		m_inline[0] = '\0';
	}
	void init_from_nocopy(const char * str)
	{
		if (!str) str = "";
		init_from_nocopy(arrayview<byte>((uint8_t*)str, strlen(str)), true);
	}
	void init_from_nocopy(arrayview<byte> data, bool has_nul = false)
	{
		const uint8_t * str = data.ptr();
		uint32_t len = data.size();
		
		if (len <= MAX_INLINE)
		{
			memcpy(m_inline, str, len);
			m_inline[len] = '\0';
			m_inline_len = MAX_INLINE-len;
		}
		else
		{
			m_inline_len = -1;
			
			m_data = (uint8_t*)str;
			m_len = len;
			m_nul = has_nul;
		}
	}
	void init_from_nocopy(const cstring& other)
	{
		memcpy(this, &other, sizeof(*this));
	}
	
	char getchar(uint32_t index) const
	{
		//this function is REALLY hot, use the strongest possible optimizations
		if (inlined()) return m_inline[index];
		else if (index < m_len) return m_data[index];
		else return '\0'; // for cstring, which isn't necessarily NUL-terminated
	}
	
	class noinit {};
	forceinline cstring(noinit) {}
	
	cstring(arrayview<uint8_t> bytes, bool has_nul) { init_from_nocopy(bytes, has_nul); }
public:
	cstring() { init_empty(); }
	cstring(const cstring& other) = default;
	cstring(const char * str) { init_from_nocopy(str); }
	cstring(arrayview<uint8_t> bytes) { init_from_nocopy(bytes); }
	cstring(arrayview<char> chars) { init_from_nocopy(chars.reinterpret<uint8_t>()); }
	cstring(nullptr_t) { init_empty(); }
	cstring& operator=(const cstring& other) = default;
	cstring& operator=(const char * str) { init_from_nocopy(str); return *this; }
	cstring& operator=(nullptr_t) { init_empty(); return *this; }
	
	explicit operator bool() const { return length() != 0; }
	//explicit operator const char * () const { return ptr_withnul(); }
	
	char operator[](int index) const { return getchar(index); }
	
	//static string create(arrayview<uint8_t> data) { string ret=noinit(); ret.init_from(data.ptr(), data.size()); return ret; }
	
	//~0 means end of the string, ~1 is last character
	//don't try to make -1 the last character, it makes str.substr(x, ~0) blow up
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
		return cstring(arrayview<byte>(ptr()+start, end-start), (bytes_hasterm() && (uint32_t)end == length()));
	}
	
	bool contains(cstring other) const
	{
		return memmem(this->ptr(), this->length(), other.ptr(), other.length()) != NULL;
	}
	size_t indexof(cstring other) const
	{
		uint8_t* ptr = (uint8_t*)memmem(this->ptr(), this->length(), other.ptr(), other.length());
		if (ptr) return ptr - this->ptr();
		else return (size_t)-1;
	}
	size_t lastindexof(cstring other) const
	{
		size_t ret = -1;
		const uint8_t* start = this->ptr();
		const uint8_t* find = start;
		const uint8_t* find_end = find + this->length();
		if (!other) return this->length();
		
		while (true)
		{
			find = (uint8_t*)memmem(find, find_end-find, other.ptr(), other.length());
			if (!find) return ret;
			ret = find-start;
			find += 1;
		}
	}
	bool startswith(cstring other) const
	{
		if (other.length() > this->length()) return false;
		return (!memcmp(this->ptr(), other.ptr(), other.length()));
	}
	bool endswith(cstring other) const
	{
		if (other.length() > this->length()) return false;
		return (!memcmp(this->ptr()+this->length()-other.length(), other.ptr(), other.length()));
	}
	bool icontains(cstring other) const
	{
		if (other.length() > this->length()) return false;
		const char* a = (char*)this->ptr();
		const char* b = (char*)other.ptr();
		for (size_t start=0;start<=this->length()-other.length();start++)
		{
			size_t i;
			for (i=0;i<other.length();i++)
			{
				if (tolower(a[start+i]) != tolower(b[i])) break;
			}
			if (i==other.length()) return true;
		}
		return false;
	}
	bool istartswith(cstring other) const
	{
		if (other.length() > this->length()) return false;
		const char* a = (char*)this->ptr();
		const char* b = (char*)other.ptr();
		for (size_t i=0;i<other.length();i++)
		{
			if (tolower(a[i]) != tolower(b[i])) return false;
		}
		return true;
	}
	bool iendswith(cstring other) const
	{
		if (other.length() > this->length()) return false;
		const char* a = (char*)this->ptr()+this->length()-other.length();
		const char* b = (char*)other.ptr();
		for (size_t i=0;i<other.length();i++)
		{
			if (tolower(a[i]) != tolower(b[i])) return false;
		}
		return true;
	}
	bool iequals(cstring other) const
	{
		return (this->length() == other.length() && this->istartswith(other));
	}
	
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
	
	//TODO: do I need these?
	
	//array<cstring> csplitw(size_t limit) const;
	//template<size_t limit = SIZE_MAX>
	//array<cstring> csplitw() const { return csplitw(limit); }
	//
	//array<cstring> crsplitw(size_t limit) const;
	//template<size_t limit>
	//array<cstring> crsplitw() const { return crsplitw(limit); }
	//
	//array<string> splitw(size_t limit) const { return csplitw(limit).cast<string>(); }
	//template<size_t limit = SIZE_MAX>
	//array<string> splitw() const { return splitw(limit); }
	//
	//array<string> rsplitw(size_t limit) const { return crsplitw(limit).cast<string>(); }
	//template<size_t limit>
	//array<string> rsplitw() const { return rsplitw(limit); }
	
private:
	// Input: Three pointers, start <= at <= end. The found match must be within the incoming at..end.
	// Output: Set at/end.
	array<cstring> csplit(bool(*find)(const uint8_t * start, const uint8_t * & at, const uint8_t * & end), size_t limit) const;
	
public:
	template<typename T>
	std::enable_if_t<sizeof(std::declval<T>().match(nullptr,nullptr)), array<cstring>>
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
	std::enable_if_t<sizeof(std::declval<T>().match(nullptr,nullptr)), array<cstring>>
	csplit(T regex) const { return csplit(regex, limit); }
	
	template<typename T>
	std::enable_if_t<sizeof(std::declval<T>().match(nullptr,nullptr)), array<string>>
	split(T regex, size_t limit) const
	{
		return csplit(regex, limit).template cast<string>();
	}
	template<size_t limit = SIZE_MAX, typename T>
	std::enable_if_t<sizeof(std::declval<T>().match(nullptr,nullptr)), array<string>>
	split(T regex) const { return split(regex, limit); }
	
	cstring trim() const
	{
		const uint8_t * chars = ptr();
		int start = 0;
		int end = length();
		while (end > start && isspace(chars[end-1])) end--;
		while (start < end && isspace(chars[start])) start++;
		return substr(start, end);
	}
	
	inline string lower() const; // Only considers ASCII.
	inline string upper() const;
	
	bool isutf8() const; // NUL is considered valid UTF-8. U+D800, overlong encodings, etc are not.
	// Treats the string as UTF-8 and returns the codepoint there.
	// If not UTF-8 or not a start index, returns U+DC80 through U+DCFF. Callers are welcome to treat this as an error.
	// The index is updated to point to the next codepoint. Initialize it to zero; stop when it equals the string's length.
	// If index is out of bounds, returns zero and does not advance index.
	// If the string contains 00s, this function will treat it as U+0000. Callers are welcome to explicitly reject that.
	int32_t codepoint_at(uint32_t& index, int32_t eof = 0) const;
	
	//Whether the string matches a glob pattern. ? in 'pat' matches any one byte, * matches zero or more bytes.
	//NUL bytes are treated as any other byte, in both strings.
	bool matches_glob(cstring pat) const __attribute__((pure)) { return matches_glob(pat, false); }
	// Case insensitive. Considers ASCII only, øØ are considered nonequal.
	bool matches_globi(cstring pat) const __attribute__((pure)) { return matches_glob(pat, true); }
private:
	bool matches_glob(cstring pat, bool case_insensitive) const __attribute__((pure));
public:
	
	size_t hash() const { return ::hash((char*)ptr(), length()); }
	
private:
	class c_string {
		char* ptr;
		bool do_free;
	public:
		
		c_string(arrayview<byte> data, bool has_term)
		{
			if (has_term)
			{
				ptr = (char*)data.ptr();
				do_free = false;
			}
			else
			{
				ptr = (char*)malloc(data.size()+1);
				memcpy(ptr, data.ptr(), data.size());
				ptr[data.size()] = '\0';
				do_free = true;
			}
		}
		operator const char *() const { return ptr; }
		const char * c_str() const { return ptr; }
		~c_string() { if (do_free) free(ptr); }
	};
public:
	bool contains_nul() const { return memchr(ptr(), '\0', length()); }
	//no operator const char *, a cstring doesn't necessarily have a NUL terminator
	c_string c_str() const { return c_string(bytes(), bytes_hasterm()); }
};


class string : public cstring {
	friend class cstring;
	
	static size_t bytes_for(uint32_t len)
	{
		return bitround(len+1);
	}
	//static uint8_t* alloc(uint8_t* prev, uint32_t prevsize, uint32_t newsize);
	
	uint8_t * ptr()
	{
		return (uint8_t*)cstring::ptr();
	}
	const uint8_t * ptr() const
	{
		return cstring::ptr();
	}
	
	void resize(uint32_t newlen);
	
	const char * ptr_withnul() const { return (char*)ptr(); }
	
	void init_from(const char * str)
	{
		if (!str) str = "";
		init_from(arrayview<byte>((uint8_t*)str, strlen(str)));
	}
	void init_from(arrayview<byte> data);
	void init_from_large(const uint8_t * str, uint32_t len);
	void init_from(const cstring& other)
	{
		if (other.inlined())
			memcpy(this, &other, sizeof(*this));
		else
			init_from_large(other.m_data, other.m_len);
	}
	void init_from(string&& other)
	{
		memcpy(this, &other, sizeof(*this));
		other.init_empty();
	}
	
	void reinit_from(const char * str)
	{
		if (!str) str = "";
		reinit_from(arrayview<byte>((uint8_t*)str, strlen(str)));
	}
	void reinit_from(arrayview<byte> data);
	void reinit_from(cstring other)
	{
		reinit_from(other.bytes());
	}
	void reinit_from(string&& other)
	{
		release();
		memcpy(this, &other, sizeof(*this));
		other.m_inline_len = 0;
	}
	
	void release()
	{
		if (!inlined()) free(m_data);
	}
	
	void append(arrayview<uint8_t> newdat)
	{
		// cache these four, for performance
		uint8_t* p1 = ptr();
		const uint8_t* p2 = newdat.ptr();
		uint32_t l1 = length();
		uint32_t l2 = newdat.size();
		
		if (UNLIKELY(p2 >= p1 && p2 < p1+l1))
		{
			uint32_t offset = p2-p1; // technically UB to do this after resizing; nobody's gonna care, but might as well
			resize(l1+l2);
			p1 = ptr();
			memcpy(p1+l1, p1+offset, l2);
		}
		else
		{
			resize(l1+l2);
			memcpy(ptr()+l1, p2, l2);
		}
	}
	
	void append(uint8_t newch)
	{
		uint32_t oldlength = length();
		resize(oldlength + 1);
		ptr()[oldlength] = newch;
	}
	
	void replace_set(uint32_t pos, uint32_t len, cstring newdat);
	
public:
	//Resizes the string to a suitable size, then allows the caller to fill it in with whatever. Initial contents are undefined.
	//The returned pointer may only be used until the first subsequent use of the string, including read-only operations.
	arrayvieww<byte> construct(uint32_t len)
	{
		resize(len);
		return arrayvieww<byte>(ptr(), len);
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
	
	// for other integer types, fail (other other integer types will be ambiguous)
	string& operator+=(int right) = delete;
	string& operator+=(unsigned right) = delete;
	
	
	string() : cstring(noinit()) { init_empty(); }
	string(const string& other) : cstring(noinit()) { init_from(other); }
	string(string&& other) : cstring(noinit()) { init_from(std::move(other)); }
	string(const char * str) : cstring(noinit()) { init_from(str); }
	string(cstring other) : cstring(noinit()) { init_from(other); }
	string(arrayview<uint8_t> bytes) : cstring(noinit()) { init_from(bytes); }
	string(arrayview<char> chars) : cstring(noinit())
	{
		init_from(chars.reinterpret<uint8_t>());
	}
	string(array<uint8_t>&& bytes);
	string(nullptr_t) { init_empty(); }
	string& operator=(const string& other) { reinit_from(other); return *this; }
	string& operator=(const cstring& other) { reinit_from(other); return *this; }
	string& operator=(string&& other) { reinit_from(std::move(other)); return *this; }
	string& operator=(const char * str) { reinit_from(str); return *this; }
	string& operator=(nullptr_t) { release(); init_empty(); return *this; }
	~string() { release(); }
	
	explicit operator bool() const { return length() != 0; }
	operator const char * () const { return ptr_withnul(); }
	
	//Reading the NUL terminator is fine. Writing the terminator, or poking beyond the NUL, is undefined behavior.
	uint8_t& operator[](int index) { return ptr()[index]; }
	uint8_t operator[](int index) const { return ptr()[index]; }
	
	//Takes ownership of the given pointer. Will free() it when done.
	static string create_usurp(char * str);
	static string create_usurp(array<byte>&& in) { return string(std::move(in)); }
	
	//Returns a string containing a single NUL.
	static cstring nul() { return arrayview<byte>((byte*)"", 1); }
	
	//Returns U+FFFD for UTF16-reserved inputs. 0 yields a NUL byte.
	static string codepoint(uint32_t cp);
	
	//3-way comparison. If a comes first, return value is negative; if equal, zero; if b comes first, positive.
	//Comparison is bytewise. End goes before NUL, so the empty string comes before everything else.
	//The return value is not guaranteed to be in [-1..1]. It's not even guaranteed to fit in anything smaller than int.
	static int compare3(cstring a, cstring b);
	//Like the above, but 0x61-0x7A (a-z) are treated as 0x41-0x5A (A-Z).
	//If the strings are case-insensitively equal, uppercase goes first. If equal, they're equal.
	static int icompare3(cstring a, cstring b);
	static bool less(cstring a, cstring b) { return compare3(a, b) < 0; }
	static bool iless(cstring a, cstring b) { return icompare3(a, b) < 0; }
	
	//Natural comparison; "8" < "10". Other than that, same as above.
	//Exact rules:
	//  Strings are compared component by component. A component is either a digit sequence, or a non-digit. 8 < 10, 2 = 02.
	//  - and . are not part of the digit sequence. -1 < -2, 1.2 < 1.03.
	//  If the strings are otherwise equal, repeat the comparison, but with 2 < 02. If still equal, let A < a.
	//Correct sorting is a1 a2 a02 a2a a2a1 a02a2 a2a3 a2b a02b A3A A3a a3A a3a A03A A03a a03A a03a a10 a11 aa
	static int natcompare3(cstring a, cstring b) { return string::natcompare3(a, b, false); }
	static int inatcompare3(cstring a, cstring b) { return string::natcompare3(a, b, true); }
	static bool natless(cstring a, cstring b) { return natcompare3(a, b) < 0; }
	static bool inatless(cstring a, cstring b) { return inatcompare3(a, b) < 0; }
private:
	static int natcompare3(cstring a, cstring b, bool case_insensitive);
public:
};

#undef OBJ_SIZE
#undef MAX_INLINE


inline bool operator==(cstring left,      const char * right) { return left.bytes() == arrayview<byte>((uint8_t*)right,strlen(right)); }
inline bool operator==(cstring left,      cstring right     ) { return left.bytes() == right.bytes(); }
inline bool operator==(const char * left, cstring right     ) { return operator==(right, left); }
inline bool operator!=(cstring left,      const char * right) { return !operator==(left, right); }
inline bool operator!=(cstring left,      cstring right     ) { return !operator==(left, right); }
inline bool operator!=(const char * left, cstring right     ) { return !operator==(left, right); }

bool operator<(cstring left,      const char * right) = delete;
bool operator<(cstring left,      cstring right     ) = delete;
bool operator<(const char * left, cstring right     ) = delete;

inline string operator+(cstring left,      cstring right     ) { string ret=left; ret+=right; return ret; }
inline string operator+(cstring left,      const char * right) { string ret=left; ret+=right; return ret; }
inline string operator+(string&& left,     cstring right     ) { left+=right; return std::move(left); }
inline string operator+(string&& left,     const char * right) { left+=right; return std::move(left); }
inline string operator+(const char * left, cstring right     ) { string ret=left; ret+=right; return ret; }

//inline explicit string operator+(string&& left, char right) { left+=right; return std::move(left); }
//inline explicit string operator+(cstring left, char right) { string ret=left; ret+=right; return ret; }
//inline explicit string operator+(char left, cstring right) { string ret; ret[0]=left; ret+=right; return ret; }
inline string operator+(string&& left, char right) = delete;
inline string operator+(cstring left, char right) = delete;
inline string operator+(char left, cstring right) = delete;

inline string cstring::lower() const
{
	string ret = *this;
	uint8_t * chars = ret.ptr();
	for (size_t i=0;i<length();i++) chars[i] = tolower(chars[i]);
	return ret;
}

inline string cstring::upper() const
{
	string ret = *this;
	uint8_t * chars = ret.ptr();
	for (size_t i=0;i<length();i++) chars[i] = toupper(chars[i]);
	return ret;
}

//Checks if needle is one of the 'separator'-separated words in the haystack. The needle may not contain 'separator' or be empty.
//For example, haystack "GL_EXT_FOO GL_EXT_BAR GL_EXT_QUUX" (with space as separator) contains needles
// 'GL_EXT_FOO', 'GL_EXT_BAR' and 'GL_EXT_QUUX', but not 'GL_EXT_QUU'.
bool strtoken(const char * haystack, const char * needle, char separator);

static inline int isualpha(int c) { return c=='_' || isalpha(c); }
static inline int isualnum(int c) { return c=='_' || isalnum(c); }
