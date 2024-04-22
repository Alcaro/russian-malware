#include "serialize2.h"
#include "test.h"

#ifdef ARLIB_TEST
namespace {

// these structs are more fine grained than they need to be to test the current ones, but it helps when implementing a new backend
struct ser1 {
	int a;
	int b;
	
	SERIALIZE(a, b);
};

struct ser2 {
	ser1 c;
	ser1 d;
	
	SERIALIZE(c, d);
};

struct ser3 {
	int a;
	int b;
	int c;
	int d;
	int e;
	int f;
	int g;
	int h;
	
	SERIALIZE(a, b, c, d, e, f, g, h);
};

struct ser4 {
	array<int> data;
	SERIALIZE(data);
};

struct ser5 {
	array<ser1> data;
	SERIALIZE(data);
};

struct ser6 {
	ser4 par;
	SERIALIZE(par);
};

struct ser7 {
	//signed char a; // tostringhex(signed) doesn't exist
	//signed short b;
	//signed int c;
	//signed long d;
	//signed long long e;
	unsigned char f;
	unsigned short g;
	unsigned int h;
	unsigned long i;
	unsigned long long j;
	
	void serialize2(auto& s)
	{
		SER_ENTER(s)
		{
			SER_NAME(s, "f") s.item_hex(f);
			SER_NAME(s, "g") s.item_hex(g);
			SER_NAME(s, "h") s.item_hex(h);
			SER_NAME(s, "i") s.item_hex(i);
			SER_NAME(s, "j") s.item_hex(j);
		}
	}
};

struct ser9 {
	string data;
	SERIALIZE(data);
};

struct ser10 {
	set<string> data;
	SERIALIZE(data);
};

struct ser11 {
	map<string,string> data;
	SERIALIZE(data);
};

struct ser12 {
	uint8_t foo[4];
	bool t;
	bool f;
	
	void serialize(auto& s)
	{
		s.items("foo", foo, "t", t, "f", f);
	}
};

struct ser13 {
	uint8_t foo[4];
	array<uint8_t> bar;
	
	void serialize(auto& s)
	{
		SER_ENTER(s)
		{
			SER_NAME(s, "foo") s.item_hex(foo);
			SER_NAME(s, "bar") s.item_hex(bar);
		}
	}
};

class int_wrap {
public:
	uint32_t n = 0;
	explicit operator uint32_t() const { return n; }
	void operator=(uint32_t n) { this->n = n; }
	
	explicit operator string() const { assert_unreachable(); return ""; }
	void operator=(cstring str) { assert_unreachable(); }
	
	typedef uint32_t serialize_as;
};
struct ser14 {
	int_wrap foo;
	
	void serialize(auto& s)
	{
		s.items("foo", foo);
	}
};

struct ser15 {
	int a;
	bool b;
	bool c;
	int d;
	
	void serialize(auto& s)
	{
		SER_ENTER(s)
		{
			s.item("a", a);
			SER_IF(s, b) s.item("b", b);
			SER_IF(s, c) s.item("c", c);
			s.item("d", d);
		}
	}
};

struct ser16 {
	array<array<int>> a;
	array<array<int>> b;
	map<string,array<int>> c;
	
	void serialize(auto& s)
	{
		SER_ENTER(s)
		{
			s.item("a", a);
			SER_NAME(s, "b") { ser_compact2<1>(s).item(b); }
			SER_NAME(s, "c") { ser_compact2<1>(s).item(c); }
		}
	}
};

template<typename... Ts>
void assert_eq_unordered(cstring in, const char * tmpl, Ts... args)
{
	const char * parts[] = { args... };
	bitarray parts_used;
	parts_used.resize(sizeof...(args));
	const char * tmpl_in = tmpl;
	
	size_t off = 0;
	while (*tmpl)
	{
		char c = *tmpl++;
		if (c == '$')
		{
			for (size_t n : range(sizeof...(args)))
			{
				if (parts_used[n]) continue;
				if (in.substr(off, ~0).startswith(parts[n]))
				{
					off += strlen(parts[n]);
					parts_used[n] = true;
					goto next;
				}
			}
			goto fail;
		next: ;
		}
		else
		{
			if (in[off++] != c) goto fail;
		}
	}
	
	if (off != in.length()) goto fail;
	return;
	
fail:
	assert_eq(in, tmpl_in);
}
}

