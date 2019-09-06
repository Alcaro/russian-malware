#include "string.h"
#include "test.h"

void string::resize(uint32_t newlen)
{
	switch (!inlined()<<1 | (newlen>max_inline()))
	{
	case 0: // small->small
		{
			m_inline[newlen] = '\0';
			m_inline_len = max_inline()-newlen;
		}
		break;
	case 1: // small->big
		{
			uint8_t* newptr = malloc(bytes_for(newlen));
			memcpy(newptr, m_inline, max_inline());
			newptr[newlen] = '\0';
			m_data = newptr;
			m_len = newlen;
			m_nul = true;
			
			m_inline_len = -1;
		}
		break;
	case 2: // big->small
		{
			uint8_t* oldptr = m_data;
			memcpy(m_inline, oldptr, newlen);
			free(oldptr);
			m_inline[newlen] = '\0';
			m_inline_len = max_inline()-newlen;
		}
		break;
	case 3: // big->big
		{
			m_data = realloc(m_data, bytes_for(newlen));
			m_data[newlen] = '\0';
			m_len = newlen;
		}
		break;
	}
}

void string::init_from(arrayview<byte> data)
{
	const uint8_t * str = data.ptr();
	uint32_t len = data.size();
	
	if (len <= max_inline())
	{
		memcpy(m_inline, str, len);
		m_inline[len] = '\0';
		m_inline_len = max_inline()-len;
	}
	else
	{
		m_inline_len = -1;
		
		m_data = malloc(bytes_for(len));
		memcpy(m_data, str, len);
		m_data[len]='\0';
		
		m_len = len;
		m_nul = true;
	}
}

void string::reinit_from(arrayview<byte> data)
{
	const uint8_t * str = data.ptr();
	uint32_t len = data.size();
	
	if (str >= this->ptr() && str <= this->ptr()+this->length())
	{
		if (str == this->ptr() && len == this->length()) return;
		
		memmove(this->ptr(), str, len);
		resize(len);
	}
	else
	{
		release();
		init_from(data);
	}
}

string string::create_usurp(char * str)
{
	cstring tmp(str);
	string ret;
	memcpy(&ret, &tmp, sizeof(string));
	if (tmp.inlined()) free(str);
	return ret;
}


