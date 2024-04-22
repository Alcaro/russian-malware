#include "json.h"
#include "stringconv.h"
#include "simd.h"
#include "endian.h"
#include "js-identifier.h"
#include <math.h>

// TODO: find some better place for this
// if input is 0, undefined behavior
static inline int ctz32(uint32_t in)
{
#if defined(__GNUC__)
	return __builtin_ctz(in);
#elif defined(_MSC_VER)
	DWORD n;
	_BitScanForward(&n, in);
	return n;
#else
	int ret = 0;
	if (!(in&0xFFFF)) { ret += 16; in >>= 16; }
	if (!(in&0xFF))   { ret += 8;  in >>= 8; }
	if (!(in&0xF))    { ret += 4;  in >>= 4; }
	if (!(in&0x3))    { ret += 2;  in >>= 2; }
	if (!(in&0x1))    { ret += 1;  in >>= 1; }
	return ret;
#endif
}


// https://spec.json5.org/
// I disagree with some of the design choices, but such is what json5 says
// (\u in unquoted keys, \e being e and not error, control chars in quoted strings)

template<bool json5>
jsonparser::event jsonparser::next_inner()
{
	skip_spaces<json5>();
	if (m_need_error)
	{
		m_need_error = false;
		m_errored = true;
		return { jsonparser::error };
	}
	const uint8_t * end = m_data_holder.bytes().ptr() + m_data_holder.length();
	if (m_at == end)
	{
		if (m_nesting)
		{
			m_need_error = true;
			bool is_map = m_nesting[m_nesting.size()-1];
			if (is_map && !m_want_key)
			{
				m_want_key = true;
				return { jsonparser::jnull };
			}
			m_nesting.resize(m_nesting.size()-1);
			bool next_map = (m_nesting && m_nesting[m_nesting.size()-1]);
			m_want_key = next_map;
			if (is_map)
				return { jsonparser::exit_map };
			else
				return { jsonparser::exit_list };
		}
		return { jsonparser::finish };
	}
	bytesw ret_str;
	if (m_want_key)
	{
		if (*m_at == '"' || (json5 && *m_at == '\''))
		{
			goto parse_str_key;
		str_key_return: ;
		}
		else if (*m_at == '}')
		{
			goto close_brace;
		}
		else if (json5)
		{
			size_t ret_len = js_identifier_name(m_at);
			if (!ret_len)
			{
				m_at++;
				m_errored = true;
				return { jsonparser::error };
			}
			ret_str = bytesw(m_at, ret_len);
			m_at += ret_len;
		}
		else m_need_error = true;
		skip_spaces<json5>();
		if (*m_at == ':')
			m_at++;
		else
			m_need_error = true;
		m_want_key = false;
		return { jsonparser::map_key, decode_backslashes<json5>(ret_str) };
	}
	if (*m_at == '{')
	{
		m_at++;
		m_want_key = true;
		m_nesting.append(true);
		return { jsonparser::enter_map };
	}
	if (*m_at == '[')
	{
		m_at++;
		m_nesting.append(false);
		return { jsonparser::enter_list };
	}
	if (*m_at == ']')
	{
		bool is_brace;
		is_brace = false;
		if (false)
		{
		close_brace:
			is_brace = true;
		}
		m_at++;
		if (!m_nesting || m_nesting[m_nesting.size()-1] != is_brace)
		{
			m_errored = true;
			return { jsonparser::error };
		}
		m_nesting.resize(m_nesting.size()-1);
		return prepare_next<json5>({ is_brace ? jsonparser::exit_map : jsonparser::exit_list });
	}
	if (*m_at == '"' || (json5 && *m_at == '\''))
	{
	parse_str_key:
		char term = *m_at++;
		uint8_t * start = m_at;
		while (true)
		{
			if (m_at[0] == '\\' && m_at[1] != '\0')
			{
				if (m_at[1] == '\r' && m_at[2] == '\n')
					m_at += 3;
				else
					m_at += 2;
				continue;
			}
			if (!json5 && m_at[0] < 0x20)
				m_need_error = true;
			if (*m_at == '\n' || *m_at == '\r' || *m_at == '\0') // 2028 and 2029 are legal
			{
				m_need_error = true;
				break;
			}
			if (*m_at == term)
				break;
			m_at++;
		}
		
		ret_str = bytesw(start, m_at-start);
		if (*m_at == term)
			m_at++;
		
		if (m_want_key)
			goto str_key_return;
		return prepare_next<json5>({ jsonparser::str, decode_backslashes<json5>(ret_str) });
	}
	
	if (m_at+4 <= end && readu_le32(m_at) == 0x65757274) // true
		return prepare_next<json5>({ jsonparser::jtrue }, 4);
	if (m_at+5 <= end && readu_le32(m_at+1) == 0x65736c61) // alse
		return prepare_next<json5>({ jsonparser::jfalse }, 5);
	if (m_at+4 <= end && readu_le32(m_at) == 0x6c6c756e) // null
		return prepare_next<json5>({ jsonparser::jnull }, 4);
	
	// only numbers (including nan/inf) left
	const uint8_t * num_start = m_at;
	
	if ((json5 && *m_at == '+') || *m_at == '-')
		m_at++;
	
	if (json5 && m_at+8 <= end && readu_le64(m_at) == 0x7974696E69666E49) m_at += 8; // Infinity
	else if (json5 && m_at[0] == 'N' && m_at[1] == 'a' && m_at[2] == 'N') m_at += 3;
	else if (json5 && m_at[0] == '0' && (m_at[1] == 'x' || m_at[1] =='X'))
	{
		m_at += 2;
		if (!isxdigit(*m_at))
			m_need_error = true;
		while (isxdigit(*m_at))
			m_at++;
	}
	else
	{
		if (m_at[0] == '0' && isdigit(m_at[1]))
			m_need_error = true;
		if (!isdigit(*m_at) && (!json5 || *m_at != '.'))
		{
			m_at++;
			m_errored = true;
			return { jsonparser::error }; // if it doesn't look like a number, just discard it
		}
		if (!isdigit(m_at[0]) && !isdigit(m_at[1])) // reject lone decimal point
			m_need_error = true;
		while (isdigit(*m_at))
			m_at++;
		if (*m_at == '.')
		{
			m_at++;
			if (!json5 && !isdigit(*m_at))
				m_need_error = true;
			while (isdigit(*m_at))
				m_at++;
		}
		if (*m_at == 'e' || *m_at == 'E')
		{
			m_at++;
			if (*m_at == '+' || *m_at == '-')
				m_at++;
			if (!isdigit(*m_at))
				m_need_error = true;
			while (isdigit(*m_at))
				m_at++;
		}
	}
	
	return prepare_next<json5>({ jsonparser::num, cstring(bytesr(num_start, m_at-num_start)) });
}

template<bool json5>
void jsonparser::skip_spaces()
{
	// JSON5 considers any JS WhiteSpace or LineTerminator to be whitespace
	// WhiteSpace is 0009 000B 000C 0020 00A0 FEFF, or anything in Unicode 3.0 category Zs
	// LineTerminator is 000A 000D 2028 2029
	// Zs is 0020 00A0 1680 2000-200B 202F 3000 (not 205F - it is Zs, but only since Unicode 3.2)
	// normal JSON only accepts 0009 000A 000D 0020 as whitespace
again:
	if (LIKELY(*m_at < 0x80))
	{
		if (isspace(*m_at) || (json5 && (*m_at == '\f' || *m_at == '\v')))
		{
			m_at++;
			goto again;
		}
		if (json5 && m_at[0] == '/' && m_at[1] == '/')
		{
			// // comments can end with \r \n 2028 2029
			while (true)
			{
				m_at++;
				if (*m_at == '\n' || *m_at == '\r' || *m_at == '\0')
					break;
				if (m_at[0] == '\xE2' && m_at[1] == '\x80' && (m_at[2] == '\xA8' || m_at[2] == '\xA9'))
					break;
			}
			goto again;
		}
		if (json5 && m_at[0] == '/' && m_at[1] == '*')
		{
			// /* comments can contain everything, except */
			const char * end = strstr((char*)m_at+2, "*/");
			if (end)
				end += 2;
			else
			{
				m_need_error = true;
				end = strchr((char*)m_at+2, '\0');
			}
			m_at = (uint8_t*)end;
			goto again;
		}
		return;
	}
	if (!json5)
		return;
	if (memeq(m_at, u8"\u00A0", 2))
	{
		m_at += 2;
		goto again;
	}
	if (m_at[1] == '\x00')
		return;
	uint64_t u2000_series_whitespace =
		(1ull<<0x00) | (1ull<<0x01) | (1ull<<0x02) | (1ull<<0x03) | (1ull<<0x04) | (1ull<<0x05) | (1ull<<0x06) | (1ull<<0x07) |
		(1ull<<0x08) | (1ull<<0x09) | (1ull<<0x0A) | (1ull<<0x0B) | (1ull<<0x28) | (1ull<<0x29) | (1ull<<0x2F);
	if (memeq(m_at, u8"\u1680", 3) || memeq(m_at, u8"\u3000", 3) || memeq(m_at, u8"\uFEFF", 3) ||
	    (memeq(m_at, u8"\u2000", 2) && m_at[2] >= 0x80 && m_at[2] <= 0xBF && ((u2000_series_whitespace >> (m_at[2]&0x3F))&1)))
	{
		m_at += 3;
		goto again;
	}
}

