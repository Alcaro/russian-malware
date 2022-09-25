#include "string.h"
#include "test.h"
#include "simd.h"
#include "endian.h"

extern const uint8_t char_props[256] = {
	//x0   x1   x2   x3   x4   x5   x6   x7   x8   x9   xA   xB   xC   xD   xE   xF
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x80,0x00,0x00,0x80,0x00,0x00, // 0x
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // 1x
	0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // 2x  !"#$%&'()*+,-./
	0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x00,0x00,0x00,0x00,0x00,0x00, // 3x 0123456789:;<=>?
	0x00,0x23,0x23,0x23,0x23,0x23,0x23,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22, // 4x @ABCDEFGHIJKLMNO
	0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x00,0x00,0x00,0x00,0x00, // 5x PQRSTUVWXYZ[\]^_
	0x00,0x25,0x25,0x25,0x25,0x25,0x25,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24, // 6x `abcdefghijklmno
	0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x00,0x00,0x00,0x00,0x00, // 7x pqrstuvwxyz{|}~
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 8x-9x
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // Ax-Bx
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // Cx-Dx
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // Ex-Fx
};

void string::resize(size_t newlen)
{
	switch (!inlined()<<1 | (newlen>max_inline()))
	{
	case 0: // small->small
		{
			m_inline[newlen] = '\0';
			m_inline_len_w() = max_inline()-newlen;
		}
		break;
	case 1: // small->big
		{
			uint8_t* newptr = xmalloc(bytes_for(newlen));
			memcpy(newptr, m_inline, max_inline());
			newptr[newlen] = '\0';
			m_data = newptr;
			m_len = newlen;
			m_nul = true;
			
			m_inline_len_w() = -1;
		}
		break;
	case 2: // big->small
		{
			uint8_t* oldptr = m_data;
			memcpy(m_inline, oldptr, newlen);
			free(oldptr);
			m_inline[newlen] = '\0';
			m_inline_len_w() = max_inline()-newlen;
		}
		break;
	case 3: // big->big
		{
			if (bytes_for(newlen) != bytes_for(m_len))
				m_data = xrealloc(m_data, bytes_for(newlen));
			m_data[newlen] = '\0';
			m_len = newlen;
		}
		break;
	}
}

void string::init_from_outline(const uint8_t * str, size_t len)
{
	if (len <= max_inline())
	{
		memcpy(m_inline, str, len);
		m_inline[len] = '\0';
		m_inline_len_w() = max_inline()-len;
	}
	else
	{
		init_from_large(str, len);
	}
}

void string::init_from_large(const uint8_t * str, size_t len)
{
	m_inline_len_w() = -1;
	
	m_data = xmalloc(bytes_for(len));
	memcpy(m_data, str, len);
	m_data[len]='\0';
	
	m_len = len;
	m_nul = true;
}

void string::init_from(const cstring& other)
{
	if (other.inlined())
		memcpy((void*)this, (void*)&other, sizeof(*this));
	else
		init_from_large(other.m_data, other.m_len);
}

void string::reinit_from(arrayview<uint8_t> data)
{
	const uint8_t * str = data.ptr();
	size_t len = data.size();
	
	if (str >= this->ptr() && str <= this->ptr()+this->length())
	{
		if (str == this->ptr() && len == this->length()) return;
		
		memmove(this->ptr(), str, len);
		resize(len);
	}
	else
	{
		deinit();
		init_from(data);
	}
}

