// SPDX-License-Identifier: MIT
// The above license applies only to the WuTF directory, not the entire Arlib.

//See wutf.h for documentation.

#include "wutf.h"

static int decode(uint8_t head, const uint8_t* * tailptr, const uint8_t* end)
{
	const int minforlen[] = { 0x7FFFFFFF, 0x80, 0x800, 0x10000, 0x7FFFFFFF };
	
	const uint8_t * tail = *tailptr;
	int numtrail = ((head&0xC0)==0xC0) + ((head&0xE0)==0xE0) + ((head&0xF0)==0xF0) + ((head&0xF8)==0xF8);
	
	int codepoint = (head & (0x3F>>numtrail));
	int i;
	
	if (tail + numtrail > end) return -1;
	
	for (i=0;i<3;i++)
	{
		if (numtrail>i)
		{
			if ((tail[i] & 0xC0) != 0x80) return -1;
			codepoint = (codepoint<<6) | (tail[i] & 0x3F);
		}
	}
	
	if (codepoint < minforlen[numtrail]) return -1;
	
	*tailptr += numtrail;
	
	return codepoint;
}

static int utf8_to_utf16_len(int flags, const uint8_t* ptr, const uint8_t* end)
{
	int ret = 0;
	const uint8_t* at = ptr;
	
	while (at < end)
	{
		uint8_t head = *at++;
		if (head <= 0x7F)
		{
			ret++;
			continue;
		}
		
		uint32_t codepoint = (uint32_t)decode(head, &at, end);
		ret++;
		if ((codepoint&0xF800) == 0xD800 && !(flags & WUTF_WTF8))
		{
			if ((flags & WUTF_CESU8) && (codepoint&0xFC00) == 0xD800 &&
			    at+3 <= end && at[0]==0xED && (at[1]&0xF0)==0xB0 && (at[2]&0xC0)==0x80)
			{
				at+=3;
				ret++;
			}
			else
			{
				at-=2;
				goto fail;
			}
		}
		if (codepoint>0x10FFFF)
		{
			if (codepoint != (uint32_t)-1) at-=3; // restore the tail
		fail:
			if ((flags&WUTF_INVALID_MASK) == WUTF_INVALID_ABORT) return WUTF_E_INVALID;
			if ((flags&WUTF_INVALID_MASK) == WUTF_INVALID_DROP) ret--;
		}
		else if (codepoint>=0x10000) ret++; // surrogate
	}
	return ret;
}

static const uint8_t* utf8_end(const uint8_t* utf8, int utf8_len)
{
	if (utf8_len >= 0)
	{
		return utf8 + utf8_len;
	}
	else
	{
		while (*utf8) utf8++;
		utf8++; // go behind the NUL
		return utf8;
	}
}

int WuTF_utf8_to_utf16(int flags, const char* utf8, int utf8_len, uint16_t* utf16, int utf16_len)
{
	if ((flags&WUTF_WTF8) && (flags&WUTF_CESU8)) return WUTF_E_INVALID;
	if ((flags&WUTF_WTF8) && (flags&WUTF_INVALID_MASK)==WUTF_INVALID_DCXX) return WUTF_E_INVALID;
	
	//I could run a bunch of special cases depending on whether cbMultiByte<0, etc,
	//but there's no need. I'll optimize it if it ends up a bottleneck.
	
	const uint8_t* iat = (const uint8_t*)utf8;
	const uint8_t* iend = utf8_end(iat, utf8_len);
	
	if (utf16_len == 0)
	{
		return utf8_to_utf16_len(flags, iat, iend);
	}
	
	uint16_t* oat = utf16;
	uint16_t* oend = oat + utf16_len;
	
	while (iat < iend)
	{
		if (oat+1 > oend) break;
		
		uint8_t head = *iat++;
		if (head <= 0x7F)
		{
			*oat++ = head;
			continue;
		}
		
		uint32_t codepoint = (uint32_t)decode(head, &iat, iend); // -1 -> 0xFFFFFFFF
		if (codepoint <= 0xFFFF)
		{
			if ((codepoint&0xF800) == 0xD800 && !(flags & WUTF_WTF8))
			{
				if ((flags & WUTF_CESU8) && (codepoint&0xFC00) == 0xD800 &&
						iat+3 <= iend && iat[0]==0xED && (iat[1]&0xF0)==0xB0 && (iat[2]&0xC0)==0x80)
				{
					*oat++ = codepoint;
					codepoint = (uint32_t)decode(*iat++, &iat, iend);
				}
				else
				{
					iat-=2;
					goto fail;
				}
			}
			*oat++ = codepoint;
		}
		else
		{
			if (codepoint > 0x10FFFF)
			{
				if (codepoint != (uint32_t)-1) iat-=3; // restore the tail
			fail:
				switch (flags & WUTF_INVALID_MASK)
				{
					case WUTF_INVALID_ABORT: return WUTF_E_INVALID;
					case WUTF_INVALID_DROP: break;
					case WUTF_INVALID_FFFD: *oat++ = 0xFFFD; break;
					case WUTF_INVALID_DCXX: *oat++ = 0xDC00 + head; break;
				}
				continue;
			}
			
			if (oat+2 > oend) break;
			codepoint -= 0x10000;
			*oat++ = 0xD800 | (codepoint>>10);
			*oat++ = 0xDC00 | (codepoint&0x3FF);
		}
	}
	if (iat != iend)
	{
		if (!(flags & WUTF_TRUNCATE)) return WUTF_E_TRUNCATE;
		while (oat < oend) *oat++ = 0xFFFD;
	}
	
	return oat - utf16;
}