void jsonparser::skip_spaces_json() { skip_spaces<false>(); }
void jsonparser::skip_spaces_json5() { skip_spaces<true>(); }

template<bool json5>
jsonparser::event jsonparser::prepare_next(event ev, size_t forward)
{
	m_at += forward;
	
	skip_spaces<json5>();
	if (!m_nesting)
	{
		if (m_at != m_data_holder.bytes().ptr() + m_data_holder.length())
		{
			m_need_error = true;
			m_at = m_data_holder.bytes().ptr() + m_data_holder.length();
		}
		return ev;
	}
	if (*m_at == ',')
	{
		m_at++;
		skip_spaces<json5>();
		if (!json5 && (*m_at == ']' || *m_at == '}'))
			m_need_error = true;
	}
	else if (*m_at != ']' && *m_at != '}')
		m_need_error = true;
	
	m_want_key = m_nesting[m_nesting.size()-1];
	return ev;
}

template<bool json5>
cstring jsonparser::decode_backslashes(bytesw by)
{
	if (!by.contains('\\'))
		return by;
	uint8_t* out = by.ptr();
	uint8_t* in = by.ptr();
	uint8_t* end = in + by.size();
	while (true)
	{
		// leave every character unchanged, except \, in which case (use first that matches)
		// - if followed by any of the sequences \r\n \n \r \u2028 \u2029, emit nothing
		// - if followed by u, parse 4-digit escape (including utf16 surrogates)
		// - if followed by x, parse 2-digit escape
		// - if followed by 0 and after is not 0-9, emit a NUL byte
		// - if followed by 0-9, emit nothing and set the error flag (octal is explicitly banned by the JS spec)
		// - if followed by one of bfnrtv, emit the corresponding control char
		// - if followed by one of ' " \, emit that byte unchanged
		// - if followed by anything else, emit that byte unchanged
		// of the above, normal JSON only accepts ubfnrt"\, everything else (including the 'anything else' fallback) is an error
		// normal JSON also rejects control chars in strings; JSON5 permits them
		uint8_t* slash = (uint8_t*)memchr(in, '\\', end-in);
		if (!slash)
		{
			memmove(out, in, end-in);
			out += end-in;
			return bytesr(by.ptr(), out-by.ptr());
		}
		memmove(out, in, slash-in);
		out += slash-in;
		if (slash[1] == '\0')
		{
			m_need_error = true;
			return bytesr(by.ptr(), out-by.ptr());
		}
		in = slash+2;
		
		if (json5 && slash[1] == '\r')
		{
			if (slash[2] == '\n')
				in++;
		}
		else if (json5 && slash[1] == '\n') {}
		else if (json5 && (memeq(slash+1, u8"\u2028", 3) || memeq(slash+1, u8"\u2029", 3)))
		{
			in = slash+4;
		}
		else if (slash[1] == 'u' || (json5 && slash[1] == 'x'))
		{
			size_t n_digits = (slash[1] == 'u' ? 4 : 2);
			uint32_t codepoint;
			if (!fromstringhex_ptr((char*)in, n_digits, codepoint))
			{
				m_need_error = true;
				continue;
			}
			in += n_digits;
			
			if (codepoint >= 0xD800 && codepoint <= 0xDCFF && in[0]=='\\' && in[1]=='u')
			{
				uint16_t low_sur;
				if (fromstringhex_ptr((char*)in+2, 4, low_sur))
				{
					in += 6;
					codepoint = 0x10000 + ((codepoint-0xD800)<<10) + (low_sur-0xDC00);
				}
			}
			// else leave as is, string::codepoint will return fffd for unpaired surrogates
			
			out += string::codepoint(out, codepoint);
		}
		else if (json5 && slash[1] == '0' && !isdigit(slash[2]))
		{
			*out++ = '\0';
		}
		else if (isdigit(slash[1]))
		{
			m_need_error = true;
		}
		else if (slash[1] == 'b') *out++ = '\b';
		else if (slash[1] == 'f') *out++ = '\f';
		else if (slash[1] == 'n') *out++ = '\n';
		else if (slash[1] == 'r') *out++ = '\r';
		else if (slash[1] == 't') *out++ = '\t';
		else if (json5 && slash[1] == 'v') *out++ = '\v';
		else if (json5) *out++ = slash[1];
		else if (!json5 && (slash[1] == '"' || slash[1] == '\\' || slash[1] == '/')) *out++ = slash[1];
		else m_need_error = true;
	}
}

jsonparser::event jsonparser::next() { return next_inner<false>(); }
jsonparser::event jsonparser::next5() { return next_inner<true>(); }

bool json5parser::parse_num_as_double(cstring str, double& out)
{
	out = 0;
	const char * ptr = (char*)str.bytes().ptr();
	const char * end = ptr + str.length();
	if (ptr == end) return false;
	
	if (fromstring_ptr(ptr, end-ptr, out))
		return true;
	
	if (ptr+8 <= end && memeq(end-8, "Infinity", 8))
	{
		if (*ptr == '-') out = -HUGE_VAL;
		else out = HUGE_VAL;
		return true;
	}
	if (ptr+3 <= end && memeq(end-3, "NaN", 3))
	{
		out = NAN;
		return true;
	}
	
	intmax_t tmp;
	if (!parse_num_as_signed(str, tmp))
		return false;
	out = tmp;
	return true;
}
static bool parse_num_as_signless(const char * ptr, const char * end, uintmax_t& out)
{
	if (ptr+2 <= end && (ptr[1] == 'x' || ptr[1] == 'X'))
		return fromstringhex_ptr(ptr+2, end-ptr-2, out);
	else
		return fromstring_ptr(ptr, end-ptr, out);
}
bool json5parser::parse_num_as_unsigned(cstring str, uintmax_t& out)
{
	out = 0;
	const char * ptr = (char*)str.bytes().ptr();
	const char * end = ptr + str.length();
	if (ptr == end) return false;
	
	if (*ptr == '-') return false;
	else if (*ptr == '+') ptr++;
	
	return parse_num_as_signless(ptr, end, out);
}
bool json5parser::parse_num_as_signed(cstring str, intmax_t& out)
{
	out = 0;
	const char * ptr = (char*)str.bytes().ptr();
	const char * end = ptr + str.length();
	if (ptr == end) return false;
	
	bool negative = false;
	if (*ptr == '+') ptr++;
	else if (*ptr == '-') { negative=true; ptr++; }
	
	uintmax_t tmp;
	if (!parse_num_as_signless(ptr, end, tmp)) return false;
	out = (intmax_t)tmp;
	if (out < 0) return false;
	
	if (negative) out = -out;
	return true;
}


string jsonwriter::strwrap(cstring s)
{
	const uint8_t * sp = s.bytes().ptr();
	const uint8_t * spe = sp + s.length();
	
	string out = "\"";
	const uint8_t * previt = sp;
	for (const uint8_t * it = sp; it < spe; it++)
	{
#ifdef __SSE2__
		if (spe-it >= 16)
		{
			__m128i chs = _mm_loadu_si128((__m128i*)it);
			
			__m128i bad1 = _mm_cmplt_epi8(_mm_xor_si128(chs, _mm_set1_epi8(0x82)), _mm_set1_epi8((int8_t)(0x21^0x80)));
			__m128i bad2 = _mm_or_si128(_mm_cmpeq_epi8(chs, _mm_set1_epi8(0x7F)), _mm_cmpeq_epi8(chs, _mm_set1_epi8(0x5C)));
			__m128i bad = _mm_or_si128(bad1, bad2);
			int mask = _mm_movemask_epi8(bad);
			if (mask == 0)
			{
				it += 16-1; // -1 for the it++ above (oddly enough, this way is faster)
				continue;
			}
			it += ctz32(mask);
		}
#endif
		
		uint8_t c = *it;
		// DEL is legal according to nst/JSONTestSuite, but let's avoid it anyways
		if ((c^2) <= 32 || c == '\\' || c == 0x7F)
		{
			out += arrayview<uint8_t>(previt, it-previt);
			previt = it+1;
			
			if(0);
			else if (c=='\n') out += "\\n";
			else if (c=='\r') out += "\\r";
			else if (c=='\t') out += "\\t";
			else if (c=='\b') out += "\\b";
			else if (c=='\f') out += "\\f";
			else if (c=='\"') out += "\\\"";
			else if (c=='\\') out += "\\\\";
			else out += "\\u"+tostringhex<4>(c);
		}
	}
	out += arrayview<uint8_t>(previt, spe-previt);
	return out+"\"";
}