void string::append(arrayview<uint8_t> newdat)
{
	// cache these four, for performance
	uint8_t* p1 = ptr();
	const uint8_t* p2 = newdat.ptr();
	uint32_t l1 = length();
	uint32_t l2 = newdat.size();
	
	if (UNLIKELY(p2 >= p1 && p2 < p1+l1))
	{
		uint32_t offset = p2-p1;
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

string string::create_usurp(char * str)
{
	cstring tmp(str);
	string ret;
	memcpy((void*)&ret, (void*)&tmp, sizeof(string));
	if (tmp.inlined()) free(str);
	else ret.m_data = xrealloc(ret.m_data, bytes_for(ret.m_len));
	return ret;
}

string::string(array<uint8_t>&& bytes) : cstrnul(noinit())
{
	cstring tmp(bytes);
	memcpy((void*)this, (void*)&tmp, sizeof(string));
	
	uint8_t* ptr = bytes.release().ptr();
	if (tmp.inlined()) free(ptr);
}

size_t cstring::indexof(cstring other, size_t start) const
{
	uint8_t* ptr = (uint8_t*)memmem(this->ptr()+start, this->length()-start, other.ptr(), other.length());
	if (ptr) return ptr - this->ptr();
	else return (size_t)-1;
}
size_t cstring::lastindexof(cstring other) const
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
bool cstring::startswith(cstring other) const
{
	if (other.length() > this->length()) return false;
	return (!memcmp(this->ptr(), other.ptr(), other.length()));
}
bool cstring::endswith(cstring other) const
{
	if (other.length() > this->length()) return false;
	return (!memcmp(this->ptr()+this->length()-other.length(), other.ptr(), other.length()));
}

size_t cstring::iindexof(cstring other, size_t start) const
{
	if (start+other.length() > this->length()) return -1;
	const char* a = (char*)this->ptr();
	const char* b = (char*)other.ptr();
	while (start <= this->length()-other.length())
	{
		size_t i;
		for (i=0;i<other.length();i++)
		{
			if (tolower(a[start+i]) != tolower(b[i])) break;
		}
		if (i==other.length()) return start;
		start++;
	}
	return -1;
}
size_t cstring::ilastindexof(cstring other) const
{
	if (other.length() > this->length()) return -1;
	const char* a = (char*)this->ptr();
	const char* b = (char*)other.ptr();
	
	size_t start=this->length()-other.length();
	while (true)
	{
		size_t i;
		for (i=0;i<other.length();i++)
		{
			if (tolower(a[start+i]) != tolower(b[i])) break;
		}
		if (i==other.length()) return start;
		
		if (start == 0) return -1;
		start--;
	}
}
bool cstring::icontains(cstring other) const
{
	return iindexof(other) != (size_t)-1;
}
bool cstring::istartswith(cstring other) const
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
bool cstring::iendswith(cstring other) const
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
bool cstring::iequals(cstring other) const
{
	return (this->length() == other.length() && this->istartswith(other));
}

string cstring::replace(cstring in, cstring out) const
{
	size_t outlen = length();
	
	if (in.length() != out.length())
	{
		const uint8_t* haystack = ptr();
		const uint8_t* haystackend = ptr()+length();
		while (true)
		{
			haystack = (uint8_t*)memmem(haystack, haystackend-haystack, in.ptr(), in.length());
			if (!haystack) break;
			
			haystack += in.length();
			outlen += out.length(); // outlen-inlen is type uint32 - bad idea
			outlen -= in.length();
		}
	}
	
	string ret;
	uint8_t* retptr = ret.construct(outlen).ptr();
	
	const uint8_t* prev = ptr();
	const uint8_t* myend = ptr()+length();
	while (true)
	{
		const uint8_t* match = (uint8_t*)memmem(prev, myend-prev, in.ptr(), in.length());
		if (!match) break;
		
		memcpy(retptr, prev, match-prev);
		retptr += match-prev;
		prev = match + in.length();
		
		memcpy(retptr, out.ptr(), out.length());
		retptr += out.length();
	}
	memcpy(retptr, prev, myend-prev);
	
	return ret;
}


array<cstring> cstring::csplit(cstring sep, size_t limit) const
{
	array<cstring> ret;
	const uint8_t * data = ptr();
	const uint8_t * dataend = ptr()+length();
	
	while (ret.size() < limit)
	{
		const uint8_t * next = (uint8_t*)memmem(data, dataend-data, sep.ptr(), sep.length());
		if (!next) break;
		ret.append(arrayview<uint8_t>(data, next-data));
		data = next+sep.length();
	}
	ret.append(arrayview<uint8_t>(data, dataend-data));
	return ret;
}

array<cstring> cstring::crsplit(cstring sep, size_t limit) const
{
	array<cstring> ret;
	const uint8_t * datastart = ptr();
	const uint8_t * data = ptr()+length();
	
	const uint8_t * sepp = sep.ptr();
	size_t sepl = sep.length();
	
	while (ret.size() < limit)
	{
		if (datastart+sepl > data) break;
		const uint8_t * next = data-sepl;
		while (memcmp(next, sepp, sepl) != 0)
		{
			if (datastart==next) goto done;
			next--;
		}
		ret.insert(0, arrayview<uint8_t>(next+sepl, data-(next+sepl)));
		data = next;
	}
done:
	ret.insert(0, arrayview<uint8_t>(datastart, data-datastart));
	return ret;
}


array<cstring> cstring::cspliti(cstring sep, size_t limit) const
{
	array<cstring> ret;
	const uint8_t * data = ptr();
	const uint8_t * dataend = ptr()+length();
	
	while (ret.size() < limit)
	{
		const uint8_t * next = (uint8_t*)memmem(data, dataend-data, sep.ptr(), sep.length());
		if (!next) break;
		ret.append(arrayview<uint8_t>(data, next-data+sep.length()));
		data = next+sep.length();
	}
	if (dataend != data)
		ret.append(arrayview<uint8_t>(data, dataend-data));
	return ret;
}

//TODO: this function is pretty messy
array<cstring> cstring::crspliti(cstring sep, size_t limit) const
{
	array<cstring> ret;
	const uint8_t * datastart = ptr();
	const uint8_t * data = ptr()+length();
	
	const uint8_t * sepp = sep.ptr();
	size_t sepl = sep.length();
	
	size_t outlen = 0;
	while (outlen < limit)
	{
		if (datastart+sepl > data) break;
		const uint8_t * next = data-sepl;
		while (memcmp(next, sepp, sepl) != 0)
		{
			if (datastart==next) goto done;
			next--;
		}
		size_t len = data - (next+sepl) + (data == ptr()+length() ? 0 : sepl);
		if (len)
			ret.insert(0, arrayview<uint8_t>(next+sepl, len));
		outlen++;
		data = next;
	}
done:
	size_t len = data - datastart + (data == ptr()+length() ? 0 : sepl);
	if (len)
		ret.insert(0, arrayview<uint8_t>(datastart, len));
	return ret;
}


//array<cstring> cstring::csplitw(size_t limit) const
//{
//	array<cstring> ret;
//	const uint8_t * data = ptr();
//	const uint8_t * dataend = ptr()+length();
//	
//	while (ret.size() < limit)
//	{
//		const uint8_t * next = data;
//		while (next < dataend && !isspace(*next)) next++;
//		if (next == dataend) break;
//		ret.append(arrayview<uint8_t>(data, next-data));
//		data = next+1;
//	}
//	ret.append(arrayview<uint8_t>(data, dataend-data));
//	return ret;
//}
//
//array<cstring> cstring::crsplitw(size_t limit) const
//{
//	array<cstring> ret;
//	const uint8_t * datastart = ptr();
//	const uint8_t * data = ptr()+length();
//	
//	while (ret.size() < limit)
//	{
//		if (datastart+1 > data) break;
//		const uint8_t * next = data-1;
//		while (!isspace(*next))
//		{
//			if (datastart==next) goto done;
//			next--;
//		}
//		ret.insert(0, arrayview<uint8_t>(next+1, data-(next+1)));
//		data = next;
//	}
//done:
//	ret.insert(0, arrayview<uint8_t>(datastart, data-datastart));
//	return ret;
//}

array<cstring> cstring::csplit(bool(*find)(const uint8_t * start, const uint8_t * & at, const uint8_t * & end), size_t limit) const
{
	array<cstring> ret;
	
	const uint8_t * data = ptr();
	const uint8_t * dataend = ptr()+length();
	
	const uint8_t * gstart = data;
	const uint8_t * at = gstart;
	
	while (ret.size() < limit && at < dataend)
	{
		const uint8_t * mat = at;
		const uint8_t * mend = dataend;
		if (!find(data, mat, mend) || (gstart == mat && mat == mend))
		{
			at++;
			continue;
		}
		
		ret.append(arrayview<uint8_t>(gstart, mat-gstart));
		gstart = mend;
		at = gstart;
	}
	ret.append(arrayview<uint8_t>(gstart, dataend-gstart));
	return ret;
}

#ifdef __SSE2__
bool operator==(const cstring& left, const cstring& right)
{
	static_assert(sizeof(cstring) == 16);
	if (left.inlined())
	{
		if (left.m_inline_len() != right.m_inline_len()) return false; // if right isn't inlined, m_inline_len is -1 aka not equal
		__m128i a = _mm_loadu_si128((__m128i*)&left);
		__m128i b = _mm_loadu_si128((__m128i*)&right);
		int eq = _mm_movemask_epi8(_mm_cmpeq_epi8(a, b));
		return ((((~eq) << left.m_inline_len()) & 0xFFFF) == 0);
	}
	else
	{
		// this is basically a memcmp, but I know size >= 16, so I can jump straight to the SIMD loop
		
		auto memeq16 = [](const uint8_t * ap, const uint8_t * bp)
		{
			__m128i a = _mm_loadu_si128((__m128i*)ap);
			__m128i b = _mm_loadu_si128((__m128i*)bp);
			return (_mm_movemask_epi8(_mm_cmpeq_epi8(a, b)) == 0xFFFF);
		};
		if (right.inlined() || left.m_len != right.m_len) return false;
		
		const uint8_t * leftp = left.m_data;
		const uint8_t * rightp = right.m_data;
		size_t stop = left.m_len - 16;
		
		size_t iter = 0;
		do { // entering this loop at all is pointless if len == 16, but do-while is a few bytes smaller than while
			if (!memeq16(leftp+iter, rightp+iter)) return false;
			iter += 16;
		} while (iter < stop);
		
		return (memeq16(leftp+stop, rightp+stop));
	}
}
#endif

int string::compare3(cstring a, cstring b)
{
	size_t cmplen = min(a.length(), b.length());
	int ret = memcmp(a.bytes().ptr(), b.bytes().ptr(), cmplen);
	
	if (ret) return ret;
	else return a.length() - b.length();
}

int string::icompare3(cstring a, cstring b)
{
	int ret_i = a.length() - b.length();
	
	size_t cmplen = min(a.length(), b.length());
	const uint8_t* ab = a.bytes().ptr();
	const uint8_t* bb = b.bytes().ptr();
	for (size_t n=0; n<cmplen; n++)
	{
		if (ab[n] != bb[n])
		{
			if (ret_i == 0) ret_i = ab[n] - bb[n];
			
			uint8_t ac = toupper(ab[n]);
			uint8_t bc = toupper(bb[n]);
			if (ac != bc) return ac - bc;
		}
	}
	
	return ret_i;
}

int string::natcompare3(cstring a, cstring b, bool case_insensitive)
{
	const uint8_t* ab = a.bytes().ptr();
	const uint8_t* bb = b.bytes().ptr();
	const uint8_t* abe = ab + a.length();
	const uint8_t* bbe = bb + b.length();
	
	int ret_zero = 0; // negative = 'a' is first
	int ret_case = 0;
	
	while (ab < abe && bb < bbe)
	{
		if (isdigit(*ab) && isdigit(*bb))
		{
			while (ab < abe && *ab == '0' &&
			       bb < bbe && *bb == '0')
			{
				ab++;
				bb++;
			}
			while (ab < abe && *ab == '0')
			{
				if (!ret_zero) ret_zero = 1;
				ab++;
			}
			while (bb < bbe && *bb == '0')
			{
				if (!ret_zero) ret_zero = -1;
				bb++;
			}
			
			const uint8_t* abs = ab;
			const uint8_t* bbs = bb;
			
			while (ab < abe && isdigit(*ab)) ab++;
			while (bb < bbe && isdigit(*bb)) bb++;
			
			if (ab-abs > bb-bbs) return 1; // a's digit sequence is longer -> a is greater
			if (ab-abs < bb-bbs) return -1;
			// same length - compare char by char (no leading zero, same length, so first difference wins)
			while (abs < ab)
			{
				if (*abs != *bbs) return *abs - *bbs;
				abs++;
				bbs++;
			}
		}
		else
		{
			uint8_t ac = *ab;
			uint8_t bc = *bb;
			if (ac != bc)
			{
				if (ret_case == 0) ret_case = ac - bc;
				if (case_insensitive) ac = toupper(ac);
				if (case_insensitive) bc = toupper(bc);
				if (ac != bc) return ac - bc;
			}
			ab++;
			bb++;
		}
	}
	
	if (bb < bbe) return -1;
	if (ab < abe) return 1;
	
	if (ret_zero) return ret_zero;
	if (ret_case) return ret_case;
	return 0;
}

string cstring::upper() const
{
	string ret = *this;
	bytesw by = ret.bytes();
	for (size_t i=0;i<by.size();i++) by[i] = toupper(by[i]);
	return ret;
}

string cstring::lower() const
{
	string ret = *this;
	bytesw by = ret.bytes();
	for (size_t i=0;i<by.size();i++) by[i] = tolower(by[i]);
	return ret;
}

cstring cstring::trim() const
{
	const uint8_t * chars = ptr();
	int start = 0;
	int end = length();
	while (end > start && isspace(chars[end-1])) end--;
	while (start < end && isspace(chars[start])) start++;
	return substr(start, end);
}

bool cstring::contains_nul() const
{
#ifdef __SSE2__
	static_assert(sizeof(cstring) == 16);
	if (inlined())
	{
		int iszero = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128((__m128i*)this), _mm_setzero_si128()));
		// m_inline[0] becomes the 0001 bit, [1] -> 0002, etc
		// m_inline_len is number of bytes at the end of the object we don't care about, not counting the NUL terminator
		// iszero << m_inline_len puts the NUL at the 0x8000 bit; if anything lower is also true, that's a NUL in the input
		return ((iszero << m_inline_len()) & 0x7FFF);
	}
	else
	{
		// similar design to operator==(cstring,cstring)
		
		auto hasnul16 = [](const uint8_t * ap)
		{
			__m128i a = _mm_loadu_si128((__m128i*)ap);
			return (_mm_movemask_epi8(_mm_cmpeq_epi8(a, _mm_setzero_si128())) != 0x0000);
		};
		
		const uint8_t * ptr = m_data;
		size_t stop = m_len - 16;
		
		size_t iter = 0;
		do {
			if (hasnul16(ptr+iter)) return true;
			iter += 16;
		} while (iter < stop);
		
		return (hasnul16(ptr+stop));
	}
#else
	return memchr(ptr(), '\0', length());
#endif
}