test("JSON serialization2", "json", "serialize")
{
	{
		ser1 item;
		item.a = 1;
		item.b = 2;
		
		assert_eq(jsonserialize2(item), "{\"a\":1,\"b\":2}");
	}
	
	{
		ser2 item;
		item.c.a = 1;
		item.c.b = 2;
		item.d.a = 3;
		item.d.b = 4;
		assert_eq(jsonserialize2(item), "{\"c\":{\"a\":1,\"b\":2},\"d\":{\"a\":3,\"b\":4}}");
	}
	
	{
		ser3 item;
		item.a = 1;
		item.b = 2;
		item.c = 3;
		item.d = 4;
		item.e = 5;
		item.f = 6;
		item.g = 7;
		item.h = 8;
		assert_eq(jsonserialize2(item), "{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8}");
	}
	
	{
		ser4 item;
		item.data.append(1);
		item.data.append(2);
		item.data.append(3);
		assert_eq(jsonserialize2(item), "{\"data\":[1,2,3]}");
	}
	
	{
		ser5 item;
		item.data.append();
		item.data.append();
		item.data[0].a = 1;
		item.data[0].b = 2;
		item.data[1].a = 3;
		item.data[1].b = 4;
		assert_eq(jsonserialize2(item), "{\"data\":[{\"a\":1,\"b\":2},{\"a\":3,\"b\":4}]}");
	}
	
	{
		ser6 item;
		item.par.data.append(1);
		item.par.data.append(2);
		item.par.data.append(3);
		assert_eq(jsonserialize2(item), "{\"par\":{\"data\":[1,2,3]}}");
	}
	
	{
		ser9 item;
		item.data = "test";
		assert_eq(jsonserialize2(item), "{\"data\":\"test\"}");
	}
	
	{
		ser10 item;
		item.data.add("foo");
		item.data.add("test");
		item.data.add("slightly longer string");
		assert_eq_unordered(jsonserialize2(item), "{\"data\":[$,$,$]}", "\"foo\"", "\"test\"", "\"slightly longer string\"");
	}
	
	{
		ser11 item;
		item.data.insert("foo", "bar");
		item.data.insert("a", "b");
		item.data.insert("longstringlongstring", "floating munchers");
		assert_eq_unordered(jsonserialize2(item),
		                    "{\"data\":{$,$,$}}", "\"foo\":\"bar\"", "\"a\":\"b\"", "\"longstringlongstring\":\"floating munchers\"");
	}
	
	{
		ser12 item;
		item.foo[0] = 12;
		item.foo[1] = 34;
		item.foo[2] = 56;
		item.foo[3] = 78;
		item.t = true;
		item.f = false;
		assert_eq(jsonserialize2(item), "{\"foo\":[12,34,56,78],\"t\":true,\"f\":false}");
		assert_eq(jsonserialize2<1>(item), "{\n \"foo\": [\n  12,\n  34,\n  56,\n  78],\n \"t\": true,\n \"f\": false}");
	}
	
	{
		ser13 item;
		item.foo[0] = 0x12;
		item.foo[1] = 0x34;
		item.foo[2] = 0x56;
		item.foo[3] = 0x78;
		item.bar.append(0x21);
		item.bar.append(0x43);
		item.bar.append(0x65);
		item.bar.append(0x87);
		assert_eq(jsonserialize2(item), "{\"foo\":\"12345678\",\"bar\":\"21436587\"}");
	}
	
	{
		ser14 item;
		item.foo.n = 123456789;
		assert_eq(jsonserialize2(item), "{\"foo\":123456789}");
	}
	
	{
		ser15 item = { 123, false, true, 456 };
		assert_eq(jsonserialize2(item), "{\"a\":123,\"c\":true,\"d\":456}");
	}
	
	{
		ser16 item;
		item.a.resize(1);
		item.a[0].append(1);
		item.a[0].append(2);
		item.b.resize(2);
		item.b[0].append(2);
		item.b[0].append(3);
		item.b[1].append(4);
		item.b[1].append(5);
		item.c.get_create("a").append(4);
		item.c.get_create("a").append(5);
		item.c.get_create("b").append(6);
		item.c.get_create("b").append(7);
		assert_eq_unordered(jsonserialize2<2>(item), "{\n  \"a\": [\n    [\n      1,\n      2]],"
		                                   "\n  \"b\": [\n    [2,3],\n    [4,5]],"
		                                   "\n  \"c\": {\n    $,\n    $}}",
		                                   "\"a\": [4,5]",
		                                   "\"b\": [6,7]");
	}
	
	{
		jsonserializer2 s;
		SER_ENTER(s)
		{
			SER_NAME(s, "num") s.item(42);
			SER_NAME(s, "str") s.item("test");
			SER_NAME(s, "inner") s.items("a", 1, "b", 2, "c", 3);
		}
		assert_eq(s.finish(), "{\"num\":42,\"str\":\"test\",\"inner\":{\"a\":1,\"b\":2,\"c\":3}}");
	}
	
	assert_eq(jsonserialize2((timestamp){ 123,0 }), "\"123\""); // the format is tested in time.cpp, just ensure the json parts work
}