void jsonwriter::comma()
{
	if (m_comma) m_data += ',';
	m_comma = true;
	
	if (UNLIKELY(!m_indent_disable))
	{
		if (m_indent_is_value)
		{
			m_data += ' ';
			m_indent_is_value = false;
		}
		else if (m_indent_depth)
		{
			cstring indent_str = (&"        "[8-m_indent_size]);
			m_data += '\n';
			for (int i=0;i<m_indent_depth;i++)
			{
				m_data += indent_str;
			}
		}
	}
}

void jsonwriter::null()
{
	comma();
	m_data += "null";
}
void jsonwriter::boolean(bool b)
{
	comma();
	m_data += b ? "true" : "false";
}
void jsonwriter::str(cstring s)
{
	comma();
	m_data += strwrap(s);
}
void jsonwriter::list_enter()
{
	comma();
	m_data += "[";
	m_comma = false;
	m_indent_depth++;
}
void jsonwriter::list_exit()
{
	m_data += "]";
	m_comma = true;
	m_indent_depth--;
}
void jsonwriter::map_enter()
{
	comma();
	m_data += "{";
	m_comma = false;
	m_indent_depth++;
}
void jsonwriter::map_key(cstring s)
{
	str(s);
	m_data += ":";
	m_comma = false;
	m_indent_is_value = true;
}
void jsonwriter::map_exit()
{
	m_data += "}";
	m_comma = true;
	m_indent_depth--;
}



JSONw JSONw::c_null;
map<string,JSONw> JSONw::c_null_map;

void JSONw::construct(jsonparser& p, jsonparser::event& ev, bool* ok, size_t maxdepth)
{
	if(0);
	else if (ev.type == jsonparser::unset) set_to<jsonparser::unset>();
	else if (ev.type == jsonparser::jtrue) set_to<jsonparser::jtrue>();
	else if (ev.type == jsonparser::jfalse) set_to<jsonparser::jfalse>();
	else if (ev.type == jsonparser::jnull) set_to<jsonparser::jnull>();
	else if (ev.type == jsonparser::str) set_to<jsonparser::str>(ev.str);
	else if (ev.type == jsonparser::num) set_to<jsonparser::num>(ev.str);
	else if (ev.type == jsonparser::error) { set_to<jsonparser::error>(); *ok = false; }
	else if (maxdepth == 0)
	{
		set_to<jsonparser::error>();
		*ok = false;
		if (ev.type == jsonparser::enter_list || ev.type == jsonparser::enter_map)
		{
			// it would be faster to reset the jsonparser somehow,
			// but performance on hostile inputs isn't really a priority
			size_t xdepth = 1;
			while (xdepth)
			{
				jsonparser::event next = p.next();
				if (next.type == jsonparser::enter_list) xdepth++;
				if (next.type == jsonparser::enter_map) xdepth++;
				if (next.type == jsonparser::exit_list) xdepth--;
				if (next.type == jsonparser::exit_map) xdepth--;
			}
		}
		return;
	}
	else if (ev.type == jsonparser::enter_list)
	{
		set_to<jsonparser::enter_list>();
		array<JSONw>& children = content.get<jsonparser::enter_list>();
		while (true)
		{
			jsonparser::event next = p.next();
			if (next.type == jsonparser::exit_list) break;
			if (next.type == jsonparser::error) *ok = false;
			children.append().construct(p, next, ok, maxdepth-1);
		}
	}
	else if (ev.type == jsonparser::enter_map)
	{
		set_to<jsonparser::enter_map>();
		map<string,JSONw>& children = content.get<jsonparser::enter_map>();
		while (true)
		{
			jsonparser::event next = p.next();
			if (next.type == jsonparser::exit_map) break;
			if (next.type == jsonparser::map_key)
			{
				jsonparser::event ev = p.next();
				children.insert(next.str).construct(p, ev, ok, maxdepth-1);
			}
			if (next.type == jsonparser::error) *ok = false;
		}
	}
}

bool JSON::parse(string s)
{
	// it would not do to have some function changing the value of null for everybody else
	// this check is only needed here; all of JSON's other members are conceptually const, though most aren't marked as such
	if (this == &c_null)
		abort();
	
	jsonparser p(std::move(s));
	bool ok = true;
	jsonparser::event ev = p.next();
	construct(p, ev, &ok, 1000);
	
	ev = p.next();
	if (!ok || ev.type != jsonparser::finish)
	{
		// this discards the entire object
		// but considering how rare damaged json is, that's acceptable
		set_to<jsonparser::error>();
		return false;
	}
	return true;
}

template<bool sort>
void JSON::serialize(jsonwriter& w) const
{
	switch (type())
	{
	case jsonparser::unset:
	case jsonparser::error:
	case jsonparser::jnull:
		w.null();
		break;
	case jsonparser::jtrue:
		w.boolean(true);
		break;
	case jsonparser::jfalse:
		w.boolean(false);
		break;
	case jsonparser::str:
		w.str(content.get<jsonparser::str>());
		break;
	case jsonparser::num:
		w.num_unsafe(content.get<jsonparser::num>());
		break;
	case jsonparser::enter_list:
		w.list_enter();
		for (const JSON& j : list())
			j.serialize<sort>(w);
		w.list_exit();
		break;
	case jsonparser::enter_map:
		w.map_enter();
		if (sort)
		{
			array<const map<string,JSONw>::node*> items;
			for (const map<string,JSONw>::node& e : assoc())
			{
				items.append(&e);
			}
			items.ssort([](const map<string,JSONw>::node* a, const map<string,JSONw>::node* b) { return string::less(a->key, b->key); });
			for (const map<string,JSONw>::node* e : items)
			{
				w.map_key(e->key);
				e->value.serialize<sort>(w);
			}
		}
		else
		{
			for (const map<string,JSONw>::node& e : assoc())
			{
				w.map_key(e.key);
				e.value.serialize<sort>(w);
			}
		}
		w.map_exit();
		break;
	default: abort(); // unreachable
	}
}

string JSON::serialize(int indent) const
{
	jsonwriter w(indent);
	serialize<false>(w);
	return w.finish();
}

string JSON::serialize_sorted(int indent) const
{
	jsonwriter w(indent);
	serialize<true>(w);
	return w.finish();
}



#include "test.h"
#ifdef ARLIB_TEST
#define e_jfalse jsonparser::jfalse
#define e_jtrue jsonparser::jtrue
#define e_jnull jsonparser::jnull
#define e_str jsonparser::str
#define e_num jsonparser::num
#define e_enter_list jsonparser::enter_list
#define e_exit_list jsonparser::exit_list
#define e_enter_map jsonparser::enter_map
#define e_map_key jsonparser::map_key
#define e_exit_map jsonparser::exit_map
#define e_error jsonparser::error
#define e_finish jsonparser::finish

static const char * test1 =
"\"x\"\n"
;

static jsonparser::event test1e[]={
	{ e_str, "x" },
	{ e_finish }
};

static const char * test2 =
"[ 1, 2.5e+1, 3, 4.5e+0, 6.7e+08 ]"
;

static jsonparser::event test2e[]={
	{ e_enter_list },
		{ e_num, "1" },
		{ e_num, "2.5e+1" },
		{ e_num, "3" },
		{ e_num, "4.5e+0" },
		{ e_num, "6.7e+08" },
	{ e_exit_list },
	{ e_finish }
};

static const char * test3 =
"\r\n\t {\r\n\t \"foo\":\r\n\t [\r\n\t true\r\n\t ,\r\n\t false, null\r\n\t ]\r\n\t }\r\n\t "
;

static jsonparser::event test3e[]={
	{ e_enter_map },
		{ e_map_key, "foo" },
		{ e_enter_list },
			{ e_jtrue },
			{ e_jfalse },
			{ e_jnull },
		{ e_exit_list },
	{ e_exit_map },
	{ e_finish }
};

static const char * test4 =
"{ \"a\": [ { \"b\": [ 1, 2 ], \"c\": [ 3, 4 ] }, { \"d\": [ 5, 6 ], "
"\"e\": [ \"this is a 31 byte string aaaaaa\", \"this is a 32 byte string aaaaaaa\", \"this is a 33 byte string aaaaaaaa\" ] } ],\n"
"  \"f\": [ { \"g\": [ 7, 8 ], \"h\": [ 9, 0 ] }, { \"i\": [ 1, \"\xC3\xB8\" ], \"j\": [ {}, \"x\\nx\x7fx\" ] },"
" \"\\\"\\\\\\/\" ] }"
;