void string::replace_set(uint32_t pos, uint32_t len, cstring newdat)
{
	//if newdat is a cstring backed by this, modifying this invalidates that string, so it's illegal
	//if newdat equals this, then the memmoves will mess things up
	if (this == &newdat)
	{
		string copy = newdat;
		replace_set(pos, len, copy);
		return;
	}
	
	uint32_t prevlength = length();
	uint32_t newlength = newdat.length();
	
	if (newlength < prevlength)
	{
		memmove(ptr()+pos+newlength, ptr()+pos+len, prevlength-len-pos);
		resize(prevlength - len + newlength);
	}
	if (newlength > prevlength)
	{
		resize(prevlength - len + newlength);
		memmove(ptr()+pos+newlength, ptr()+pos+len, prevlength-len-pos);
	}
	
	memcpy(ptr()+pos, newdat.ptr(), newlength);
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
			outlen += out.length(); // outlen-inlen is type uint - bad idea
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
			if (ret_i == 0) ret_i = (int)ab[n] - (int)bb[n];
			
			uint8_t ac = ab[n];
			uint8_t bc = bb[n];
			if (ac >= 'a' && ac <= 'z') ac -= 'a'-'A';
			if (bc >= 'a' && bc <= 'z') bc -= 'a'-'A';
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
				if (*abs != *bbs)
					return (int)*abs - (int)*bbs;
				abs++;
				bbs++;
			}
		}
		else
		{
			if (*ab != *bb)
			{
				if (ret_case == 0) ret_case = (int)*ab - (int)*bb;
				
				uint8_t ac = *ab;
				uint8_t bc = *bb;
				if (case_insensitive && ac >= 'a' && ac <= 'z') ac -= 'a'-'A';
				if (case_insensitive && bc >= 'a' && bc <= 'z') bc -= 'a'-'A';
				if (ac != bc) return (int)ac - (int)bc;
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


string string::codepoint(uint32_t cp)
{
	if (cp <= 0x7F)
	{
		uint8_t ret[] = { (uint8_t)cp };
		return string(arrayview<byte>(ret));
	}
	else if (cp <= 0x07FF)
	{
		uint8_t ret[] = {
			(uint8_t)(((cp>> 6)     )|0xC0),
			(uint8_t)(((cp    )&0x3F)|0x80),
		};
		return string(arrayview<byte>(ret));
	}
	else if (cp >= 0xD800 && cp <= 0xDFFF)
		return "\xEF\xBF\xBD"; // curse utf16 forever
	else if (cp <= 0xFFFF)
	{
		uint8_t ret[] = {
			(uint8_t)(((cp>>12)&0x0F)|0xE0),
			(uint8_t)(((cp>>6 )&0x3F)|0x80),
			(uint8_t)(((cp    )&0x3F)|0x80),
		};
		return string(arrayview<byte>(ret));
	}
	else if (cp <= 0x10FFFF)
	{
		uint8_t ret[] = {
			(uint8_t)(((cp>>18)&0x07)|0xF0),
			(uint8_t)(((cp>>12)&0x3F)|0x80),
			(uint8_t)(((cp>>6 )&0x3F)|0x80),
			(uint8_t)(((cp    )&0x3F)|0x80),
		};
		return string(arrayview<byte>(ret));
	}
	else return "\xEF\xBF\xBD";
}

bool cstring::isutf8() const
{
	const uint8_t * bytes = ptr();
	const uint8_t * end = bytes + length();
	
	while (bytes < end)
	{
		uint8_t head = *bytes++;
		if (LIKELY(head < 0x80))
			continue;
		if (UNLIKELY(bytes == end))
			return false; // continuation needed if above 0x80
		
		if (head < 0xC2) // continuation or overlong twobyte
			return false;
		if (head < 0xE0) // twobyte
			goto cont1;
		if (head == 0xE0 && *bytes <= 0x9F) // threebyte overlong
			return false;
		if (head == 0xED && *bytes >  0x9F) // utf16 surrogate range
			return false;
		if (head < 0xF0) // threebyte
			goto cont2;
		if (head == 0xF0 && *bytes <= 0x8F) // fourbyte overlong
			return false;
		if (head == 0xF4 && *bytes >  0x8F) // fourbyte, above U+110000
			return false;
		if (head < 0xF5) // fourbyte
			goto cont3;
		return false; // U+140000 or above, fivebyte, or otherwise invalid
		
		cont3:
			if (bytes == end || ((*bytes++) & 0xC0) != 0x80) return false;
		cont2:
			if (bytes == end || ((*bytes++) & 0xC0) != 0x80) return false;
		cont1:
			if (bytes == end || ((*bytes++) & 0xC0) != 0x80) return false;
	}
	
	return true;
}

uint32_t cstring::codepoint_at(uint32_t& idx) const
{
	const uint8_t * bytes = ptr();
	uint32_t len = length();
	if (UNLIKELY(idx == len)) return 0;
	
	uint8_t head = bytes[idx++];
	if (LIKELY(head < 0x80)) return head;
	
	if (head <= 0xDF)
	{
		if (idx > len-1)
			goto fail;
		if (head < 0xC2) // continuation or overlong twobyte
			goto fail;
		if ((bytes[idx+0]&0xC0)!=0x80)
			goto fail;
		
		idx += 1;
		
		//return (head&0x1F)<<6 | bytes[idx+1]&0x3F;
		return (head<<6) + (bytes[idx-1]<<0) - ((0xC0<<6) + (0x80<<0));
	}
	
	if (head <= 0xEF)
	{
		if (idx > len-2)
			goto fail;
		if (head == 0xE0 && bytes[idx+0] < 0xA0) // overlong
			goto fail;
		if (head == 0xED && bytes[idx+0] >= 0xA0) // surrogate
			goto fail;
		if ((bytes[idx+0]&0xC0)!=0x80)
			goto fail;
		if ((bytes[idx+1]&0xC0)!=0x80)
			goto fail;
		
		idx += 2;
		return (head<<12) + (bytes[idx-2]<<6) + (bytes[idx-1]<<0) - ((0xE0<<12) + (0x80<<6) + (0x80<<0));
	}
	
	if (head <= 0xF4)
	{
		if (idx > len-3)
			goto fail;
		if (head == 0xF0 && bytes[idx+0] < 0x90) // overlong
			goto fail;
		if (head == 0xF4 && bytes[idx+0] >= 0x90) // above U+10FFFF
			goto fail;
		if ((bytes[idx+0]&0xC0)!=0x80)
			goto fail;
		if ((bytes[idx+1]&0xC0)!=0x80)
			goto fail;
		if ((bytes[idx+2]&0xC0)!=0x80)
			goto fail;
		
		idx += 3;
		return (head<<18) + (bytes[idx-3]<<12) + (bytes[idx-2]<<6) + (bytes[idx-1]<<0) - ((0xF0<<18) + (0x80<<12) + (0x80<<6) + (0x80<<0));
	}
	
fail:
	return 0xDC00 | head;
}

bool cstring::matches_glob(cstring pat) const
{
	const uint8_t * s = ptr();
	const uint8_t * p = pat.ptr();
	const uint8_t * se = s + length();
	const uint8_t * pe = p + pat.length();
	
	while (s < se && p < pe && *p != '*')
	{
		if (*s != *p && *p != '?') return false;
		s++;
		p++;
	}
	if (p == pe) return s==se;
	
	const uint8_t * sp = NULL; // never used uninitialized, but Gcc is dumb
	const uint8_t * pp = NULL;
	while (s < se)
	{
		if (p < pe && *p == '*')
		{
			p++;
			if (p == pe) return true;
			pp = p;
			sp = s+1;
		}
		else if (p < pe && s < se && (*p == *s || *p == '?'))
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

bool cstring::matches_globi(cstring pat) const
{
	const uint8_t * s = ptr();
	const uint8_t * p = pat.ptr();
	const uint8_t * se = s + length();
	const uint8_t * pe = p + pat.length();
	
	while (s < se && p < pe && *p != '*')
	{
		if (tolower(*s) != tolower(*p) && *p != '?') return false;
		s++;
		p++;
	}
	if (p == pe) return s==se;
	
	const uint8_t * sp = NULL; // never used uninitialized, but Gcc is dumb
	const uint8_t * pp = NULL;
	while (s < se)
	{
		if (p < pe && *p == '*')
		{
			p++;
			if (p == pe) return true;
			pp = p;
			sp = s+1;
		}
		else if (p < pe && s < se && (tolower(*s) == tolower(*p) || *p == '?'))
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


bool strtoken(const char * haystack, const char * needle, char separator)
{
	//token lists are annoyingly complex to parse
	//I suspect 'people using fixed-size buffers, then extension list grows and app explodes'
	// isn't the only reason GL_EXTENSIONS string was deprecated from OpenGL
	size_t nlen = strlen(needle);
	
	while (true)
	{
		const char * found = strstr(haystack, needle);
		if (!found) break;
		
		if ((found==haystack || found[-1]==separator) && // ensure the match is the start of a word
				(found[nlen]==separator || found[nlen]=='\0')) // ensure the match is the end of a word
		{
			return true;
		}
		
		haystack = strchr(found, separator); // try again, could've found GL_foobar_limited when looking for GL_foobar
		if (!haystack) return false;
	}
	return false;
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

test("string", "array", "string")
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
		
		//a.replace(1,1, "ello");
		//assert_eq(a, "hello!");
		//assert_eq(a.substr(1,3), "el");
		//a.replace(1,4, "i");
		//assert_eq(a, "hi!");
		//a.replace(1,2, "ey");
		//assert_eq(a, "hey");
		//
		//assert_eq(a.substr(2,2), "");
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
		//a.replace(1,5, "-");
		//assert_eq(a, "1-789012345678123456789012345678");
		//a.replace(4,20, "-");
		//assert_eq(a, "1-78-12345678");
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
		assert_eq(cstring("aaaaaaaa").replace("aa","aba"), "abaabaabaaba");
	}
	
	{
		//this has thrown valgrind errors due to derpy allocations
		string a = "abcdefghijklmnopqrstuvwxyz";
		string b = a; // needs an extra reference
		a += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
		assert_eq(a, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
		assert_eq(b, "abcdefghijklmnopqrstuvwxyz");
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
		arrayview<byte> a((uint8_t*)"123", 3);
		string b = "["+string(a)+"]";
		string c = "["+cstring(a)+"]";
		assert_eq(b, "[123]");
		assert_eq(c, "[123]");
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
		
		a = "baaaaaaaaaaaaaaa";
		assert_eq(a.rsplit<1>("aa").join("."), "baaaaaaaaaaaaa.");
		assert_eq(a.rsplit<1>("x").join("."), "baaaaaaaaaaaaaaa");
		
		a = "aaaaaaaaaaaaaaab";
		assert_eq(a.rsplit<1>("aa").join("."), "aaaaaaaaaaaaa.b");
		assert_eq(a.rsplit<1>("x").join("."), "aaaaaaaaaaaaaaab");
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
		cstring a(NULL);
		cstring b = NULL;
		cstring c; c = NULL;
		cstring d((const char*)NULL);
		string e(NULL);
		string f = NULL;
		string g; g = NULL;
		string h((const char*)NULL);
		
		assert_eq(a, "");
		assert_eq(b, "");
		assert_eq(c, "");
		assert_eq(d, "");
		assert_eq(e, "");
		assert_eq(f, "");
		assert_eq(g, "");
		assert_eq(h, "");
		
		assert(!a);
		assert(!b);
		assert(!c);
		assert(!d);
		assert(!e);
		assert(!f);
		assert(!g);
		assert(!h);
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
		assert(a.icontains("Ers"));
		assert(!a.icontains("x"));
	}
	
	{
		cstring a(arrayview<byte>((byte*)"\0", 1));
		assert_eq(a.length(), 1);
		assert_eq(a[0], '\0');
		
		cstring b(arrayview<byte>((byte*)"\0\0\0", 3));
		assert_eq(b.length(), 3);
		assert_eq(b.replace(a, "ee"), "eeeeee");
	}
	
	{
		assert(cstring().isutf8());
		assert(cstring("abc").isutf8());
		assert(cstring(arrayview<byte>((byte*)"\0", 1)).isutf8());
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
			if (a <  b) testctx(strs[a]+" < "+strs[b]) assert(string::less(strs[a], strs[b]));
			else       testctx(strs[a]+" >= "+strs[b]) assert(!string::less(strs[a], strs[b]));
		}
	}
	
	{
		cstring strs[] = {
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
			if (a <  b) testctx(strs[a]+" < "+strs[b]) assert(string::iless(strs[a], strs[b]));
			else       testctx(strs[a]+" >= "+strs[b]) assert(!string::iless(strs[a], strs[b]));
		}
	}
	
	{
		cstring strs[] = {
		  "A3A",
		  "A03A",
		  "A3a",
		  "A03a",
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
		  "aa",
		};
		
		for (size_t a=0;a<ARRAY_SIZE(strs);a++)
		for (size_t b=0;b<ARRAY_SIZE(strs);b++)
		{
			if (a <  b) testctx(strs[a]+" < "+strs[b]) assert_lt(string::natcompare3(strs[a], strs[b]), 0);
			if (a == b) testctx(strs[a]+" = "+strs[b]) assert_eq(string::natcompare3(strs[a], strs[b]), 0);
			if (a >  b) testctx(strs[a]+" > "+strs[b]) assert_gt(string::natcompare3(strs[a], strs[b]), 0);
			if (a <  b) testctx(strs[a]+" < " +strs[b]) assert( string::natless(strs[a], strs[b]));
			else        testctx(strs[a]+" >= "+strs[b]) assert(!string::natless(strs[a], strs[b]));
		}
	}
	
	{
		cstring strs[] = {
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
		  "aa",
		};
		
		/*
		puts("");
		for (size_t a=0;a<ARRAY_SIZE(strs);a++)
		{
			printf("%s%-5s",(a?" | ":"        "),strs[a].c_str().c_str());
		}
		puts("");
		for (size_t b=0;b<ARRAY_SIZE(strs);b++)
		{
			printf("%-5s", strs[b].c_str().c_str());
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
			if (a <  b) testctx(strs[a]+" < " +strs[b]) assert( string::inatless(strs[a], strs[b]));
			else        testctx(strs[a]+" >= "+strs[b]) assert(!string::inatless(strs[a], strs[b]));
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
		cstring a = "💩";
		uint32_t n = 0;
		assert_eq(a.codepoint_at(n), 0x1F4A9);
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
		assert_eq(string::codepoint(0xD800), "�");
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
	}
}