static int utf16_to_utf8_len(int flags, const uint16_t* ptr, const uint16_t* end)
{
	int ret = 0;
	const uint16_t* at = ptr;
	
	while (at < end)
	{
		uint16_t head = *at++;
		ret++;
		if (head >= 0x80) ret++;
		if (head >= 0x0800)
		{
			ret++;
			if ((head&0xF800)==0xD800)
			{
				if (head<=0xDBFF && at < end && *at >= 0xDC00 && *at <= 0xDFFF)
				{
					at++;
					ret++;
					continue;
				}
				
				if (!(flags & WUTF_WTF8))
				{
					if ((flags & WUTF_INVALID_MASK) == WUTF_INVALID_ABORT) return WUTF_E_INVALID;
					if ((flags & WUTF_INVALID_MASK) == WUTF_INVALID_DROP) { ret--; continue; }
					if ((flags & WUTF_INVALID_MASK) == WUTF_INVALID_FFFD) continue;
					if ((flags & WUTF_INVALID_MASK) == WUTF_INVALID_DCXX)
					{
						if ((head&0xFF00)==0xDC00) continue;
						else return WUTF_E_INVALID;
					}
				}
			}
		}
	}
	return ret;
}

static const uint16_t* utf16_end(const uint16_t* utf16, int utf16_len)
{
	if (utf16_len >= 0)
	{
		return utf16 + utf16_len;
	}
	else
	{
		while (*utf16) utf16++;
		utf16++; // go behind the NUL
		return utf16;
	}
}

int WuTF_utf16_to_utf8(int flags, const uint16_t* utf16, int utf16_len, char* utf8, int utf8_len)
{
	if (flags&WUTF_CESU8) return WUTF_E_INVALID;
	if ((flags&WUTF_WTF8) && (flags&WUTF_INVALID_MASK) == WUTF_INVALID_DCXX) return WUTF_E_INVALID;
	
	const uint16_t* iat = utf16;
	const uint16_t* iend = utf16_end(iat, utf16_len);
	
	if (utf8_len == 0)
	{
		return utf16_to_utf8_len(flags, iat, iend);
	}
	
	uint8_t* oat = (uint8_t*)utf8;
	uint8_t* oend = oat + utf8_len;
	
	while (iat < iend)
	{
		uint16_t head = *iat++;
		if (head <= 0x7F)
		{
			if (oat+1 > oend) break;
			*oat++ = (uint8_t)head;
		}
		else if (head <= 0x07FF)
		{
			if (oat+2 > oend) break;
			*oat++ = (((head>> 6)     )|0xC0);
			*oat++ = (((head    )&0x3F)|0x80);
		}
		else
		{
			if ((head&0xF800)==0xD800)
			{
				if (head<=0xDBFF && iat < iend)
				{
					uint16_t tail = *iat;
					if (tail >= 0xDC00 && tail <= 0xDFFF)
					{
						iat++;
						if (oat+4 > oend) break;
						uint32_t codepoint = 0x10000+((head&0x03FF)<<10)+(tail&0x03FF);
						*oat++ = (((codepoint>>18)&0x07)|0xF0);
						*oat++ = (((codepoint>>12)&0x3F)|0x80);
						*oat++ = (((codepoint>>6 )&0x3F)|0x80);
						*oat++ = (((codepoint    )&0x3F)|0x80);
						continue;
					}
				}
				
				if (!(flags & WUTF_WTF8))
				{
					if ((flags & WUTF_INVALID_MASK) == WUTF_INVALID_ABORT) return WUTF_E_INVALID;
					if ((flags & WUTF_INVALID_MASK) == WUTF_INVALID_DROP) continue;
					if ((flags & WUTF_INVALID_MASK) == WUTF_INVALID_FFFD) { head = 0xFFFD; continue; }
					if ((flags & WUTF_INVALID_MASK) == WUTF_INVALID_DCXX)
					{
						if ((head&0xFF00)==0xDC00)
						{
							if (oat+1 > oend) break;
							*oat++ = (head&0x00FF); // don't bother ensuring that this ends up as invalid utf8, too much effort
							continue;
						}
						else
						{
							return WUTF_E_INVALID;
						}
					}
				}
			}
			if (oat+3 > oend) break;
			*oat++ = (((head>>12)&0x0F)|0xE0);
			*oat++ = (((head>>6 )&0x3F)|0x80);
			*oat++ = (((head    )&0x3F)|0x80);
		}
	}
	if (iat != iend)
	{
		if (!(flags & WUTF_TRUNCATE)) return WUTF_E_TRUNCATE;
		while (oat < oend) *oat++ = '?'; // (probably) can't fit a U+FFFD, just shove in something
	}
	
	return oat - (uint8_t*)utf8;
}