static jsonparser::event test4e[]={
	{ e_enter_map },
		{ e_map_key, "a" },
		{ e_enter_list },
			{ e_enter_map },
				{ e_map_key, "b" },
				{ e_enter_list }, { e_num, "1" }, { e_num, "2" }, { e_exit_list },
				{ e_map_key, "c" },
				{ e_enter_list }, { e_num, "3" }, { e_num, "4" }, { e_exit_list },
			{ e_exit_map },
			{ e_enter_map },
				{ e_map_key, "d" },
				{ e_enter_list }, { e_num, "5" }, { e_num, "6" }, { e_exit_list },
				{ e_map_key, "e" },
				{ e_enter_list },
					{ e_str, "this is a 31 byte string aaaaaa" },
					{ e_str, "this is a 32 byte string aaaaaaa" },
					{ e_str, "this is a 33 byte string aaaaaaaa" },
				{ e_exit_list },
			{ e_exit_map },
		{ e_exit_list },
		{ e_map_key, "f" },
		{ e_enter_list },
			{ e_enter_map },
				{ e_map_key, "g" },
				{ e_enter_list }, { e_num, "7" }, { e_num, "8" }, { e_exit_list },
				{ e_map_key, "h" },
				{ e_enter_list }, { e_num, "9" }, { e_num, "0" }, { e_exit_list },
			{ e_exit_map },
			{ e_enter_map },
				{ e_map_key, "i" },
				{ e_enter_list }, { e_num, "1" }, { e_str, "\xC3\xB8" }, { e_exit_list },
				{ e_map_key, "j" },
				{ e_enter_list }, { e_enter_map }, { e_exit_map }, { e_str, "x\nx\x7fx" }, { e_exit_list },
			{ e_exit_map },
		{ e_str, "\"\\/" },
		{ e_exit_list },
	{ e_exit_map },
	{ e_finish }
};

static const char * test5 =
"{ \"foo\": \"\xC2\x80\\u0080\\ud83d\\udCA9\" }\n"
;

static jsonparser::event test5e[]={
	{ e_enter_map },
		{ e_map_key, "foo" },
		{ e_str, "\xC2\x80\xC2\x80\xF0\x9F\x92\xA9" },
	{ e_exit_map },
	{ e_finish }
};

static const char * test6 =
"\"#\\\n#\\\r#\\\r\n#\\\xE2\x80\xA8#\\\xE2\x80\xA9#\\\xE2\x80\xAA#\""
;

static jsonparser::event test6e[]={
	{ e_str, "######\xE2\x80\xAA#" },
	{ e_finish }
};

template<typename T>
static void testjson(cstring json, jsonparser::event* expected)
{
	T parser(json);
	while (true)
	{
		jsonparser::event actual = parser.next();
		
//printf("e=%d [%s]\n", expected->type, (const char*)expected->str.c_str());
//printf("a=%d [%s]\n\n", actual.type,  (const char*)actual.str.c_str());
		if (expected)
		{
			assert_eq(actual.type, expected->type);
			assert_eq(actual.str, expected->str);
			
			if (expected->type == e_finish) return;
		}
		if (actual.type == e_finish) return;
		
		expected++;
	}
}

template<typename T, bool valid=false>
static void testjson_error(cstring json)
{
	T parser(json);
	array<int> events;
	while (true)
	{
		jsonparser::event ev = parser.next();
		events.append(ev.type);
		assert_lt(events.size(), 1000); // fail on infinite error loops
		if (ev.type == e_finish) break;
	}
	assert_eq(events.contains(e_error), !valid); // any error is fine
	
	array<int> events1 = events;
	for (ssize_t n=events.size()-1;n>=0;n--)
	{
		if (events[n] == e_error)
			events.remove(n);
		if (events[n] == e_str || events[n] == e_num || events[n] == e_jtrue || events[n] == e_jfalse)
			events[n] = e_jnull;
		while (events[n] == e_enter_list && events[n+1] == e_jnull)
		{
			events.remove(n+1);
		}
		if (events[n] == e_enter_list && events[n+1] == e_exit_list)
		{
			events[n] = e_jnull;
			events.remove(n+1);
		}
		while (events[n] == e_enter_map && events[n+1] == e_map_key && events[n+2] == e_jnull)
		{
			events.remove(n+1);
			events.remove(n+1);
		}
		if (events[n] == e_enter_map && events[n+1] == e_exit_map)
		{
			events[n] = e_jnull;
			events.remove(n+1);
		}
	}
	if (events[0] == e_jnull)
		events.remove(0);
	else
		assert(!valid);
	assert(events[0] == e_finish);
	
	//enum {
		//unset      = 0,
		//jtrue      = 1,
		//jfalse     = 2,
		//jnull      = 3,
		//str        = 4,
		//num        = 5,
		//enter_list = 6,
		//exit_list  = 7,
		//enter_map  = 8,
		//map_key    = 9,
		//exit_map   = 10,
		//error      = 11,
		//finish     = 12,
	//};
	
	//string evs_str = tostring_dbg(events);
	//if (evs_str == "[3,12]") {}
	//else if (!valid && evs_str == "[12]") {}
	//else
	//{
		//puts(tostring_dbg(events1)+tostring_dbg(events)+json);
		//assert(false);
	//}
}

template<typename T, bool json5>
static void testjson_all()
{
	testcall(testjson<T>(test1, test1e));
	testcall(testjson<T>(test2, test2e));
	testcall(testjson<T>(test3, test3e));
	testcall(testjson<T>(test4, test4e));
	testcall(testjson<T>(test5, test5e));
	if (json5)
		testcall(testjson<T>(test6, test6e));
	
	testcall(testjson_error<T>(""));
	testcall(testjson_error<T>("           "));
	testcall(testjson_error<T>("{"));
	testcall(testjson_error<T>("{\"a\""));
	testcall(testjson_error<T>("{\"a\":"));
	testcall(testjson_error<T>("{\"a\":1"));
	testcall(testjson_error<T>("{\"a\":1,"));
	testcall(testjson_error<T>("{\"a\":}"));
	testcall(testjson_error<T>("["));
	testcall(testjson_error<T>("[1"));
	testcall(testjson_error<T>("[1,"));
	testcall(testjson_error<T>("\""));
	testcall(testjson_error<T>("01"));
	testcall(testjson_error<T>("1,2,3"));
	testcall(testjson_error<T>("1.23.4"));
	testcall(testjson_error<T>("0x120x34"));
	testcall(testjson_error<T>("1e"));
	testcall(testjson_error<T>("1e+"));
	testcall(testjson_error<T>("1e-"));
	testcall(testjson_error<T>("z"));
	testcall(testjson_error<T>("{ \"a\":1, \"b\":2, \"q\":*, \"a\":3, \"a\":4 }"));
	testcall(testjson_error<T>("\""));
	testcall(testjson_error<T>("\"\\"));
	testcall(testjson_error<T>("\"\\u"));
	testcall(testjson_error<T>("\"\\u1"));
	testcall(testjson_error<T>("\"\\u12"));
	testcall(testjson_error<T>("\"\\u123"));
	testcall(testjson_error<T>("\"\\u1234"));
	
	//try to make it read out of bounds
	//input length 31
	testcall(testjson_error<T>("\"this is a somewhat longer str\\u"));
	testcall(testjson_error<T>("\"this is a somewhat longer st\\u1"));
	testcall(testjson_error<T>("\"this is a somewhat longer s\\u12"));
	testcall(testjson_error<T>("\"this is a somewhat longer \\u123"));
	testcall(testjson_error<T>("\"this is a somewhat longer\\u1234"));
	//input length 32
	testcall(testjson_error<T>("\"this is a somewhat longer stri\\u"));
	testcall(testjson_error<T>("\"this is a somewhat longer str\\u1"));
	testcall(testjson_error<T>("\"this is a somewhat longer st\\u12"));
	testcall(testjson_error<T>("\"this is a somewhat longer s\\u123"));
	testcall(testjson_error<T>("\"this is a somewhat longer \\u1234"));
	//input length 15
	testcall(testjson_error<T>("\"short string \\u"));
	testcall(testjson_error<T>("\"short string\\u1"));
	testcall(testjson_error<T>("\"short strin\\u12"));
	testcall(testjson_error<T>("\"short stri\\u123"));
	testcall(testjson_error<T>("\"short str\\u1234"));
	testcall(testjson_error<T>("                    f"));
	testcall(testjson_error<T>("                    fa"));
	testcall(testjson_error<T>("                    fal"));
	testcall(testjson_error<T>("                    fals"));
	testcall(testjson_error<T>("                    t"));
	testcall(testjson_error<T>("                    tr"));
	testcall(testjson_error<T>("                    tru"));
	testcall(testjson_error<T>("                    n"));
	testcall(testjson_error<T>("                    nu"));
	testcall(testjson_error<T>("                    nul"));
	
	testcall(testjson_error<T>("[]\1"));
	testcall(testjson_error<T>("[],"));
	testcall(testjson_error<T>("123,"));
	testcall(testjson_error<T>("[1,,2]"));
	
	testcall(testjson_error<T>("123"+string::nul()));
	testcall(testjson_error<T>("[]"+string::nul()));
	
	testcall(testjson_error<T>("{                   \"\\uDBBB\\uDB"));
	
	testcall(testjson_error<T,json5>("1.")); // invalid JSON, but legal JSON5
	testcall(testjson_error<T,json5>("1 // comment"));
	testcall(testjson_error<T,json5>("1 // comment\n"));
	testcall(testjson_error<T,json5>("// comment\n1 // comment"));
	testcall(testjson_error<T,json5>("1 /* comment */"));
	testcall(testjson_error<T,json5>("/* comment */ 1"));
	testcall(testjson_error<T,json5>("\"\\x41\""));
	testcall(testjson_error<T,json5>("{unquoted:1}"));
	testcall(testjson_error<T,json5>("'single quoted'"));
	testcall(testjson_error<T,json5>("\"line\\\nbreak\""));
	testcall(testjson_error<T,json5>("\"line\\\rbreak\""));
	testcall(testjson_error<T,json5>("\"line\\\r\nbreak\""));
	testcall(testjson_error<T,json5>("\"#\\\xE2\x80\xA8#\\\xE2\x80\xA9#\""));
	testcall(testjson_error<T,json5>("-Infinity"));
	testcall(testjson_error<T,json5>("+NaN"));
	testcall(testjson_error<T,json5>("1\f\v"));
	testcall(testjson_error<T,json5>("0x42"));
	testcall(testjson_error<T,json5>("+1"));
	testcall(testjson_error<T,json5>("[\f]"));
	testcall(testjson_error<T,json5>("[\v]"));
	testcall(testjson_error<T,json5>("[\"\t\"]"));
	testcall(testjson_error<T,json5>("\"\\v\""));
	testcall(testjson_error<T,json5>("[1,]"));
	testcall(testjson_error<T,json5>("{\"a\":0,}"));
	testcall(testjson_error<T,json5>("[-.123]"));
	testcall(testjson_error<T,json5>("[.123]"));
	testcall(testjson_error<T,json5>("[123.,]"));
	testcall(testjson_error<T,json5>("['\"',\"'\"]"));
	testcall(testjson_error<T,json5>("\"\\U0001f4a9\"")); // though some of them really shouldn't be...
	testcall(testjson_error<T>("{unquoted☃:1}"));
	testcall(testjson_error<T>("{not-valid:1}"));
	testcall(testjson_error<T>("5 /*"));
	testcall(testjson_error<T,json5>("\"a\\0b\""));
	testcall(testjson_error<T>("\"a\\01b\""));
	testcall(testjson_error<T>("\"a\\1b\""));
	testcall(testjson_error<T,json5>(R"({
  // comments
  unquoted: 'and you can quote me on that',
  singleQuotes: 'I can use "double quotes" here',
  lineBreaks: "Look, Mom! \
No \\n's!",
  hexadecimal: 0xdecaf,
  leadingDecimalPoint: .8675309, andTrailing: 8675309.,
  positiveSign: +1,
  trailingComma: 'in objects', andIn: ['arrays',],
  "backwardsCompatible": "with JSON",
}
)"));
}