test("JSON deserialization2", "json", "serialize")
{
	{
		int x = jsondeserialize2<int>("42");
		assert_eq(x, 42);
	}
	
	{
		ser1 item = jsondeserialize2<ser1>("{\"a\":1,\"b\":2}");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	
	{
		ser2 item = jsondeserialize2<ser2>("{ \"c\": {\"a\":1,\"b\":2}, \"d\": {\"a\":3,\"b\":4} }");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 2);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	//the system should not be order-sensitive
	{
		ser2 item = jsondeserialize2<ser2>("{ \"d\": {\"b\":4,\"a\":3}, \"c\": {\"a\":1,\"b\":2} }");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 2);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	//in case of dupes, last one should win; extraneous nodes should be cleanly ignored
	{
		ser1 item = jsondeserialize2<ser1>("{ \"a\":1, \"b\":2, \"x\":[1,2,3], \"a\":3, \"a\":4 }");
		assert_eq(item.a, 4);
		assert_eq(item.b, 2);
	}
	
	//wrong type, or weird partitioning
	{
		ser1 item = jsondeserialize2<ser1>("{\"a\":1,\"a\":[],\"b\":2}");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	{
		ser1 item = jsondeserialize2<ser1>("{\"a\":1,\"a\":{},\"b\":2}");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	{
		ser1 item = jsondeserialize2<ser1>("{\"a\":1,\"a\":[1,2,3],\"b\":2}");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	{
		ser1 item = jsondeserialize2<ser1>("{\"a\":1,\"a\":{\"x\":4,\"y\":5},\"b\":2}");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	{
		ser2 item = jsondeserialize2<ser2>("{ \"c\": {\"a\":1}, \"d\": {\"a\":3,\"b\":4}, \"c\": {} }");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 0);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	{
		ser3 item = jsondeserialize2<ser3>("{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8}");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
		assert_eq(item.c, 3);
		assert_eq(item.d, 4);
		assert_eq(item.e, 5);
		assert_eq(item.f, 6);
		assert_eq(item.g, 7);
		assert_eq(item.h, 8);
	}
	
	{
		ser4 item = jsondeserialize2<ser4>("{ \"data\": [ 1, 2, 3 ] }");
		assert_eq(tostring_dbg(item.data), "[1,2,3]");
	}
	
	{
		ser5 item = jsondeserialize2<ser5>("{ \"data\": [ { \"a\":1,\"b\":2 }, { \"a\":3,\"b\":4 } ] }");
		assert_eq(item.data.size(), 2);
		assert_eq(item.data[0].a, 1);
		assert_eq(item.data[0].b, 2);
		assert_eq(item.data[1].a, 3);
		assert_eq(item.data[1].b, 4);
	}
	
	{
		ser6 item = jsondeserialize2<ser6>("{ \"foo\": {\"bar\": {}}, \"par\": { \"data\": [ 1, 2, 3 ] } }");
		assert_eq(item.par.data.size(), 3);
		assert_eq(item.par.data[0], 1);
		assert_eq(item.par.data[1], 2);
		assert_eq(item.par.data[2], 3);
	}
	
	{
		ser9 item = jsondeserialize2<ser9>("{ \"data\": \"test\" }");
		assert_eq(item.data, "test");
	}
	
	{
		ser10 item = jsondeserialize2<ser10>("{ \"data\": [ \"foo\", \"test\", \"slightly longer string\" ] }");
		assert(item.data.contains("foo"));
		assert(item.data.contains("test"));
		assert(item.data.contains("slightly longer string"));
	}
	
	{
		ser11 item = jsondeserialize2<ser11>("{\"data\":{\"foo\":\"bar\",\"a\":\"b\",\"longstringlongstring\":\"floating munchers\"}}");
		assert_eq_unordered(tostring_dbg(item.data), "{$, $, $}", "foo => bar", "a => b", "longstringlongstring => floating munchers");
	}
	
	{
		ser12 item = jsondeserialize2<ser12>("{\"foo\":[12,34,56,78],\"t\":true,\"f\":false}");
		assert_eq(tostring_dbg(item.foo), "[12,34,56,78]");
		assert_eq(item.t, true);
		assert_eq(item.f, false);
	}
	
	{
		ser12 item = jsondeserialize2<ser12>("{\"foo\":[],\"t\":true,\"f\":false}");
		assert_eq(tostring_dbg(item.foo), "[0,0,0,0]"); // it's fixed size, it should have size 4
		assert_eq(item.t, true);
		assert_eq(item.f, false);
	}
	
	{
		ser12 item = jsondeserialize2<ser12>("{\"foo\":[1,2,3,4,5,6,7],\"t\":true,\"f\":false}");
		assert_eq(tostring_dbg(item.foo), "[1,2,3,4]"); // overflow should discard the excess
		assert_eq(item.t, true);
		assert_eq(item.f, false);
	}
	
	{
		ser12 item = jsondeserialize2<ser12>("{\"foo\":[1,2,3,4,5,6,7],\"t\":true,\"f\":false,\"foo\":[8,9]}");
		assert_eq(tostring_dbg(item.foo), "[8,9,3,4]"); // there's no reason to prefer this behavior, but I want to know if it changes
		assert_eq(item.t, true);
		assert_eq(item.f, false);
	}
	
	{
		ser13 item = jsondeserialize2<ser13>("{\"foo\":\"12345678\",\"bar\":\"21436587\"}");
		assert_eq(tostringhex(item.foo), "12345678");
		assert_eq(tostringhex(item.bar), "21436587");
	}
	
	{
		ser14 item = jsondeserialize2<ser14>("{\"foo\":123456789}");
		assert_eq(item.foo.n, 123456789);
	}
	
	{
		ser15 item = jsondeserialize2<ser15>("{\"a\":123,\"c\":true,\"d\":456}");
		assert_eq(item.a, 123);
		assert_eq(item.b, false);
		assert_eq(item.c, true);
		assert_eq(item.d, 456);
	}
	
	{
		ser14 item = jsondeserialize2<ser14>("{\"foo\":\"wrong type\"}");
		assert_eq(item.foo.n, 0);
	}
	
	{
		int num;
		string str;
		map<string,int> inner;
		
		jsondeserializer2 s("{\"num\":42,\"str\":\"test\",\"inner\":{\"a\":1,\"b\":2,\"c\":3}}");
		SER_ENTER(s)
		{
			s.item("num", num);
			s.item("str", str);
			SER_NAME(s, "inner")
			{
				SER_ENTER(s)
				{
					s.item(inner.get_create(s.get_name()));
				}
			}
		}
		
		assert_eq(num, 42);
		assert_eq(str, "test");
		assert_eq_unordered(tostring_dbg(inner), "{$, $, $}", "a => 1", "b => 2", "c => 3");
	}
	
	{
		int calls = 0;
		jsondeserializer2 s("{\"a\":1,\"b\":1,\"a\":2,\"a\":2,\"x\":[[]],\"a\":4}");
		SER_ENTER(s)
		{
			calls++;
			SER_NAME(s, "a") { int n; s.item(n); assert_eq(n, calls); }
			SER_NAME(s, "b") { int n; s.item(n); assert_eq(n, calls); }
		}
		assert_eq(calls, 4);
	}
	
	assert_eq(jsondeserialize2<timestamp>("\"123\""), (timestamp{123,0}));
	
	// these pass if they do not give infinite loops or valgrind errors, or otherwise explode
	{
		jsondeserialize2<ser1>("{ \"a\":1, \"b\":2, \"q\":*, \"a\":3, \"a\":4 }");
		jsondeserialize2<ser5>("{\"data\":[{\"a\":\"a\n[]\"}]}");
		jsondeserialize2<ser5>("{\"data\":[{\"a\":[]}]}");
		jsondeserialize2<map<string,int>>("{\"aaaaaaaaaaaaaaaa\":1,\"bbbbbbbbbbbbbbbb\":2}");
		jsondeserialize2<ser1>("{\"a\":}");
		jsondeserialize2<ser1>("{\"a\":1,\"b\":}");
		
		const char * jsons[] = {
			R"({"a":[]})",
			R"({"a":[1]})",
			R"({"a":{}})",
			R"({"a":{"b":1}})",
			R"({"a":1})",
			R"({"a":"a"})",
		};
		for (const char * json : jsons)
		{
			jsondeserializer2 a(json);
			SER_ENTER(a) { SER_NAME(a, "a") { SER_ENTER(a) {} } }
			jsondeserializer2 b(json);
			array<int> b2;
			SER_ENTER(b) { b.item("a", b2); }
			jsondeserializer2 c(json);
			int c2;
			SER_ENTER(c) { c.item("a", c2); }
			jsondeserializer2 d(json);
			string d2;
			SER_ENTER(d) { d.item("a", d2); }
		}
	}
}
#endif