size_t string::codepoint(uint8_t* out, uint32_t cp)
{
	if (LIKELY(cp <= 0x7F))
	{
		out[0] = cp;
		return 1;
	}
	
	if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD; // curse utf16 forever
	if (cp > 0x10FFFF) cp = 0xFFFD;
	
	// some bit magic on the codepoint / utf8 representation, smaller and faster than more obvious approaches
	uint32_t bits = cp;
	bits = (bits&0x00fff000)<<4 | (bits&0x00000fff);
	bits = (bits&0x0fc00fc0)<<2 | (bits&0x003f003f);
	
	// check how many bytes that'd need, fill in the mandatory 1 bits, and write it out
	if (bits <= 0x00001fff) // cp <= 0x7ff
	{
		writeu_be32(out, (bits|0xC080)<<16);
		return 2;
	}
	else if (bits <= 0x000fffff) // cp <= 0xffff
	{
		writeu_be32(out, (bits|0xE08080)<<8);
		return 3;
	}
	else
	{
		writeu_be32(out, bits|0xF0808080);
		return 4;
	}
}

string string::codepoint(uint32_t cp)
{
	string ret;
	ret.resize(string::codepoint(ret.construct(4).ptr(), cp));
	return ret;
}

bool cstring::isutf8() const
{
	const uint8_t * bytes = ptr();
	const uint8_t * end = bytes + length();
	
	while (bytes < end)
	{
		uint8_t head = *bytes++;
		if (LIKELY(head < 0x80)) continue;
		
		uint32_t bits = head;
		while (bytes != end && (*bytes&0xC0) == 0x80)
		{
			// if there are four or more continuations, the head will be shifted out of bits
			// it'll end up between 0x80808080 and 0xBFBFBFBF, which doesn't match any approved range below
			bits = bits<<8 | *bytes++;
		}
		
		if (bits >= 0xC280 && bits <= 0xDFBF) {}
		else if (bits >= 0xE0A080 && (bits^0x020000) <= 0xEF9FBF) {} // extra xor to catch utf16
		else if (bits >= 0xF0908080 && bits <= 0xF48FBFBF) {}
		else return false;
	}
	
	return true;
}