test("JSON parser", "string", "json") { testjson_all<jsonparser, false>(); }
test("JSON5 parser", "string", "json") { testjson_all<json5parser, true>(); }

test("JSON container", "string,array,set", "json")
{
	{
		JSON json("7");
		assert_eq((int)json, 7);
	}
	
	{
		JSON json("\"42\"");
		assert_eq(json.str(), "42");
	}
	
	{
		JSON json("                    false");
		assert_eq(json.type(), jsonparser::jfalse);
	}
	{
		JSON json("                    true");
		assert_eq(json.type(), jsonparser::jtrue);
	}
	{
		JSON json("                    null");
		assert_eq(json.type(), jsonparser::jnull);
	}
	
	{
		JSON json("[1,2,3]");
		assert_eq((int)json[0], 1);
		assert_eq((int)json[1], 2);
		assert_eq((int)json[2], 3);
	}
	
	{
		JSON json("{\"a\":null,\"b\":true,\"c\":false}");
		assert_eq((bool)json["a"], false);
		assert_eq((bool)json["b"], true);
		assert_eq((bool)json["c"], false);
	}
	
	{
		JSON("["); // these pass if they do not yield infinite loops
		JSON("[{}");
		JSON("[[]");
		JSON("[{},");
		JSON("[[],");
		JSON("{");
		JSON("{\"x\"");
		JSON("{\"x\":");
	}
	
	{
		JSONw json;
		json["a"] = 1;
		json["b"] = 2;
		json["c"] = 3;
		json["d"] = 4;
		json["e"] = 5;
		json["f"] = 6;
		json["g"] = 7;
		json["h"] = 8;
		json["i"] = 9;
		assert_eq(json.serialize_sorted(), R"({"a":1,"b":2,"c":3,"d":4,"e":5,"f":6,"g":7,"h":8,"i":9})");
		assert_ne(json.serialize(),        R"({"a":1,"b":2,"c":3,"d":4,"e":5,"f":6,"g":7,"h":8,"i":9})");
	}
	
	{
		JSONw json;
		json["\x01\x02\x03\n\\n☃\""] = "\x01\x02\x03\n\\n☃\"";
		assert_eq(json.serialize(), R"({"\u0001\u0002\u0003\n\\n☃\"":"\u0001\u0002\u0003\n\\n☃\""})");
	}
	
	{
		JSONw json;
		json[0] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
		json[1] = "aaaaaaaa\"aaaaaaaaaaaaaaaaaaaaaaa";
		json[2] = "aaaaaaaa\x1f""aaaaaaaaaaaaaaaaaaaaaaa";
		json[3] = "aaaaaaaa aaaaaaaaaaaaaaaaaaaaaaa";
		json[4] = "aaaaaaaa\\aaaaaaaaaaaaaaaaaaaaaaa";
		json[5] = 123;
		assert_eq((cstring)json[0], "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		assert_eq((double)json[5], 123.0);
		assert_eq((size_t)json[5], 123);
		assert_eq(json.serialize(), R"(["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaa\"aaaaaaaaaaaaaaaaaaaaaaa",)"
		           R"("aaaaaaaa\u001Faaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaa aaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaa\\aaaaaaaaaaaaaaaaaaaaaaa",123])");
	}
	
	if (false) // disabled, JSON::construct runs through all 200000 events from the jsonparser which is slow
	{
		char spam[200001];
		for (int i=0;i<100000;i++)
		{
			spam[i] = '[';
			spam[i+100000] = ']';
		}
		spam[200000] = '\0';
		JSON((char*)spam); // must not overflow the stack (pointless cast to avoid some c++ language ambiguity)
	}
	
	const char * h = R"({"ok":true,"messages":[{"client_msg_id":"19a4f3ca-5afa-44bc-b3e4-5c8026482eb3","type":"message","text":"Will be 5 min late for standup","user":"U03EE8T0HM2","ts":"1694675426.587709","blocks":[],"team":"T03E45N2BFF","thread_ts":"1694675426.587709","reply_count":9,"reply_users_count":2,"latest_reply":"1694676264.646619","reply_users":["U045LHWEFUN","U041YPW0T0A"],"is_locked":false,"subscribed":false,"reactions":[{"name":"+1","users":["U041YPW0T0A","U045LHWEFUN","U0403TT304S"],"count":3},{"name":"alarm_clock","users":["U045LHWEFUN","U0403TT304S"],"count":2},{"name":"rotating_light","users":["U045LHWEFUN","U0403TT304S"],"count":2}]},{"client_msg_id":"43b2a66f-a7ef-4ad7-bf29-506b463f04f2","type":"message","text":"<https:\/\/www.youtube.com\/watch?v=x7yxc8LmEy0>\n\n<https:\/\/www.youtube.com\/watch?v=g84cg2KvJxY>\n\nA couple of example processes being done by customers today in off-road domains.","user":"U03E73KK3D1","ts":"1694590428.592999","blocks":[{"type":"rich_text","block_id":"2Cn0","elements":[{"type":"rich_text_section","elements":[{"type":"link","url":"https:\/\/www.youtube.com\/watch?v=x7yxc8LmEy0"},{"type":"text","text":"\n\n"},{"type":"link","url":"https:\/\/www.youtube.com\/watch?v=g84cg2KvJxY"},{"type":"text","text":"\n\nA couple of example processes being done by customers today in off-road domains."}]}]}],"team":"T03E45N2BFF","attachments":[{"from_url":"https:\/\/www.youtube.com\/watch?v=x7yxc8LmEy0","service_icon":"https:\/\/a.slack-edge.com\/80588\/img\/unfurl_icons\/youtube.png","thumb_url":"https:\/\/i.ytimg.com\/vi\/x7yxc8LmEy0\/hqdefault.jpg","thumb_width":480,"thumb_height":360,"video_html":"<iframe width=\"400\" height=\"225\" src=\"https:\/\/www.youtube.com\/embed\/x7yxc8LmEy0?feature=oembed&autoplay=1&iv_load_policy=3\" frameborder=\"0\" allow=\"accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share\" allowfullscreen title=\"Create a flowing wheat field in Nvidia Omniverse Create. Millions of instances rendered in realtime!\"><\/iframe>","video_html_width":400,"video_html_height":225,"id":1,"original_url":"https:\/\/www.youtube.com\/watch?v=x7yxc8LmEy0","fallback":"YouTube Video: Create a flowing wheat field in Nvidia Omniverse Create. Millions of instances rendered in realtime!","title":"Create a flowing wheat field in Nvidia Omniverse Create. Millions of instances rendered in realtime!","title_link":"https:\/\/www.youtube.com\/watch?v=x7yxc8LmEy0","author_name":"edstudios","author_link":"https:\/\/www.youtube.com\/@edstudios","service_name":"YouTube","service_url":"https:\/\/www.youtube.com\/"},{"from_url":"https:\/\/www.youtube.com\/watch?v=g84cg2KvJxY","service_icon":"https:\/\/a.slack-edge.com\/80588\/img\/unfurl_icons\/youtube.png","thumb_url":"https:\/\/i.ytimg.com\/vi\/g84cg2KvJxY\/hqdefault.jpg","thumb_width":480,"thumb_height":360,"video_html":"<iframe width=\"400\" height=\"225\" src=\"https:\/\/www.youtube.com\/embed\/g84cg2KvJxY?feature=oembed&autoplay=1&iv_load_policy=3\" frameborder=\"0\" allow=\"accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share\" allowfullscreen title=\"7 Minute Photoreal Forest!  Procedural Content Generation PCG in Unreal Engine\"><\/iframe>","video_html_width":400,"video_html_height":225,"id":2,"original_url":"https:\/\/www.youtube.com\/watch?v=g84cg2KvJxY","fallback":"YouTube Video: 7 Minute Photoreal Forest!  Procedural Content Generation PCG in Unreal Engine","title":"7 Minute Photoreal Forest!  Procedural Content Generation PCG in Unreal Engine","title_link":"https:\/\/www.youtube.com\/watch?v=g84cg2KvJxY","author_name":"Aziel Arts","author_link":"https:\/\/www.youtube.com\/@azielarts","service_name":"YouTube","service_url":"https:\/\/www.youtube.com\/"}],"reactions":[{"name":"eyes","users":["U045LHWEFUN"],"count":1}]},{"client_msg_id":"16d12612-0d5a-4f2b-9082-9127cb6c0f93","type":"message","text":"oh no\nsame","user":"U041YPW0T0A","ts":"1694501681.581539","blocks":[{"type":"rich_text","block_id":"63\/vU","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"oh no\nsame"}]}]}],"team":"T03E45N2BFF"},{"client_msg_id":"F9FDC7AA-F677-4FDA-9FD6-711028F2D54F","type":"message","text":"I\u2019ll be a few mins late! ","user":"U03EKMGNFHP","ts":"1694501605.064989","blocks":[{"type":"rich_text","block_id":"YOf","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"I"},{"type":"text","text":"\u2019"},{"type":"text","text":"ll be a few mins late! "}]}]}],"team":"T03E45N2BFF"},{"client_msg_id":"801744EE-9E2A-4403-A1BC-1773821D39BE","type":"message","text":"My usual bus got cancelled, but I assume I\u2019ll still be at the office for the standup","user":"U045LHWEFUN","ts":"1694413131.483699","blocks":[{"type":"rich_text","block_id":"QhR2C","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"My usual bus got cancelled, but I assume I\u2019ll still be at the office for the standup"}]}]}],"team":"T03E45N2BFF","reactions":[{"name":"bus","users":["U03E73KK3D1"],"count":1}]},{"client_msg_id":"3DBED80A-C1F1-4930-A261-0A1798DEFBA7","type":"message","text":"Wfh :male-technologist:  ","user":"U03E73KK3D1","ts":"1694410942.359489","blocks":[{"type":"rich_text","block_id":"GVRs","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"Wfh "},{"type":"emoji","name":"male-technologist","unicode":"1f468-200d-1f4bb"},{"type":"text","text":"  "}]}]}],"team":"T03E45N2BFF","reactions":[{"name":"+1","users":["U045LHWEFUN","U03EE8T0HM2"],"count":2}]},{"client_msg_id":"3baa329b-0917-4c0a-9a58-d71c8c7f7334","type":"message","text":"Will continue WFH, feeling better but not gr8. We can take final on Friday","user":"U03EE8T0HM2","ts":"1694070690.053979","blocks":[{"type":"rich_text","block_id":"99dY","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"Will continue WFH, feeling better but not gr8. We can take final on Friday"}]}]}],"team":"T03E45N2BFF","reactions":[{"name":"grapes","users":["U045LHWEFUN"],"count":1},{"name":"+1","users":["U045LHWEFUN"],"count":1},{"name":"baguette_bread","users":["U041YPW0T0A","U03E73KK3D1"],"count":2},{"name":"sneezing_face","users":["U03E0DNBTJA"],"count":1}]},{"client_msg_id":"8dcb34f7-399e-45a0-88e6-171389c91fe5","type":"message","text":"still wfh","user":"U041YPW0T0A","ts":"1694070111.209519","blocks":[{"type":"rich_text","block_id":"HWQE","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"still wfh"}]}]}],"team":"T03E45N2BFF","reactions":[{"name":"+1","users":["U045LHWEFUN"],"count":1}]},{"client_msg_id":"6AEA8A5B-1CFA-4EF9-B0FD-4F056FECEEF8","type":"message","text":"I\u2019m feeling a little odd today, so I think I\u2019ll work from home at least during the morning ","user":"U045LHWEFUN","ts":"1694066064.571979","blocks":[{"type":"rich_text","block_id":"xMG","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"I"},{"type":"text","text":"\u2019"},{"type":"text","text":"m feeling a little odd today, so I think I\u2019ll work from home at least during the morning "}]}]}],"team":"T03E45N2BFF","reactions":[{"name":"+1::skin-tone-2","users":["U03E73KK3D1"],"count":1},{"name":"+1","users":["U0403TT304S"],"count":1},{"name":"nauseated_face","users":["U041YPW0T0A"],"count":1},{"name":"pill","users":["U05AHPTAWHX"],"count":1}]},{"type":"message","text":"BMW group is using roadrunner but wishes it had more procedural tools -- a good opportunity for us. Can we reach out to them? (_Hubert Cao, BMW Group_)\n<https:\/\/se.mathworks.com\/videos\/virtual-world-generation-for-bmw-driving-simulation-1691430718131.html?s_tid=srchtitle>","files":[{"id":"F05RKCF5DUH","created":1694012059,"timestamp":1694012059,"name":"image.png","title":"image.png","mimetype":"image\/png","filetype":"png","pretty_type":"PNG","user":"U05AHPTAWHX","user_team":"T03E45N2BFF","editable":false,"size":921379,"mode":"hosted","is_external":false,"external_type":"","is_public":true,"public_url_shared":false,"display_as_bot":false,"username":"","url_private":"https:\/\/files.slack.com\/files-pri\/T03E45N2BFF-F05RKCF5DUH\/image.png","url_private_download":"https:\/\/files.slack.com\/files-pri\/T03E45N2BFF-F05RKCF5DUH\/download\/image.png","media_display_type":"unknown","thumb_64":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RKCF5DUH-80f962d73f\/image_64.png","thumb_80":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RKCF5DUH-80f962d73f\/image_80.png","thumb_360":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RKCF5DUH-80f962d73f\/image_360.png","thumb_360_w":360,"thumb_360_h":152,"thumb_480":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RKCF5DUH-80f962d73f\/image_480.png","thumb_480_w":480,"thumb_480_h":203,"thumb_160":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RKCF5DUH-80f962d73f\/image_160.png","thumb_720":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RKCF5DUH-80f962d73f\/image_720.png","thumb_720_w":720,"thumb_720_h":304,"thumb_800":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RKCF5DUH-80f962d73f\/image_800.png","thumb_800_w":800,"thumb_800_h":338,"thumb_960":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RKCF5DUH-80f962d73f\/image_960.png","thumb_960_w":960,"thumb_960_h":406,"thumb_1024":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RKCF5DUH-80f962d73f\/image_1024.png","thumb_1024_w":1024,"thumb_1024_h":433,"original_w":2488,"original_h":1051,"thumb_tiny":"AwAUADDR\/i6U7mmk89BS8Y6UAG4e9GfrRk\/3TRn1FAEdwQseT0FZLEOzlh16Y7Voajn7OP8AeFZwxtPXOePSn0A3M0UlLSAKQgHqKWigCrqP\/HuP96svPNaeo\/8AHuP94Vmd6fQD\/9k=","permalink":"https:\/\/repli5.slack.com\/files\/U05AHPTAWHX\/F05RKCF5DUH\/image.png","permalink_public":"https:\/\/slack-files.com\/T03E45N2BFF-F05RKCF5DUH-64775186d2","is_starred":false,"has_rich_preview":false,"file_access":"visible"},{"id":"F05RVU24VG8","created":1694012208,"timestamp":1694012208,"name":"image.png","title":"image.png","mimetype":"image\/png","filetype":"png","pretty_type":"PNG","user":"U05AHPTAWHX","user_team":"T03E45N2BFF","editable":false,"size":231823,"mode":"hosted","is_external":false,"external_type":"","is_public":true,"public_url_shared":false,"display_as_bot":false,"username":"","url_private":"https:\/\/files.slack.com\/files-pri\/T03E45N2BFF-F05RVU24VG8\/image.png","url_private_download":"https:\/\/files.slack.com\/files-pri\/T03E45N2BFF-F05RVU24VG8\/download\/image.png","media_display_type":"unknown","thumb_64":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RVU24VG8-1ea635045e\/image_64.png","thumb_80":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RVU24VG8-1ea635045e\/image_80.png","thumb_360":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RVU24VG8-1ea635045e\/image_360.png","thumb_360_w":360,"thumb_360_h":170,"thumb_480":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RVU24VG8-1ea635045e\/image_480.png","thumb_480_w":480,"thumb_480_h":227,"thumb_160":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RVU24VG8-1ea635045e\/image_160.png","thumb_720":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RVU24VG8-1ea635045e\/image_720.png","thumb_720_w":720,"thumb_720_h":341,"thumb_800":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RVU24VG8-1ea635045e\/image_800.png","thumb_800_w":800,"thumb_800_h":379,"thumb_960":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RVU24VG8-1ea635045e\/image_960.png","thumb_960_w":960,"thumb_960_h":454,"thumb_1024":"https:\/\/files.slack.com\/files-tmb\/T03E45N2BFF-F05RVU24VG8-1ea635045e\/image_1024.png","thumb_1024_w":1024,"thumb_1024_h":485,"original_w":1684,"original_h":797,"thumb_tiny":"AwAWADDSY4HTJPFAz3AH0NI3UZOOeKCpzkMevSgBec9sUc56cetLn3oyPWgBCSOgFKKYzrtPzDpRH0P1\/oKAHFQwwRmk2DGBwKdRQAwpxgEj3pVTbTqKAEC47k0oGKKKAP\/Z","permalink":"https:\/\/repli5.slack.com\/files\/U05AHPTAWHX\/F05RVU24VG8\/image.png","permalink_public":"https:\/\/slack-files.com\/T03E45N2BFF-F05RVU24VG8-882eb3adaa","is_starred":false,"has_rich_preview":false,"file_access":"visible"}],"upload":false,"user":"U05AHPTAWHX","ts":"1694012273.913339","blocks":[{"type":"rich_text","block_id":"fLb","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"BMW group is using roadrunner but wishes it had more procedural tools -- a good opportunity for us. Can we reach out to them? ("},{"type":"text","text":"Hubert Cao, BMW Group","style":{"italic":true}},{"type":"text","text":")\n"},{"type":"link","url":"https:\/\/se.mathworks.com\/videos\/virtual-world-generation-for-bmw-driving-simulation-1691430718131.html?s_tid=srchtitle"}]}]}],"client_msg_id":"29030280-5f2d-42d1-a07b-5b7db25264a1","attachments":[{"image_url":"https:\/\/se.mathworks.com\/content\/dam\/mathworks\/videos\/d\/design-scenes-drive-sim-bmw.mp4\/jcr:content\/renditions\/design-scenes-drive-sim-bmw-thumbnail.jpg","image_width":640,"image_height":360,"image_bytes":98982,"from_url":"https:\/\/se.mathworks.com\/videos\/virtual-world-generation-for-bmw-driving-simulation-1691430718131.html?s_tid=srchtitle","service_icon":"https:\/\/se.mathworks.com\/etc.clientlibs\/mathworks\/clientlibs\/customer-ui\/templates\/common\/resources\/images\/favicon.20230810122605597.ico","id":1,"original_url":"https:\/\/se.mathworks.com\/videos\/virtual-world-generation-for-bmw-driving-simulation-1691430718131.html?s_tid=srchtitle","fallback":"Virtual World Generation for BMW Driving Simulation Video","text":"Discover how BMW uses RoadRunner as part of its virtual world generation process to meet the requirements for high-fidelity simulation of real-life roads and traffic scenarios.","title":"Virtual World Generation for BMW Driving Simulation Video","title_link":"https:\/\/se.mathworks.com\/videos\/virtual-world-generation-for-bmw-driving-simulation-1691430718131.html?s_tid=srchtitle","service_name":"se.mathworks.com"}],"reactions":[{"name":"eyes","users":["U03EKMGNFHP","U045LHWEFUN","U03E0DNBTJA"],"count":3}]},{"client_msg_id":"6220ea94-cb54-4716-9049-6d7ae1c3a625","type":"message","text":"hi same","user":"U041YPW0T0A","ts":"1693983212.901749","blocks":[{"type":"rich_text","block_id":"xTJk","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"hi same"}]}]}],"team":"T03E45N2BFF","reactions":[{"name":"chicken","users":["U045LHWEFUN"],"count":1},{"name":"nauseated_face","users":["U041YPW0T0A"],"count":1},{"name":"face_vomiting","users":["U041YPW0T0A"],"count":1},{"name":"moyai","users":["U041YPW0T0A"],"count":1},{"name":"ab","users":["U041YPW0T0A"],"count":1},{"name":"b","users":["U041YPW0T0A"],"count":1},{"name":"pill","users":["U05AHPTAWHX"],"count":1}]},{"client_msg_id":"9b5618cd-1d01-4997-9a31-082008f32178","type":"message","text":"WFH, still not feeling gr8","user":"U03EE8T0HM2","ts":"1693983077.322069","blocks":[{"type":"rich_text","block_id":"iMdRM","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"WFH, still not feeling gr8"}]}]}],"team":"T03E45N2BFF","edited":{"user":"U03EE8T0HM2","ts":"1693983118.000000"},"reactions":[{"name":"8ball","users":["U03E73KK3D1","U0403TT304S","U045LHWEFUN"],"count":3},{"name":"pill","users":["U05AHPTAWHX"],"count":1}]},{"client_msg_id":"4e10e8aa-1d1d-4ba3-be75-5f8fb4d013c3","type":"message","text":"<https:\/\/www.linkedin.com\/posts\/triangraphics-gmbh_dsc2023europevr-terraingeneration-trian3dbuilder-activity-7104791210350792704-nRPR?utm_source=share&amp;utm_medium=member_desktop|https:\/\/www.linkedin.com\/posts\/triangraphics-gmbh_dsc2023europevr-terraingeneration-t[\u2026]210350792704-nRPR?utm_source=share&amp;utm_medium=member_desktop>\n\n<@U0403TT304S>","user":"U03E73KK3D1","ts":"1693981066.509979","blocks":[{"type":"rich_text","block_id":"Z0yg","elements":[{"type":"rich_text_section","elements":[{"type":"link","url":"https:\/\/www.linkedin.com\/posts\/triangraphics-gmbh_dsc2023europevr-terraingeneration-trian3dbuilder-activity-7104791210350792704-nRPR?utm_source=share&utm_medium=member_desktop","text":"https:\/\/www.linkedin.com\/posts\/triangraphics-gmbh_dsc2023europevr-terraingeneration-t[\u2026]210350792704-nRPR?utm_source=share&utm_medium=member_desktop"},{"type":"text","text":"\n\n"},{"type":"user","user_id":"U0403TT304S"}]}]}],"team":"T03E45N2BFF","attachments":[{"from_url":"https:\/\/www.linkedin.com\/posts\/triangraphics-gmbh_dsc2023europevr-terraingeneration-trian3dbuilder-activity-7104791210350792704-nRPR?utm_source=share&utm_medium=member_desktop","thumb_url":"https:\/\/media.licdn.com\/dms\/image\/D4E05AQG53iuuMB0Qkw\/videocover-high\/0\/1693914190996?e=2147483647&v=beta&t=h3vDLJk5qD0RJgTUO9lKCh2bBcTnTXGs_Yf4M2hvT9M","thumb_width":1280,"thumb_height":720,"image_url":"https:\/\/media.licdn.com\/dms\/image\/D4E05AQG53iuuMB0Qkw\/videocover-high\/0\/1693914190996?e=2147483647&v=beta&t=h3vDLJk5qD0RJgTUO9lKCh2bBcTnTXGs_Yf4M2hvT9M","image_width":1280,"image_height":720,"image_bytes":8004,"service_icon":"https:\/\/static.licdn.com\/aero-v1\/sc\/h\/al2o9zrvru7aqj8e1x2rzsrca","id":1,"original_url":"https:\/\/www.linkedin.com\/posts\/triangraphics-gmbh_dsc2023europevr-terraingeneration-trian3dbuilder-activity-7104791210350792704-nRPR?utm_source=share&amp;utm_medium=member_desktop","fallback":"TrianGraphics on LinkedIn: #dsc2023europevr #terraingeneration #trian3dbuilder #dscantibes\u2026","text":"\ud835\udde7\ud835\uddee\ud835\uddf8\ud835\uddf2 \ud835\uddee \ud835\uddfd\ud835\uddf2\ud835\uddf2\ud835\uddf8 \ud835\uddee\ud835\ude01 \ud835\uddfc\ud835\ude02\ud835\uddff \ud835\udde3\ud835\ude02\ud835\ude07\ud835\ude07\ud835\uddf9\ud835\uddf2 \ud835\uddd7\ud835\uddee\ud835\ude01\ud835\uddee\ud835\uddef\ud835\uddee\ud835\ude00\ud835\uddf2 \ud835\uddf6\ud835\uddfb \ud835\ude01\ud835\uddf5\ud835\uddf6\ud835\ude00 \ud835\ude03\ud835\uddf6\ud835\uddf1\ud835\uddf2\ud835\uddfc!\nVisit us at booth 23 during DSC 2023 EUROPE VR to experience it live.\u2026","title":"TrianGraphics on LinkedIn: #dsc2023europevr #terraingeneration #trian3dbuilder #dscantibes\u2026","title_link":"https:\/\/www.linkedin.com\/posts\/triangraphics-gmbh_dsc2023europevr-terraingeneration-trian3dbuilder-activity-7104791210350792704-nRPR?utm_source=share&utm_medium=member_desktop","service_name":"linkedin.com"}],"reactions":[{"name":"eyes","users":["U045LHWEFUN"],"count":1}]},{"client_msg_id":"c0104394-65b4-47d7-b808-d0e3b501baee","type":"message","text":"interesting procedural town building demo <https:\/\/oskarstalberg.com\/Townscaper\/#GSB0RARueC6Snc9E0lO5B>","user":"U05AHPTAWHX","ts":"1693906230.618569","blocks":[{"type":"rich_text","block_id":"ZCn","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"interesting procedural town building demo "},{"type":"link","url":"https:\/\/oskarstalberg.com\/Townscaper\/#GSB0RARueC6Snc9E0lO5B"}]}]}],"team":"T03E45N2BFF","thread_ts":"1693906230.618569","reply_count":1,"reply_users_count":1,"latest_reply":"1693906438.647699","reply_users":["U045LHWEFUN"],"is_locked":false,"subscribed":false,"reactions":[{"name":"eyes","users":["U045LHWEFUN"],"count":1}]},{"client_msg_id":"ad403f46-6b34-4a8f-a127-6c9749c92376","type":"message","text":"Could not fall asleep yesterday for hours and woke up now with a massive headache. sorry for missing meeting, but will wfh","user":"U03DXABDN23","ts":"1693902498.015979","blocks":[{"type":"rich_text","block_id":"af40a","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"Could not fall asleep yesterday for hours and woke up now with a massive headache. sorry for missing meeting, but will wfh"}]}]}],"team":"T03E45N2BFF"},{"client_msg_id":"7B4FC0AC-45B6-4D14-BE33-F8D1B42CA4A0","type":"message","text":"Going to be late, trams delayed ","user":"U0403TT304S","ts":"1693897153.169979","blocks":[{"type":"rich_text","block_id":"+CL","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"Going to be late, trams delayed "}]}]}],"team":"T03E45N2BFF","reactions":[{"name":"moyai","users":["U041YPW0T0A"],"count":1},{"name":"seal","users":["U03EE8T0HM2"],"count":1}]},{"client_msg_id":"2e35d367-20ea-400e-8588-7ad203270ad9","type":"message","text":"wfh til lunch","user":"U03EKMGNFHP","ts":"1693896541.583069","blocks":[{"type":"rich_text","block_id":"ooRN","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"wfh til lunch"}]}]}],"team":"T03E45N2BFF","reactions":[{"name":"face_vomiting","users":["U041YPW0T0A"],"count":1}]},{"client_msg_id":"c97c9d61-c41e-41bd-a052-e71a498c87fc","type":"message","text":"WFH today","user":"U03EE8T0HM2","ts":"1693896501.085009","blocks":[{"type":"rich_text","block_id":"CZba","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"WFH today"}]}]}],"team":"T03E45N2BFF","reactions":[{"name":"nauseated_face","users":["U041YPW0T0A"],"count":1},{"name":"pill","users":["U05AHPTAWHX"],"count":1}]},{"client_msg_id":"c55ae363-2515-4d21-bae1-de81d932f0fc","type":"message","text":"ok\nim wfh tomorrow too because the runny nose seems to be continuing","user":"U041YPW0T0A","ts":"1693862586.820509","blocks":[{"type":"rich_text","block_id":"c7vJ","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"ok\nim wfh tomorrow too because the runny nose seems to be continuing"}]}]}],"team":"T03E45N2BFF"},{"client_msg_id":"146d36c2-84d5-47ac-9a50-bb53823cc338","type":"message","text":"im back\nwill wfh the rest of the day because runny nose","user":"U041YPW0T0A","ts":"1693832932.924299","blocks":[{"type":"rich_text","block_id":"fH7S9","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"im back\nwill wfh the rest of the day because runny nose"}]}]}],"team":"T03E45N2BFF","reactions":[{"name":"pill","users":["U05AHPTAWHX"],"count":1},{"name":"nauseated_face","users":["U041YPW0T0A"],"count":1},{"name":"face_vomiting","users":["U041YPW0T0A"],"count":1}]},{"client_msg_id":"b6f59911-e7fe-4a59-b545-650ee11d9580","type":"message","text":"I'll join CampX at lunch today","user":"U03EKMGNFHP","ts":"1693811507.694719","blocks":[{"type":"rich_text","block_id":"jsH","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"I'll join CampX at lunch today"}]}]}],"team":"T03E45N2BFF","reactions":[{"name":"+1","users":["U03E0DNBTJA"],"count":1}]},{"client_msg_id":"39F3022B-3FB4-422A-960D-7B598CCA226D","type":"message","text":"Last week I extended all access cards to February ","user":"U03E0DNBTJA","ts":"1693811272.622869","blocks":[{"type":"rich_text","block_id":"9qm","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"Last week I extended all access cards to February "}]}]}],"team":"T03E45N2BFF"},{"client_msg_id":"c1cf8735-472d-48bf-8076-714f68312633","type":"message","text":"Note to everyone - come to the front reception so they can help with access cards","user":"U03E73KK3D1","ts":"1693811143.962349","blocks":[{"type":"rich_text","block_id":"lfdm","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"Note to everyone - come to the front reception so they can help with access cards"}]}]}],"team":"T03E45N2BFF"},{"client_msg_id":"ea6979a6-30ef-466d-a82a-47ea54387b77","type":"message","text":"oook so i have to go to the other side","user":"U041YPW0T0A","ts":"1693811092.559739","blocks":[{"type":"rich_text","block_id":"=ltlg","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"oook so i have to go to the other side"}]}]}],"team":"T03E45N2BFF"},{"client_msg_id":"56a5f6e2-4c78-4553-9575-4241f1620bce","type":"message","text":"You need to be at front reception","user":"U03E73KK3D1","ts":"1693811068.670199","blocks":[{"type":"rich_text","block_id":"Okc","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"You need to be at front reception"}]}]}],"team":"T03E45N2BFF"},{"client_msg_id":"b74b0fbc-5907-4603-8b4a-8ecbb8e0aec3","type":"message","text":"They wont help","user":"U03E73KK3D1","ts":"1693811052.164659","blocks":[{"type":"rich_text","block_id":"SN+","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"They wont help"}]}]}],"team":"T03E45N2BFF"},{"client_msg_id":"a1ff33f5-c0d8-488c-83ab-792b6fbb3690","type":"message","text":"aiight ill try again\nat bar centro","user":"U041YPW0T0A","ts":"1693811010.173199","blocks":[{"type":"rich_text","block_id":"SROEu","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"aiight ill try again\nat bar centro"}]}]}],"team":"T03E45N2BFF"},{"client_msg_id":"80f13b21-e32f-4314-9837-179b007ae87c","type":"message","text":"Which reception are you at","user":"U03E73KK3D1","ts":"1693811000.363979","blocks":[{"type":"rich_text","block_id":"VXi","elements":[{"type":"rich_text_section","elements":[{"type":"text","text":"Which reception are you at"}]}]}],"team":"T03E45N2BFF"}],"has_more":true,"pin_count":0,"channel_actions_ts":null,"channel_actions_count":0,"response_metadata":{"next_cursor":"bmV4dF90czoxNjkzODEwOTc1MDU2MDQ5"}})";
	JSONw json;
	assert(json.parse(h));
}
#endif
