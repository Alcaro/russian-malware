#include "json.h"
#include "stringconv.h"
#include "simd.h"

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
	if (isspace(ret)) goto again;
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
		{
			m_want_key = true;
		}
		if (ch == ',')
		{
			m_need_value = true;
		}
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
		string val;
		
		//most strings don't contain escapes - special case them
		const uint8_t * fast_iter = m_data;
#ifdef __SSE2__
		while (fast_iter+16 <= m_data_end)
		{
			__m128i chs = _mm_loadu_si128((__m128i*)fast_iter);
			
			__m128i bad1 = _mm_cmplt_epi8(_mm_xor_si128(chs, _mm_set1_epi8(0x80)), _mm_set1_epi8((int8_t)(0x20^0x80)));
			__m128i bad2 = _mm_or_si128(_mm_cmpeq_epi8(chs, _mm_set1_epi8(0x22)), _mm_cmpeq_epi8(chs, _mm_set1_epi8(0x5C)));
			__m128i bad = _mm_or_si128(bad1, bad2);
			int mask = _mm_movemask_epi8(bad); // always in uint16 range, but gcc doesn't know that; keeping it as int optimizes better
			if (mask == 0)
			{
				fast_iter += 16;
				continue;
			}
			fast_iter += ctz32(mask);
			val = string(arrayview<uint8_t>(m_data, fast_iter-m_data));
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
			if (UNLIKELY(ch < 32 || ch == '\\' || ch == '"'))
			{
				val = string(arrayview<uint8_t>(m_data, fast_iter-m_data));
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
				case '"': val +=  '"'; break;
				case '\\':val += '\\'; break;
				case '/': val +=  '/'; break;
				case 'b': val += '\b'; break;
				case 'f': val += '\f'; break;
				case 'n': val += '\n'; break;
				case 'r': val += '\r'; break;
				case 't': val += '\t'; break;
				case 'u':
				{
					if (m_data+4 > m_data_end) return do_error();
					
					uint32_t codepoint;
					if (!fromstringhex(arrayview<uint8_t>(m_data, 4), codepoint)) return do_error();
					m_data += 4;
					
					// curse utf16 forever
					if (codepoint >= 0xD800 && codepoint <= 0xDBFF && m_data[0] == '\\' && m_data[1] == 'u')
					{
						uint16_t low_sur;
						if (!fromstringhex(arrayview<uint8_t>(m_data+2, 4), low_sur)) return do_error();
						
						if (low_sur >= 0xDC00 && low_sur <= 0xDFFF)
						{
							m_data += 6;
							codepoint = 0x10000 + ((codepoint-0xD800)<<10) + (low_sur-0xDC00);
						}
					}
					// else leave as is, string::codepoint will return fffd
					
					val += string::codepoint(codepoint);
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
			val += ch;
		}
	skip_escape_parse:
		if (is_key)
		{
			if (nextch() != ':') return do_error();
			return { map_key, std::move(val) };
		}
		else
		{
			if (!skipcomma()) return do_error();
			return { str, std::move(val) };
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
		
		double d;
		if (!fromstring(arrayview<uint8_t>(start, m_data-start), d)) return do_error();
		if (!skipcomma()) return do_error();
		return { num, d };
	}
	if (ch == 't' && *(m_data++)=='r' && *(m_data++)=='u' && *(m_data++)=='e')
	{
		if (!skipcomma()) return do_error();
		return { jtrue };
	}
	if (ch == 'f' && *(m_data++)=='a' && *(m_data++)=='l' && *(m_data++)=='s' && *(m_data++)=='e')
	{
		if (!skipcomma()) return do_error();
		return { jfalse };
	}
	if (ch == 'n' && *(m_data++)=='u' && *(m_data++)=='l' && *(m_data++)=='l')
	{
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
			
			__m128i bad1 = _mm_cmplt_epi8(_mm_xor_si128(chs, _mm_set1_epi8(0x80)), _mm_set1_epi8((int8_t)(0x20^0x80)));
			__m128i bad2 = _mm_or_si128(_mm_cmpeq_epi8(chs, _mm_set1_epi8(0x22)), _mm_cmpeq_epi8(chs, _mm_set1_epi8(0x5C)));
			__m128i bad3 = _mm_or_si128(bad2, _mm_cmpeq_epi8(chs, _mm_set1_epi8(0x7F)));
			__m128i bad = _mm_or_si128(bad1, bad3);
			int mask = _mm_movemask_epi8(bad);
			if (mask == 0)
			{
				it += 16-1; // -1 for the it++ above
				continue;
			}
			it += ctz32(mask);
		}
#endif
		
		uint8_t c = *it;
		// DEL is legal according to nst/JSONTestSuite, but let's avoid it anyways
		if (c < 32 || c == '"' || c == '\\' || c == 0x7F)
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

void JSON::construct(jsonparser& p, bool* ok, size_t maxdepth)
{
	if (maxdepth == 0)
	{
		*ok = false;
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
	
	if (ev.action == jsonparser::enter_list)
	{
		while (true)
		{
			jsonparser::event next = p.next();
			if (next.action == jsonparser::exit_list) break;
			if (next.action == jsonparser::error) *ok = false;
			JSON& child = chld_list.append();
			child.ev = next;
			child.construct(p, ok, maxdepth-1);
		}
	}
	if (ev.action == jsonparser::enter_map)
	{
		while (true)
		{
			jsonparser::event next = p.next();
			if (next.action == jsonparser::exit_map) break;
			if (next.action == jsonparser::map_key)
			{
				JSON& child = chld_map.insert(next.str);
				child.ev = p.next();
				child.construct(p, ok, maxdepth-1);
			}
			if (next.action == jsonparser::error) *ok = false;
		}
	}
	if (ev.action == jsonparser::error) *ok = false;
}

bool JSON::parse(cstring s)
{
	// it would not do to have some function changing the value of null for everybody else
	// this check is only needed here; all of JSON's other members are conceptually const, though most aren't marked as such
	if (this == &c_null)
		abort();
	
	chld_list.reset();
	chld_map.reset();
	
	jsonparser p(s);
	ev = p.next();
	bool ok = true;
	construct(p, &ok, 1000);
	jsonparser::event lastev = p.next();
	if (!ok || lastev.action != jsonparser::finish)
	{
		ev.action = jsonparser::error;
		return false;
	}
	return true;
}

template<bool sort>
void JSON::serialize(jsonwriter& w) const
{
	switch (ev.action)
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
		w.str(ev.str);
		break;
	case jsonparser::num:
		if (ev.num == (int)ev.num) w.num((int)ev.num);
		else w.num(ev.num);
		break;
	case jsonparser::enter_list:
		w.list_enter();
		for (const JSON& j : chld_list) j.serialize<sort>(w);
		w.list_exit();
		break;
	case jsonparser::enter_map:
		w.map_enter();
		if (sort)
		{
			array<const map<string,JSON>::node*> items;
			for (const map<string,JSON>::node& e : chld_map)
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
			for (auto& e : chld_map)
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
"[ 1, 2.5e+1, 3 ]"
;

static jsonparser::event test2e[]={
	{ e_enter_list },
		{ e_num, 1 },
		{ e_num, 25 },
		{ e_num, 3 },
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
"  \"f\": [ { \"g\": [ 7, 8 ], \"h\": [ 9, 1 ] }, { \"i\": [ 2, \"\xC3\xB8\" ], \"j\": [ {}, \"x\\nx\x7fx\" ] } ] }"
;

static jsonparser::event test4e[]={
	{ e_enter_map },
		{ e_map_key, "a" },
		{ e_enter_list },
			{ e_enter_map },
				{ e_map_key, "b" },
				{ e_enter_list }, { e_num, 1 }, { e_num, 2 }, { e_exit_list },
				{ e_map_key, "c" },
				{ e_enter_list }, { e_num, 3 }, { e_num, 4 }, { e_exit_list },
			{ e_exit_map },
			{ e_enter_map },
				{ e_map_key, "d" },
				{ e_enter_list }, { e_num, 5 }, { e_num, 6 }, { e_exit_list },
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
				{ e_enter_list }, { e_num, 7 }, { e_num, 8 }, { e_exit_list },
				{ e_map_key, "h" }, // don't use [9,0], { e_num, 0 } is ambiguous between double/char* ctors because c++ null is drunk
				{ e_enter_list }, { e_num, 9 }, { e_num, 1 }, { e_exit_list },
			{ e_exit_map },
			{ e_enter_map },
				{ e_map_key, "i" },
				{ e_enter_list }, { e_num, 2 }, { e_str, "\xC3\xB8" }, { e_exit_list },
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
		
//printf("e=%d [%s] [%lf]\n", expected->action, (const char*)expected->str.c_str(), (expected->action==e_num ? expected->num : -1));
//printf("a=%d [%s] [%lf]\n\n", actual.action,  (const char*)actual.str.c_str(),    (actual.action==e_num ? actual.num : -1));
		if (expected)
		{
			assert_eq(actual.action, expected->action);
			assert_eq(actual.str, expected->str);
			if (expected->action == e_num) assert_eq(actual.num, expected->num);
			
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
//printf("a=%d [%s] [%f]\n", ev.action, ev.str.bytes().ptr(), ev.num);
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
	
	//try to make it read out of bounds
	//input length 31
	testcall(testjson_error("\"force allocating the string \\u"));
	testcall(testjson_error("\"force allocating the string\\u1"));
	testcall(testjson_error("\"force allocating the strin\\u12"));
	testcall(testjson_error("\"force allocating the stri\\u123"));
	testcall(testjson_error("\"force allocating the str\\u1234"));
	//input length 32
	testcall(testjson_error("\"force allocating the string  \\u"));
	testcall(testjson_error("\"force allocating the string \\u1"));
	testcall(testjson_error("\"force allocating the string\\u12"));
	testcall(testjson_error("\"force allocating the strin\\u123"));
	testcall(testjson_error("\"force allocating the stri\\u1234"));
	//input length 15
	testcall(testjson_error("\"inline data \\u"));
	testcall(testjson_error("\"inline data\\u1"));
	testcall(testjson_error("\"inline dat\\u12"));
	testcall(testjson_error("\"inline da\\u123"));
	testcall(testjson_error("\"inline d\\u1234"));
	
	//found by https://github.com/nst/JSONTestSuite/ - thanks!
	testcall(testjson_error("[]\1"));
	testcall(testjson_error("[],"));
	testcall(testjson_error("123,"));
	testcall(testjson_error("[\"\t\"]"));
	testcall(testjson_error("[1,]"));
	testcall(testjson_error("{\"a\":0,}"));
	testcall(testjson_error("[1,,2]"));
	testcall(testjson_error("[-.123]"));
	testcall(testjson_error(arrayview<uint8_t>((uint8_t*)"123\0", 4)));
	testcall(testjson_error(arrayview<uint8_t>((uint8_t*)"[]\0", 3)));
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
