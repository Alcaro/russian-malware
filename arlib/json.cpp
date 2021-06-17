#include "json.h"
#include "stringconv.h"
#include "simd.h"
#include "endian.h"

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
	while (!(in&1)) // could use some bithacks, but compilers not identifying as msvc nor gcc are just about extinct
	{
		ret++;
		in >>= 1;
	}
	return ret;
#endif
}

uint8_t jsonparser::nextch()
{
again: ;
	uint8_t ret = *(m_data++);
	if (ret >= 33) return ret;
	if (LIKELY(isspace(ret))) goto again;
	if (m_data > m_data_end)
	{
		m_data--;
		return '\0';
	}
	return '\1'; // all pre-space non-whitespace is invalid, just pick one at random
}

//returns false if the input is invalid
bool jsonparser::skipcomma(size_t depth)
{
	uint8_t ch = nextch();
	if (ch == ',' || ch == '\0')
	{
		if ((m_nesting.size() >= depth) == (ch == '\0')) return false;
		if (m_nesting.get_or(m_nesting.size()-depth, false) == true)
			m_want_key = true;
		if (ch == ',')
			m_need_value = true;
		return true;
	}
	if (ch == ']' || ch == '}')
	{
		m_data--;
		return true;
	}
	return false;
}

jsonparser::event jsonparser::next()
{
	uint8_t ch = nextch();
	if (m_want_key)
	{
		m_want_key = false;
		if (ch == '"') goto parse_key;
		else if (ch == '}') goto close_brace;
		else return do_error();
	}
	
	if (ch == '\0')
	{
		if (m_need_value)
		{
			m_need_value = false;
			return do_error();
		}
		if (m_nesting)
		{
			if (!m_unexpected_end)
			{
				m_unexpected_end = true;
				return do_error();
			}
			bool map = (m_nesting[m_nesting.size()-1] == true);
			m_nesting.resize(m_nesting.size()-1);
			return { map ? exit_map : exit_list };
		}
		return { finish };
	}
	if (m_need_value && (ch == ']' || ch == '}'))
	{
		m_need_value = false;
		m_data--;
		return do_error();
	}
	m_need_value = false;
	
	if (ch == '"')
	{
		bool is_key;
		is_key = false;
		if (false)
		{
		parse_key:
			is_key = true;
		}
		uint8_t * val_out_start = m_data;
		uint8_t * val_out = m_data;
		
		//most strings don't contain escapes, special case them
		uint8_t * fast_iter = m_data;
#ifdef __SSE2__
		while (fast_iter+16 <= m_data_end)
		{
			__m128i chs = _mm_loadu_si128((__m128i*)fast_iter);
			
			// xor 128 because signed compare; xor 2 to fold " to beside the control chars and not check it separately
			__m128i bad1 = _mm_cmplt_epi8(_mm_xor_si128(chs, _mm_set1_epi8(0x82)), _mm_set1_epi8((int8_t)(0x21^0x80)));
			__m128i bad2 = _mm_cmpeq_epi8(chs, _mm_set1_epi8(0x5C)); // can't find any way to optimize out the \ check
			__m128i bad = _mm_or_si128(bad1, bad2);
			int mask = _mm_movemask_epi8(bad); // always in uint16 range, but gcc doesn't know that; keeping it as int optimizes better
			if (mask == 0)
			{
				fast_iter += 16;
				continue;
			}
			fast_iter += ctz32(mask);
			val_out = fast_iter;
			m_data = fast_iter;
			if (LIKELY(*m_data == '"'))
			{
				m_data++;
				goto skip_escape_parse;
			}
			break;
		}
		//just take the slow path for the last 16 bytes of the document
#else
		while (true)
		{
			uint8_t ch = *fast_iter;
			if (UNLIKELY((ch^2) <= 32 || ch == '\\'))
			{
				val_out = fast_iter;
				m_data = fast_iter;
				if (LIKELY(ch == '"'))
				{
					m_data++;
					goto skip_escape_parse;
				}
				break;
			}
			fast_iter++;
		}
#endif
		
		while (true)
		{
			uint8_t ch = *(m_data++);
			if (ch == '\\')
			{
				uint8_t esc = *(m_data++);
				switch (esc)
				{
				case '"': *val_out++ =  '"'; break;
				case '\\':*val_out++ = '\\'; break;
				case '/': *val_out++ =  '/'; break;
				case 'b': *val_out++ = '\b'; break;
				case 'f': *val_out++ = '\f'; break;
				case 'n': *val_out++ = '\n'; break;
				case 'r': *val_out++ = '\r'; break;
				case 't': *val_out++ = '\t'; break;
				case 'u':
				{
					if (UNLIKELY(m_data+4 > m_data_end)) return do_error();
					
					uint32_t codepoint;
					if (UNLIKELY(!fromstringhex(arrayview<uint8_t>(m_data, 4), codepoint))) return do_error();
					m_data += 4;
					
					// curse utf16 forever
					if (codepoint >= 0xD800 && codepoint <= 0xDBFF && m_data[0] == '\\' && m_data[1] == 'u')
					{
						uint16_t low_sur;
						if (UNLIKELY(!fromstringhex(arrayview<uint8_t>(m_data+2, 4), low_sur))) return do_error();
						
						if (low_sur >= 0xDC00 && low_sur <= 0xDFFF)
						{
							m_data += 6;
							codepoint = 0x10000 + ((codepoint-0xD800)<<10) + (low_sur-0xDC00);
						}
					}
					// else leave as is, string::codepoint will return fffd for unpaired surrogates
					
					val_out += string::codepoint(val_out, codepoint);
					break;
				}
				default:
					m_data--;
					return do_error();
				}
				continue;
			}
			if (ch == '"') break;
			if (ch < 32)
			{
				m_data--;
				return do_error();
			}
			*val_out++ = ch;
		}
	skip_escape_parse:
		*val_out = '\0';
		cstring val(arrayview<uint8_t>(val_out_start, val_out-val_out_start), true);
		if (is_key)
		{
			if (nextch() != ':') return do_error();
			return { map_key, val };
		}
		else
		{
			if (!skipcomma()) return do_error();
			return { str, val };
		}
	}
	if (ch == '[')
	{
		m_nesting.append(false);
		return { enter_list };
	}
	if (ch == ']')
	{
		if (!m_nesting || m_nesting[m_nesting.size()-1] != false) return do_error();
		if (!skipcomma(2)) return do_error();
		m_nesting.resize(m_nesting.size()-1);
		return { exit_list };
	}
	if (ch == '{')
	{
		m_nesting.append(true);
		m_want_key = true;
		return { enter_map };
	}
	if (ch == '}')
	{
	close_brace:
		m_want_key = false;
		if (!m_nesting || m_nesting[m_nesting.size()-1] != true) return do_error();
		if (!skipcomma(2)) return do_error();
		m_nesting.resize(m_nesting.size()-1);
		return { exit_map };
	}
	if (ch == '-' || isdigit(ch))
	{
		m_data--;
		const uint8_t * start = m_data;
		if (*m_data == '-') m_data++;
		if (*m_data == '0') m_data++;
		else
		{
			if (!isdigit(*m_data)) return do_error();
			while (isdigit(*m_data)) m_data++;
		}
		if (*m_data == '.')
		{
			m_data++;
			if (!isdigit(*m_data)) return do_error();
			while (isdigit(*m_data)) m_data++;
		}
		if (*m_data == 'e' || *m_data == 'E')
		{
			m_data++;
			if (*m_data == '+' || *m_data == '-') m_data++;
			if (!isdigit(*m_data)) return do_error();
			while (isdigit(*m_data)) m_data++;
		}
		
		size_t len = m_data-start;
		if (!skipcomma()) return do_error();
		return { num, arrayview<uint8_t>(start, len) };
	}
	if (ch == 't' && m_data+3 <= m_data_end && readu_le32(m_data-1) == 0x65757274) // true
	{
		m_data += 3;
		if (!skipcomma()) return do_error();
		return { jtrue };
	}
	if (ch == 'f' && m_data+4 <= m_data_end && readu_le32(m_data) == 0x65736c61) // alse
	{
		m_data += 4;
		if (!skipcomma()) return do_error();
		return { jfalse };
	}
	if (ch == 'n' && m_data+3 <= m_data_end && readu_le32(m_data-1) == 0x6c6c756e) // null
	{
		m_data += 3;
		if (!skipcomma()) return do_error();
		return { jnull };
	}
	
	return do_error();
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



JSON JSON::c_null;

void JSON::construct(jsonparser& p, jsonparser::event& ev, bool* ok, size_t maxdepth)
{
	m_action = ev.action;
	if(0);
	else if (ev.action == jsonparser::str) m_str = ev.str;
	else if (ev.action == jsonparser::num) fromstring(ev.str, m_num);
	else if (ev.action == jsonparser::error) *ok = false;
	else if (maxdepth == 0)
	{
		*ok = false;
		m_action = jsonparser::error;
		if (ev.action == jsonparser::enter_list || ev.action == jsonparser::enter_map)
		{
			// it would be faster to reset the jsonparser somehow,
			// but performance on hostile inputs isn't really a priority
			size_t xdepth = 1;
			while (xdepth)
			{
				jsonparser::event next = p.next();
				if (next.action == jsonparser::enter_list) xdepth++;
				if (next.action == jsonparser::enter_map) xdepth++;
				if (next.action == jsonparser::exit_list) xdepth--;
				if (next.action == jsonparser::exit_map) xdepth--;
			}
		}
		return;
	}
	else if (ev.action == jsonparser::enter_list)
	{
		while (true)
		{
			jsonparser::event next = p.next();
			if (next.action == jsonparser::exit_list) break;
			if (next.action == jsonparser::error) *ok = false;
			m_chld_list.append().construct(p, next, ok, maxdepth-1);
		}
	}
	else if (ev.action == jsonparser::enter_map)
	{
		while (true)
		{
			jsonparser::event next = p.next();
			if (next.action == jsonparser::exit_map) break;
			if (next.action == jsonparser::map_key)
			{
				jsonparser::event ev = p.next();
				m_chld_map.insert(next.str).construct(p, ev, ok, maxdepth-1);
			}
			if (next.action == jsonparser::error) *ok = false;
		}
	}
}

bool JSON::parse(string s)
{
	// it would not do to have some function changing the value of null for everybody else
	// this check is only needed here; all of JSON's other members are conceptually const, though most aren't marked as such
	if (this == &c_null)
		abort();
	
	m_chld_list.reset();
	m_chld_map.reset();
	
	jsonparser p(std::move(s));
	bool ok = true;
	jsonparser::event ev = p.next();
	construct(p, ev, &ok, 1000);
	
	ev = p.next();
	if (!ok || ev.action != jsonparser::finish)
	{
		m_action = jsonparser::error;
		return false;
	}
	return true;
}

template<bool sort>
void JSON::serialize(jsonwriter& w) const
{
	switch (m_action)
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
		w.str(m_str);
		break;
	case jsonparser::num:
		if (m_num == (int)m_num) w.num((int)m_num);
		else w.num(m_num);
		break;
	case jsonparser::enter_list:
		w.list_enter();
		for (const JSON& j : m_chld_list) j.serialize<sort>(w);
		w.list_exit();
		break;
	case jsonparser::enter_map:
		w.map_enter();
		if (sort)
		{
			array<const map<string,JSON>::node*> items;
			for (const map<string,JSON>::node& e : m_chld_map)
			{
				items.append(&e);
			}
			items.ssort([](const map<string,JSON>::node* a, const map<string,JSON>::node* b) { return string::less(a->key, b->key); });
			for (const map<string,JSON>::node* e : items)
			{
				w.map_key(e->key);
				e->value.serialize<sort>(w);
			}
		}
		else
		{
			for (auto& e : m_chld_map)
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
"  \"f\": [ { \"g\": [ 7, 8 ], \"h\": [ 9, 0 ] }, { \"i\": [ 1, \"\xC3\xB8\" ], \"j\": [ {}, \"x\\nx\x7fx\" ] } ] }"
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
		{ e_exit_list },
	{ e_exit_map },
	{ e_finish }
};

static const char * test5 =
"{ \"foo\": \"\xC2\x80\\u0080\\ud83d\\udca9\" }\n"
;

static jsonparser::event test5e[]={
	{ e_enter_map },
		{ e_map_key, "foo" },
		{ e_str, "\xC2\x80\xC2\x80\xF0\x9F\x92\xA9" },
	{ e_exit_map },
	{ e_finish }
};

static void testjson(cstring json, jsonparser::event* expected)
{
	jsonparser parser(json);
	while (true)
	{
		jsonparser::event actual = parser.next();
		
//printf("e=%d [%s]\n", expected->action, (const char*)expected->str.c_str());
//printf("a=%d [%s]\n\n", actual.action,  (const char*)actual.str.c_str());
		if (expected)
		{
			assert_eq(actual.action, expected->action);
			assert_eq(actual.str, expected->str);
			
			if (expected->action == e_finish) return;
		}
		if (actual.action == e_finish) return;
		
		expected++;
	}
}

static void testjson_error(cstring json, bool actually_error = true)
{
	jsonparser parser(json);
	int depth = 0;
	bool error = false;
	int events = 0;
	while (true)
	{
		jsonparser::event ev = parser.next();
//if (events==999)
//printf("a=%d [%s]\n", ev.action, (const char*)ev.str.c_str());
		if (ev.action == e_error) error = true; // any error is fine
		if (ev.action == e_enter_list || ev.action == e_enter_map) depth++;
		if (ev.action == e_exit_list  || ev.action == e_exit_map)  depth--;
		if (ev.action == e_finish) break;
		assert(depth >= 0);
		
		events++;
		assert_lt(events, 1000); // fail on infinite error loops
	}
	assert_eq(error, actually_error);
	assert_eq(depth, 0);
}

test("JSON parser", "string", "json")
{
	testcall(testjson(test1, test1e));
	testcall(testjson(test2, test2e));
	testcall(testjson(test3, test3e));
	testcall(testjson(test4, test4e));
	testcall(testjson(test5, test5e));
	
	testcall(testjson_error(""));
	testcall(testjson_error("           "));
	testcall(testjson_error("{"));
	testcall(testjson_error("{\"a\""));
	testcall(testjson_error("{\"a\":"));
	testcall(testjson_error("{\"a\":1"));
	testcall(testjson_error("{\"a\":1,"));
	testcall(testjson_error("["));
	testcall(testjson_error("[1"));
	testcall(testjson_error("[1,"));
	testcall(testjson_error("\""));
	testcall(testjson_error("01"));
	testcall(testjson_error("1."));
	testcall(testjson_error("+1"));
	testcall(testjson_error("1e"));
	testcall(testjson_error("1e+"));
	testcall(testjson_error("1e-"));
	testcall(testjson_error("z"));
	testcall(testjson_error("{ \"a\":1, \"b\":2, \"q\":*, \"a\":3, \"a\":4 }"));
	testcall(testjson_error("\""));
	testcall(testjson_error("\"\\"));
	testcall(testjson_error("\"\\u"));
	testcall(testjson_error("\"\\u1"));
	testcall(testjson_error("\"\\u12"));
	testcall(testjson_error("\"\\u123"));
	testcall(testjson_error("\"\\u1234"));
	testcall(testjson_error("[\f]"));
	testcall(testjson_error("[\v]"));
	
	//try to make it read out of bounds
	//input length 31
	testcall(testjson_error("\"this is a somewhat longer str\\u"));
	testcall(testjson_error("\"this is a somewhat longer st\\u1"));
	testcall(testjson_error("\"this is a somewhat longer s\\u12"));
	testcall(testjson_error("\"this is a somewhat longer \\u123"));
	testcall(testjson_error("\"this is a somewhat longer\\u1234"));
	//input length 32
	testcall(testjson_error("\"this is a somewhat longer stri\\u"));
	testcall(testjson_error("\"this is a somewhat longer str\\u1"));
	testcall(testjson_error("\"this is a somewhat longer st\\u12"));
	testcall(testjson_error("\"this is a somewhat longer s\\u123"));
	testcall(testjson_error("\"this is a somewhat longer \\u1234"));
	//input length 15
	testcall(testjson_error("\"short string \\u"));
	testcall(testjson_error("\"short string\\u1"));
	testcall(testjson_error("\"short strin\\u12"));
	testcall(testjson_error("\"short stri\\u123"));
	testcall(testjson_error("\"short str\\u1234"));
	testcall(testjson_error("                    f"));
	testcall(testjson_error("                    fa"));
	testcall(testjson_error("                    fal"));
	testcall(testjson_error("                    fals"));
	testcall(testjson_error("                    t"));
	testcall(testjson_error("                    tr"));
	testcall(testjson_error("                    tru"));
	testcall(testjson_error("                    n"));
	testcall(testjson_error("                    nu"));
	testcall(testjson_error("                    nul"));
	
	//found by https://github.com/nst/JSONTestSuite/ - thanks!
	testcall(testjson_error("[]\1"));
	testcall(testjson_error("[],"));
	testcall(testjson_error("123,"));
	testcall(testjson_error("[\"\t\"]"));
	testcall(testjson_error("[1,]"));
	testcall(testjson_error("{\"a\":0,}"));
	testcall(testjson_error("[1,,2]"));
	testcall(testjson_error("[-.123]"));
	
	testcall(testjson_error("123"+string::nul()));
	testcall(testjson_error("[]"+string::nul()));
}

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
		json["a"].num() = 1;
		json["b"].num() = 2;
		json["c"].num() = 3;
		json["d"].num() = 4;
		json["e"].num() = 5;
		json["f"].num() = 6;
		json["g"].num() = 7;
		json["h"].num() = 8;
		json["i"].num() = 9;
		assert_eq(json.serialize_sorted(), R"({"a":1,"b":2,"c":3,"d":4,"e":5,"f":6,"g":7,"h":8,"i":9})");
		assert_ne(json.serialize(),        R"({"a":1,"b":2,"c":3,"d":4,"e":5,"f":6,"g":7,"h":8,"i":9})");
	}
	
	{
		JSONw json;
		json["\x01\x02\x03\n\\n☃\""].str() = "\x01\x02\x03\n\\n☃\"";
		assert_eq(json.serialize(), R"({"\u0001\u0002\u0003\n\\n☃\"":"\u0001\u0002\u0003\n\\n☃\""})");
	}
	
	{
		JSONw json;
		json[0].str() = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
		json[1].str() = "aaaaaaaa\"aaaaaaaaaaaaaaaaaaaaaaa";
		json[2].str() = "aaaaaaaa\x1f""aaaaaaaaaaaaaaaaaaaaaaa";
		json[3].str() = "aaaaaaaa aaaaaaaaaaaaaaaaaaaaaaa";
		json[4].str() = "aaaaaaaa\\aaaaaaaaaaaaaaaaaaaaaaa";
		assert_eq(json.serialize(), R"(["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaa\"aaaaaaaaaaaaaaaaaaaaaaa",)"
		           R"("aaaaaaaa\u001Faaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaa aaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaa\\aaaaaaaaaaaaaaaaaaaaaaa"])");
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
}
#endif