uint32_t cstring::codepoint_at(uint32_t& idx) const
{
	const uint8_t * bytes = ptr();
	uint32_t remaining = length()-idx;
	
	uint8_t head = bytes[idx];
	if (LIKELY(head < 0x80)) { idx++; return head; }
	
	uint32_t bits = 0; // same general idea as isutf8, but a little more careful
	uint8_t iter = head; // valid codepoint followed by continuation should report valid codepoint
	int n = 0;
	do {
		bits = bits<<8 | bytes[idx + n];
		n++;
		iter <<= 1;
	} while ((iter & 0x80) && --remaining && (bytes[idx+n]&0xC0) == 0x80);
	
	if (bits >= 0xC280 && bits <= 0xDFBF) {}
	else if (bits >= 0xE0A080 && (bits^0x020000) <= 0xEF9FBF) bits &= ~0x200000; // otherwise that bit becomes 0x8000 in codepoint
	else if (bits >= 0xF0908080 && bits <= 0xF48FBFBF) {} // for other top bytes, the fixed bits are zero or masked off
	else
	{
		idx++;
		return 0xDC00 | head;
	}
	
	idx += n;
	bits = (bits&0x003f003f) | (bits&0x0f003f00)>>2;
	bits = (bits&0x00000fff) | (bits&0x0fff0000)>>4;
	return bits;
}

bool cstring::matches_glob(cstring pat, bool case_insensitive) const
{
	const uint8_t * s = ptr();
	const uint8_t * p = pat.ptr();
	const uint8_t * se = s + length();
	const uint8_t * pe = p + pat.length();
	
	const uint8_t * sp = se; // weirdo trick to not need a special case for the part before first asterisk
	const uint8_t * pp = p;
	if (p == pe)
		return (s == se);
	while (s < se)
	{
		if (p < pe && *p == '*')
		{
			p++;
			if (p == pe) return true;
			pp = p;
			sp = s+1;
		}
		else if (p < pe && s < se && (*p == *s || *p == '?' || (case_insensitive && tolower(*p) == tolower(*s))))
		{
			p++;
			s++;
		}
		else
		{
			p = pp;
			s = sp;
			sp++;
		}
	}
	while (p < pe && *p == '*') p++;
	return (p == pe);
}


string cstring::leftPad (size_t len, uint8_t ch) const {
	if (len >= length()) return *this;
	len -= length();
	
	array<uint8_t> pad;
	pad.resize(len);
	memset(pad.ptr(), ch, len);
	return cstring(pad) + *this;
}



static bool test_isspace(uint8_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static bool test_isdigit(uint8_t c) { return c >= '0' && c <= '9'; }
static bool test_islower(uint8_t c) { return c >= 'a' && c <= 'z'; }
static bool test_isupper(uint8_t c) { return c >= 'A' && c <= 'Z'; }
static bool test_isalpha(uint8_t c) { return test_islower(c) || test_isupper(c); }
static bool test_isalnum(uint8_t c) { return test_isalpha(c) || test_isdigit(c); }
static bool test_isxdigit(uint8_t c) { return test_isdigit(c) || ((c&~0x20) >= 'A' && (c&~0x20) <= 'F'); }
static uint8_t test_tolower(uint8_t c) { if (test_isupper(c)) return c+0x20; else return c; }
static uint8_t test_toupper(uint8_t c) { if (test_islower(c)) return c-0x20; else return c; }

test("ctype", "", "string")
{
	for (int i=0;i<=255;i++)
	{
		testctx(tostring(i)) {
			assert_eq(isspace(i), test_isspace(i));
			assert_eq(isdigit(i), test_isdigit(i));
			assert_eq(isalpha(i), test_isalpha(i));
			assert_eq(islower(i), test_islower(i));
			assert_eq(isupper(i), test_isupper(i));
			assert_eq(isalnum(i), test_isalnum(i));
			assert_eq(isxdigit(i), test_isxdigit(i));
			assert_eq(tolower(i), test_tolower(i));
			assert_eq(toupper(i), test_toupper(i));
		}
	}
}

test("strtoken", "", "string")
{
	assert(strtoken("aa", "aa", ' '));
	assert(!strtoken("aa", "a", ' '));
	assert(!strtoken("aa", "aaa", ' '));
	assert(strtoken("aa aa aa aa", "aa", ' '));
	assert(!strtoken("aa aa aa aa", "a", ' '));
	assert(!strtoken("aa aa aa aa", "aaa", ' '));
	assert(!strtoken("12345", "1234", ' '));
	assert(!strtoken("12345", "2345", ' '));
	assert(!strtoken("12345", "234", ' '));
	assert(strtoken("1234 123456 2345 123456 0123456 012345 12345 12345", "12345", ' '));
	assert(strtoken("a b b", "a", ' '));
	assert(strtoken("b a b", "a", ' '));
	assert(strtoken("b b a", "a", ' '));
	
	//blank needle not allowed
	//assert(!strtoken("a b c", "", ' '));
	//assert(strtoken(" a b c", "", ' '));
	//assert(strtoken("a  b c", "", ' '));
	//assert(strtoken("a b c ", "", ' '));
	//assert(strtoken("", "", ' '));
	
	assert(strtoken("aa", "aa", ','));
	assert(!strtoken("aa", "a", ','));
	assert(!strtoken("aa", "aaa", ','));
	assert(strtoken("aa,aa,aa,aa", "aa", ','));
	assert(!strtoken("aa,aa,aa,aa", "a", ','));
	assert(!strtoken("aa,aa,aa,aa", "aaa", ','));
	assert(!strtoken("12345", "1234", ','));
	assert(!strtoken("12345", "2345", ','));
	assert(!strtoken("12345", "234", ','));
	assert(strtoken("1234,123456,2345,123456,0123456,012345,12345,12345", "12345", ','));
	assert(strtoken("a,b,b", "a", ','));
	assert(strtoken("b,a,b", "a", ','));
	assert(strtoken("b,b,a", "a", ','));
	
	//assert(!strtoken("a,b,c", "", ','));
	//assert(strtoken(",a,b,c", "", ','));
	//assert(strtoken("a,,b,c", "", ','));
	//assert(strtoken("a,b,c,", "", ','));
	//assert(strtoken("", "", ','));
}

test("string base", "array,memeq", "string")
{
	{
		const char * g = "hi";
		
		string a = g;
		a += '!';
		string b = a;
		assert_eq(b, "hi!");
		a += '!';
		assert_eq(a, "hi!!");
		assert_eq(b, "hi!");
		a = b;
		assert_eq(a, "hi!");
		assert_eq(b, "hi!");
		
		assert_eq(a.length(), 3);
		assert_eq((char)a[2], '!');
		
		string c;
		cstring d;
		
		assert_eq(c, "");
		assert_eq(d, "");
		
		assert(!c);
		assert(!d);
	}
	
	{
		uint8_t buf1[65] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
		uint8_t buf2[65] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
		for (size_t len=0;len<=34;len++)
		{
			assert_eq(cstring(bytesr(buf1, len)), cstring(bytesr(buf2, len)));
			assert_ne(cstring(bytesr(buf1, len)), cstring(bytesr(buf2, len+1)));
			for (size_t pos=0;pos<len;pos++)
			{
				buf2[pos] = 'b';
				assert_ne(cstring(bytesr(buf1, len)), cstring(bytesr(buf2, len)));
				buf2[pos] = 'a';
			}
		}
	}
	
	{
		//ensure it works properly when going across the inline-outline border
		string a = "123456789012345";
		a += "678";
		assert_eq(a, "123456789012345678");
		a += (const char*)a;
		string b = a;
		assert_eq(a, "123456789012345678123456789012345678");
		assert_eq(a.substr(1,3), "23");
		assert_eq(b, "123456789012345678123456789012345678");
		assert_eq(a.substr(1,21), "23456789012345678123");
		assert_eq(a.substr(1,~1), "2345678901234567812345678901234567");
		assert_eq(a.substr(2,2), "");
		assert_eq(a.substr(22,22), "");
	}
	
	{
		string a = "12345678";
		a += a;
		a += a;
		string b = a;
		a = "";
		assert_eq(b, "12345678123456781234567812345678");
	}
	
	{
		string a = "1abc1de1fgh1";
		assert_eq(a.replace("1", ""), "abcdefgh");
		assert_eq(a.replace("1", "@"), "@abc@de@fgh@");
		assert_eq(a.replace("1", "@@"), "@@abc@@de@@fgh@@");
		assert_eq(cstring("aaaaaaaaa").replace("aa","aba"), "abaabaabaabaa");
	}
	
	{
		string a = "abcdefghijklmnopqrstuvwxyz";
		string b = a;
		a += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
		assert_eq(a, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
		assert_eq(b, "abcdefghijklmnopqrstuvwxyz");
		a.operator=(a);
		a = a.substr(0, ~1);
	}
	
	{
		string a = "aaaaaaaaaaaaaaaa";
		a[0] = 'b';
		assert_eq(a, "baaaaaaaaaaaaaaa");
	}
	
	{
		string a = "abcdefghijklmnopqrstuvwxyz";
		cstring b = a;
		a = b;
		assert_eq(a, "abcdefghijklmnopqrstuvwxyz");
		b = a;
		a = b.substr(1, ~1);
		assert_eq(a, "bcdefghijklmnopqrstuvwxy");
	}
	
	{
		arrayview<uint8_t> a((uint8_t*)"123", 3);
		string b = "["+string(a)+"]";
		string c = "["+cstring(a)+"]";
		assert_eq(b, "[123]");
		assert_eq(c, "[123]");
		
		assert(b == "[123]"); // test the ==literal optimization
		assert(c == "[123]");
	}
	
	{
		string a;
		a = "192.168.0.1";
		assert_eq(a.split(".").join("/"), "192/168/0/1");
		assert_eq(a.split<1>(".").join("/"), "192/168.0.1");
		assert_eq(a.rsplit<1>(".").join("/"), "192.168.0/1");
		
		a = "baaaaaaaaaaaaaaa";
		assert_eq(a.split("a").join("."), "b...............");
		assert_eq(a.split("aa").join("."), "b.......a");
		assert_eq(a.split<1>("aa").join("."), "b.aaaaaaaaaaaaa");
		assert_eq(a.split<1>("x").join("."), "baaaaaaaaaaaaaaa");
		
		a = "aaaaaaaaaaaaaaab";
		assert_eq(a.split("a").join("."), "...............b");
		assert_eq(a.split("aa").join("."), ".......ab");
		assert_eq(a.split<1>("aa").join("."), ".aaaaaaaaaaaaab");
		assert_eq(a.split<1>("x").join("."), "aaaaaaaaaaaaaaab");
		assert_eq(a.split("b").join("."), "aaaaaaaaaaaaaaa.");
		
		a = "baaaaaaaaaaaaaaa";
		assert_eq(a.rsplit<1>("aa").join("."), "baaaaaaaaaaaaa.");
		assert_eq(a.rsplit<1>("x").join("."), "baaaaaaaaaaaaaaa");
		
		a = "aaaaaaaaaaaaaaab";
		assert_eq(a.rsplit<1>("aa").join("."), "aaaaaaaaaaaaa.b");
		assert_eq(a.rsplit<1>("x").join("."), "aaaaaaaaaaaaaaab");
		
		assert_eq(((cstring)"").split(",").size(), 1);
	}
	
	//{
	//	string a;
	//	a = "192 168 0 1";
	//	assert_eq(a.splitw().join("/"), "192/168/0/1");
	//	assert_eq(a.splitw<1>().join("/"), "192/168 0 1");
	//	assert_eq(a.rsplitw().join("/"), "192/168/0/1");
	//	assert_eq(a.rsplitw<1>().join("/"), "192 168 0/1");
	//	
	//	a = "b               ";
	//	assert_eq(a.splitw().join("."), "b...............");
	//	assert_eq(a.splitw<1>().join("."), "b.              ");
	//	
	//	a = "               b";
	//	assert_eq(a.splitw().join("."), "...............b");
	//	assert_eq(a.splitw<1>().join("."), ".              b");
	//	
	//	a = "b               ";
	//	assert_eq(a.rsplitw().join("."), "b...............");
	//	assert_eq(a.rsplitw<1>().join("."), "b              .");
	//	
	//	a = "               b";
	//	assert_eq(a.rsplitw().join("."), "...............b");
	//	assert_eq(a.rsplitw<1>().join("."), "              .b");
	//}
	
	{
		string a;
		a = "192.168.0.1";
		assert_eq(a.spliti(".").join("/"), "192./168./0./1");
		assert_eq(a.spliti<1>(".").join("/"), "192./168.0.1");
		assert_eq(a.rspliti<1>(".").join("/"), "192.168.0./1");
		
		a = "baaaaaaaaaaaaaaa";
		assert_eq(a.spliti("a").join("."), "ba.a.a.a.a.a.a.a.a.a.a.a.a.a.a");
		assert_eq(a.spliti("aa").join("."), "baa.aa.aa.aa.aa.aa.aa.a");
		assert_eq(a.spliti<1>("aa").join("."), "baa.aaaaaaaaaaaaa");
		assert_eq(a.spliti<1>("x").join("."), "baaaaaaaaaaaaaaa");
		
		a = "aaaaaaaaaaaaaaab";
		assert_eq(a.spliti("a").join("."), "a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.b");
		assert_eq(a.spliti("aa").join("."), "aa.aa.aa.aa.aa.aa.aa.ab");
		assert_eq(a.spliti<1>("aa").join("."), "aa.aaaaaaaaaaaaab");
		assert_eq(a.spliti<1>("x").join("."), "aaaaaaaaaaaaaaab");
		
		a = "baaaaaaaaaaaaaaa";
		assert_eq(a.rspliti<1>("aa").join("."), "baaaaaaaaaaaaaaa");
		assert_eq(a.rspliti<1>("x").join("."), "baaaaaaaaaaaaaaa");
		assert_eq(a.rspliti<2>("aa").join("."), "baaaaaaaaaaaaa.aa");
		
		a = "aaaaaaaaaaaaaaab";
		assert_eq(a.rspliti<1>("aa").join("."), "aaaaaaaaaaaaaaa.b");
		assert_eq(a.rspliti<1>("x").join("."), "aaaaaaaaaaaaaaab");
	}
	
	{
		cstring a = "floating munchers";
		assert_eq(a.indexof("f"), 0);
		assert_eq(a.indexof("l"), 1);
		assert_eq(a.indexof("unc"), 10);
		assert_eq(a.indexof("x"), (size_t)-1);
	}
	
	{
		cstring a = "floating munchers";
		assert(a.icontains("f"));
		assert(a.icontains("l"));
		assert(a.icontains("unc"));
		assert(a.icontains("ers"));
		assert(a.icontains("F"));
		assert(a.icontains("L"));
		assert(a.icontains("Unc"));
		assert(a.icontains("eRS"));
		assert(!a.icontains("x"));
	}
	
	{
		// ensure it chooses the correct overloads
		array<uint8_t> src = cstring("floating munchers").bytes();
		string dst = src;
		assert_eq(dst, "floating munchers");
		assert_eq(src, dst.bytes());
		dst = "";
		test_nomalloc {
			string tmp = std::move(src);
			dst = std::move(tmp);
		};
		assert_eq(dst, "floating munchers");
		assert_eq(src.size(), 0);
	}
	
	{
		cstring a(arrayview<uint8_t>((uint8_t*)"\0", 1));
		assert_eq(a.length(), 1);
		assert_eq(a[0], '\0');
		
		cstring b(arrayview<uint8_t>((uint8_t*)"\0\0\0", 3));
		assert_eq(b.length(), 3);
		assert_eq(b.replace(a, "ee"), "eeeeee");
	}
	
	{
		assert(cstring().isutf8());
		assert(cstring("abc").isutf8());
		assert(cstring(arrayview<uint8_t>((uint8_t*)"\0", 1)).isutf8());
		assert(cstring("\xC2\xA9").isutf8());
		assert(cstring("\xE2\x82\xAC").isutf8());
		assert(cstring("\xF0\x9F\x80\xB0").isutf8());
		assert(cstring("\x78\xC3\xB8\xE2\x98\x83\xF0\x9F\x92\xA9").isutf8()); // xø☃💩
		assert(cstring("\xF4\x8F\xBF\xBF").isutf8()); // U+10FFFF
		
		assert(!cstring("\xC2").isutf8()); // too few continuations
		assert(!cstring("\xE2\x82").isutf8()); // too few continuations
		assert(!cstring("\xF0\x9F\x80").isutf8()); // too few continuations
		assert(!cstring("\xBF").isutf8()); // misplaced continuation
		assert(cstring("\xED\x9F\xBF").isutf8()); // U+D7FF
		assert(!cstring("\xED\xA0\x80").isutf8()); // U+D800
		assert(!cstring("\xED\xBF\xBF").isutf8()); // U+DFFF
		assert(cstring("\xEE\x80\x80").isutf8()); // U+E000
		assert(!cstring("\xF4\x90\xC0\xC0").isutf8()); // U+110000
		
		assert(!cstring("\xC0\xA1").isutf8()); // overlong '!'
		assert(!cstring("\xC1\xBF").isutf8()); // overlong U+007F
		assert(!cstring("\xE0\x9F\xBF").isutf8()); // overlong U+07FF
		assert(!cstring("\xF0\x8F\xBF\xBF").isutf8()); // overlong U+FFFF
		
		//random mangled symbols
		assert(!cstring("\xE2\x82\x78").isutf8());
		assert(!cstring("\xE2\x78\xAC").isutf8());
		assert(!cstring("\x78\xE2\x82").isutf8());
		assert(!cstring("\x78\x82\xAC").isutf8());
		assert(!cstring("\xF0\x9F\x80\x78").isutf8());
		assert(!cstring("\x82\x2C\x63").isutf8());
		assert(!cstring("\x2C\x92\x63").isutf8());
	}
	
	{
		cstring strs[] = {
		  "a!a",
		  "aBa",
		  "aBc",
		  "aBcd",
		  "aBd",
		  "a_c",
		  "abc",
		};
		
		for (size_t a=0;a<ARRAY_SIZE(strs);a++)
		for (size_t b=0;b<ARRAY_SIZE(strs);b++)
		{
			if (a <  b) testctx(strs[a]+" < "+strs[b]) assert_lt(string::compare3(strs[a], strs[b]), 0);
			if (a == b) testctx(strs[a]+" = "+strs[b]) assert_eq(string::compare3(strs[a], strs[b]), 0);
			if (a >  b) testctx(strs[a]+" > "+strs[b]) assert_gt(string::compare3(strs[a], strs[b]), 0);
			testctx(strs[a]+" <=> "+strs[b]) assert_eq(string::less(strs[a], strs[b]), a<b);
		}
	}
	
	{
		cstring strs[] = {
		  "a!a",
		  "aBa",
		  "aBc",
		  "abc",
		  "aBcd",
		  "aBd",
		  "a_c",
		};
		
		for (size_t a=0;a<ARRAY_SIZE(strs);a++)
		for (size_t b=0;b<ARRAY_SIZE(strs);b++)
		{
			if (a <  b) testctx(strs[a]+" < "+strs[b]) assert_lt(string::icompare3(strs[a], strs[b]), 0);
			if (a == b) testctx(strs[a]+" = "+strs[b]) assert_eq(string::icompare3(strs[a], strs[b]), 0);
			if (a >  b) testctx(strs[a]+" > "+strs[b]) assert_gt(string::icompare3(strs[a], strs[b]), 0);
			testctx(strs[a]+" <=> "+strs[b]) assert_eq(string::iless(strs[a], strs[b]), a<b);
		}
	}
	
	{
		cstring strs[]    = { "fl","lo","oa","at","ti","in","ng","g "," m","mu","un","nc","ch","he","er","rs" };
		cstring strsort[] = { " m","at","ch","er","fl","g ","he","in","lo","mu","nc","ng","oa","rs","ti","un" };
		
		arrayvieww<cstring>(strs).sort(&string::less);
		assert_eq(arrayview<cstring>(strs), arrayview<cstring>(strsort));
	}
	
	{
		array<string> x = { "fl","oa","ti","ng","mu","nc","he","rf","lo","at","in","gm","un","ch","er" };
		array<string> y = { "at","ch","er","fl","gm","he","in","lo","mu","nc","ng","oa","rf","ti","un" };
		x.sort([](cstring a, cstring b) { return string::less(a,b); });
		assert_eq(x, y);
	}
	
	{
		cstring a = "aeø☃💩\xF4\x8F\xBF\xBF\xC0\x80\xED\xA0\x80\xF4\x90\x80\x80";
		uint32_t n = 0;
		assert_eq(a.codepoint_at(n), 'a');
		assert_eq(a.codepoint_at(n), 'e');
		assert_eq(a.codepoint_at(n), 0xF8);
		assert_eq(a.codepoint_at(n), 0x2603);
		assert_eq(a.codepoint_at(n), 0x1F4A9);
		assert_eq(a.codepoint_at(n), 0x10FFFF);
		assert_eq(a.codepoint_at(n), 0xDCC0);
		assert_eq(a.codepoint_at(n), 0xDC80);
		assert_eq(a.codepoint_at(n), 0xDCED);
		assert_eq(a.codepoint_at(n), 0xDCA0);
		assert_eq(a.codepoint_at(n), 0xDC80);
		assert_eq(a.codepoint_at(n), 0xDCF4);
		assert_eq(a.codepoint_at(n), 0xDC90);
		assert_eq(a.codepoint_at(n), 0xDC80);
		assert_eq(a.codepoint_at(n), 0xDC80);
		assert_eq(n, a.length());
	}
	
	{
		cstring a = "ø\x80";
		uint32_t n = 0;
		assert_eq(a.codepoint_at(n), 0xF8);
		assert_eq(a.codepoint_at(n), 0xDC80);
		assert_eq(n, a.length());
	}
	
	{
		cstring a = "💩";
		uint32_t n = 0;
		assert_eq(a.codepoint_at(n), 0x1F4A9);
		assert_eq(n, a.length());
	}
	
	{
		cstring a = "☃";
		uint32_t n = 0;
		assert_eq(a.codepoint_at(n), 0x2603);
		assert_eq(n, a.length());
	}
	
	{
		cstring a = "\xF0\x9F\x92";
		uint32_t n = 0;
		assert_eq(a.codepoint_at(n), 0xDCF0);
		assert_eq(a.codepoint_at(n), 0xDC9F);
		assert_eq(a.codepoint_at(n), 0xDC92);
		assert_eq(n, a.length());
	}
	
	{
		assert_eq(string::codepoint(0x41), "A");
		assert_eq(string::codepoint(0xF8), "ø");
		assert_eq(string::codepoint(0x2603), "☃");
		assert_eq(string::codepoint(0xD805), "�");
		assert_eq(string::codepoint(0x1F4A9), "💩");
	}
	
	{
		char * a = strdup("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		assert_eq(string::create_usurp(a), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		
		a = strdup("aaaaaaaa");
		assert_eq(string::create_usurp(a), "aaaaaaaa");
	}
	
	{
		cstring a = "floating munchers";
		assert(a.matches_glob("floating munchers"));
		assert(a.matches_glob("?loating?muncher?"));
		assert(!a.matches_glob("floating"));
		assert(!a.matches_glob(""));
		assert(((cstring)"").matches_glob(""));
		assert(a.matches_glob("floating*"));
		assert(a.matches_glob("*"));
		assert(a.matches_glob("??*??"));
		assert(a.matches_glob("?????????????????"));
		assert(a.matches_glob("*?????????????????"));
		assert(a.matches_glob("??*??*??"));
		assert(a.matches_glob("?????????????????*"));
		assert(a.matches_glob("*??*??*??*??*??*??*??*??*?*"));
		assert(a.matches_glob("*?*?*?*?*?*?*?*?*?*?*?*?*?*?*?*?*?*"));
		assert(a.matches_glob("**?**?**?**?**?**?**?**?**?**?**?**?**?**?**?**?**?**"));
		assert(a.matches_glob("*fl*oa*ti*ng* m*un*ch*er*s*"));
		assert(a.matches_glob("*fl*oa*t*ng* m**ch*er*s*"));
		assert(a.matches_glob("fl*oa*t*ng* m**ch*r*s"));
		assert(!a.matches_glob("????????????????"));
		assert(!a.matches_glob("??????????????????"));
		assert(((cstring)"test test tests test").matches_glob("test*tests*test"));
		assert(!((cstring)"test test tests test").matches_glob("test*tests*test*test"));
		assert(((cstring)"AAAAAAAAAA").matches_globi("a*???a**a"));
		assert(!((cstring)"stacked").matches_glob("foobar*"));
	}
	
	{
		const char * tests[] = {
			"",
			"_",
			"a",
			"aaaa_aa", // length 7
			"aaaa_aaa", // length 8
			"_______________", // length 15
			"aaaaaaaaaaaaaaa",
			"aaaaaaaaaaaaaa_",
			"_aaaaaaaaaaaaaa",
			"aaaaaaaaaaaaaaaa", // length 16
			"aaaaaaaaaaaaaaa_",
			"_aaaaaaaaaaaaaaa",
			"_aaaaaaaaaaaaaaaa", // length 17
			"_aaaaaaaaaaaaaa_a",
			"aaaaaaaaaaaaaaaa_",
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", // length 31
			"_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			"aaaaaaaaaaaaaaa_aaaaaaaaaaaaaaa",
			"aaaaaaaaaaaaaaaa_aaaaaaaaaaaaaa",
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_",
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", // length 32
			"aaaaaaaaaaaaaaa_aaaaaaaaaaaaaaaa",
			"aaaaaaaaaaaaaaaa_aaaaaaaaaaaaaaa",
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_",
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", // length 33
			"_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			"aaaaaaaaaaaaaaa_aaaaaaaaaaaaaaaaa",
			"aaaaaaaaaaaaaaaa_aaaaaaaaaaaaaaaa",
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_a",
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_",
		};
		
		for (const char * test : tests)
		{
			testctx(test) {
				bool nul = false;
				string a = test;
				for (int i : range(strlen(test)))
				{
					if (a[i] == '_')
					{
						nul = true;
						a[i] = '\0';
					}
				}
				assert_eq(a.contains_nul(), nul);
			}
		}
	}
	
	{
		string s1;
		bytearray b1;
		b1.resize(8);
		string s2;
		bytearray b2;
		b2.resize(32);
		test_nomalloc {
			string tmp1 = std::move(b1); // will free b1; this is fine, only allocating is forbidden
			s1 = std::move(tmp1);
			string tmp2 = std::move(b2);
			s2 = std::move(tmp2);
		}
		assert_eq(b1.size(), 0);
		assert_eq(b2.size(), 0);
		assert_eq((uintptr_t)b1.ptr(), 0);
		assert_eq((uintptr_t)b2.ptr(), 0);
		assert_eq(s1.length(), 8);
		assert_eq(s2.length(), 32);
	}
	
	{
		string a = "aaaaaaaaaaaaaaaaaaaaaaaa";
		test_nomalloc {
			assert(a == "aaaaaaaaaaaaaaaaaaaaaaaa");
			a += "aaaaaaa";
			assert(a == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		}
	}
}

test("string::natcompare", "array,memeq", "string")
{
	{
		cstring strs[] = {
		  "A3A",
		  "A03A",
		  "A3a",
		  "A03a",
		  "a$",
		  "a1",
		  "a2",
		  "a02",
		  "a2a",
		  "a2a1",
		  "a02a2",
		  "a2a3",
		  "a2b",
		  "a02b",
		  "a3A",
		  "a03A",
		  "a3a",
		  "a03a",
		  "a10",
		  "a11",
		  "a18446744073709551616", // ensure no integer overflow
		  "a018446744073709551616",
		  "a18446744073709551617",
		  "a018446744073709551617",
		  "a184467440737095516160000",
		  "a0184467440737095516160000",
		  "a184467440737095516160001",
		  "a0184467440737095516160001",
		  "a184467440737095516170000",
		  "a0184467440737095516170000",
		  "a184467440737095516170001",
		  "a0184467440737095516170001",
		  "a@",
		  "aa",
		};
		
		for (size_t a=0;a<ARRAY_SIZE(strs);a++)
		for (size_t b=0;b<ARRAY_SIZE(strs);b++)
		{
			if (a <  b) testctx(strs[a]+" < "+strs[b]) assert_lt(string::snatcompare3(strs[a], strs[b]), 0);
			if (a == b) testctx(strs[a]+" = "+strs[b]) assert_eq(string::snatcompare3(strs[a], strs[b]), 0);
			if (a >  b) testctx(strs[a]+" > "+strs[b]) assert_gt(string::snatcompare3(strs[a], strs[b]), 0);
			testctx(strs[a]+" <=> "+strs[b]) assert_eq(string::snatless(strs[a], strs[b]), a<b);
		}
	}
	
	{
		cstrnul strs[] = {
		  "a$",
		  "a1",
		  "a2",
		  "a02",
		  "a2a",
		  "a2a1",
		  "a02a2",
		  "a2a3",
		  "a2b",
		  "a02b",
		  "A3A",
		  "A3a",
		  "a3A",
		  "a3a",
		  "A03A",
		  "A03a",
		  "a03A",
		  "a03a",
		  "a10",
		  "a11",
		  "a18446744073709551616",
		  "a018446744073709551616",
		  "a18446744073709551617",
		  "a018446744073709551617",
		  "a184467440737095516160000",
		  "a0184467440737095516160000",
		  "a184467440737095516160001",
		  "a0184467440737095516160001",
		  "a184467440737095516170000",
		  "a0184467440737095516170000",
		  "a184467440737095516170001",
		  "a0184467440737095516170001",
		  "a@",
		  "aa",
		};
		
		/*
		puts("");
		for (size_t a=0;a<ARRAY_SIZE(strs);a++)
		{
			printf("%s%-5s",(a?" | ":"        "),(const char*)strs[a]);
		}
		puts("");
		for (size_t b=0;b<ARRAY_SIZE(strs);b++)
		{
			printf("%-5s", (const char*)strs[b]);
			for (size_t a=0;a<ARRAY_SIZE(strs);a++)
			{
				printf(" | %-5d",string::inatcompare3(strs[a], strs[b]));
			}
			puts("");
		}
		*/
		
		for (size_t a=0;a<ARRAY_SIZE(strs);a++)
		for (size_t b=0;b<ARRAY_SIZE(strs);b++)
		{
			if (a <  b) testctx(strs[a]+" < "+strs[b]) assert_lt(string::inatcompare3(strs[a], strs[b]), 0);
			if (a == b) testctx(strs[a]+" = "+strs[b]) assert_eq(string::inatcompare3(strs[a], strs[b]), 0);
			if (a >  b) testctx(strs[a]+" > "+strs[b]) assert_gt(string::inatcompare3(strs[a], strs[b]), 0);
			testctx(strs[a]+" <=> "+strs[b]) assert_eq(string::inatless(strs[a], strs[b]), a<b);
		}
	}
}
