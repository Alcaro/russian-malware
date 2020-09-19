#include "serialize.h"
#include "test.h"

#ifdef ARLIB_TEST
namespace {

// these 13 structs are more fine grained than they need to be to test the current ones, but it helps when implementing a new backend
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
	
	template<typename T>
	void serialize(T& s)
	{
		s.items("f", ser_hex(f), "g", ser_hex(g), "h", ser_hex(h), "i", ser_hex(i), "j", ser_hex(j));
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
	
	template<typename T>
	void serialize(T& s)
	{
		s.items("foo", foo, "t", t, "f", f);
	}
};

struct ser13 {
	uint8_t foo[4];
	array<uint8_t> bar;
	
	template<typename T>
	void serialize(T& s)
	{
		s.items("foo", ser_hex(foo), "bar", ser_hex(bar));
	}
};

class int_wrap {
public:
	uint32_t n;
	explicit operator uint32_t() const { return n; }
	void operator=(uint32_t n) { this->n = n; }
	
	explicit operator string() const { assert_unreachable(); return ""; }
	void operator=(cstring str) { assert_unreachable(); }
	
	typedef uint32_t serialize_as;
};
struct ser14 {
	int_wrap foo;
	
	template<typename T>
	void serialize(T& s)
	{
		s.items("foo", foo);
	}
};

struct ser15 {
	int a;
	bool b;
	bool c;
	int d;
	
	template<typename T>
	void serialize(T& s)
	{
		s.items("a", a, "b", ser_include_if(b, b), "c", ser_include_if(c, c), "d", d);
	}
};

struct ser16 {
	array<array<int>> a;
	array<array<int>> b;
	map<string,array<int>> c;
	
	template<typename T>
	void serialize(T& s)
	{
		s.items("a", a,
		        "b", ser_compact(b, 1),
		        "c", ser_compact(c, 1));
	}
};
}

test("JSON serialization 2", "json", "serialize")
{
	{
		ser1 item;
		item.a = 1;
		item.b = 2;
		
		assert_eq(jsonserialize(item), "{\"a\":1,\"b\":2}");
	}
	
	{
		ser2 item;
		item.c.a = 1;
		item.c.b = 2;
		item.d.a = 3;
		item.d.b = 4;
		assert_eq(jsonserialize(item), "{\"c\":{\"a\":1,\"b\":2},\"d\":{\"a\":3,\"b\":4}}");
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
		assert_eq(jsonserialize(item), "{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8}");
	}
	
	{
		ser4 item;
		item.data.append(1);
		item.data.append(2);
		item.data.append(3);
		assert_eq(jsonserialize(item), "{\"data\":[1,2,3]}");
	}
	
	{
		ser5 item;
		item.data.append();
		item.data.append();
		item.data[0].a=1;
		item.data[0].b=2;
		item.data[1].a=3;
		item.data[1].b=4;
		assert_eq(jsonserialize(item), "{\"data\":[{\"a\":1,\"b\":2},{\"a\":3,\"b\":4}]}");
	}
	
	{
		ser6 item;
		item.par.data.append(1);
		item.par.data.append(2);
		item.par.data.append(3);
		assert_eq(jsonserialize(item), "{\"par\":{\"data\":[1,2,3]}}");
	}
	
	{
		ser7 item;
		item.f = 0xAA;
		item.g = 0xAAAA;
		item.h = 0xAAAAAAAA;
		item.i = 0xAAAAAAAA; // this could have another eight digits on linux, but no real point
		item.j = 0xAAAAAAAAAAAAA000; // rounded due to float precision
		// json doesn't support hex, so this just ends up with random-looking numbers
		assert_eq(jsonserialize(item), "{\"f\":170,\"g\":43690,\"h\":2863311530,\"i\":2863311530,\"j\":1.2297829382473032e+19}");
	}
	
	{
		ser9 item;
		item.data = "test";
		assert_eq(jsonserialize(item), "{\"data\":\"test\"}");
	}
	
	{
		ser10 item;
		item.data.add("foo");
		item.data.add("test");
		item.data.add("slightly longer string");
		//the set is unordered, this can give spurious failures
		if (sizeof(size_t) == 8)
			assert_eq(jsonserialize(item), "{\"data\":[\"slightly longer string\",\"test\",\"foo\"]}");
		else
			assert_eq(jsonserialize(item), "TODO: determine the current order");
	}
	
	{
		ser11 item;
		item.data.insert("foo", "bar");
		item.data.insert("a", "b");
		item.data.insert("longstringlongstring", "floating munchers");
		if (sizeof(size_t) == 8)
			assert_eq(jsonserialize(item), "{\"data\":{\"a\":\"b\",\"longstringlongstring\":\"floating munchers\",\"foo\":\"bar\"}}");
		else
			assert_eq(jsonserialize(item), "TODO: determine the current order");
	}
	
	{
		ser12 item;
		item.foo[0] = 12;
		item.foo[1] = 34;
		item.foo[2] = 56;
		item.foo[3] = 78;
		item.t = true;
		item.f = false;
		assert_eq(jsonserialize(item), "{\"foo\":[12,34,56,78],\"t\":true,\"f\":false}");
		assert_eq(jsonserialize<1>(item), "{\n \"foo\": [\n  12,\n  34,\n  56,\n  78],\n \"t\": true,\n \"f\": false}");
	}
	
	{
		ser13 item;
		item.foo[0] = 0x12;
		item.foo[1] = 0x34;
		item.foo[2] = 0x56;
		item.foo[3] = 0x78;
		item.bar.append(0x12);
		item.bar.append(0x34);
		item.bar.append(0x56);
		item.bar.append(0x78);
		assert_eq(jsonserialize(item), "{\"foo\":\"12345678\",\"bar\":\"12345678\"}");
	}
	
	{
		ser14 item;
		item.foo.n = 123456789;
		assert_eq(jsonserialize(item), "{\"foo\":123456789}");
	}
	
	{
		ser15 item = { 123, false, true, 456 };
		assert_eq(jsonserialize(item), "{\"a\":123,\"c\":true,\"d\":456}");
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
		item.c.get_create("a").append(3);
		item.c.get_create("a").append(4);
		item.c.get_create("b").append(5);
		item.c.get_create("b").append(6);
		assert_eq(jsonserialize<2>(item), "{\n  \"a\": [\n    [\n      1,\n      2]],"
		                                   "\n  \"b\": [\n    [2,3],\n    [4,5]],"
		                                   "\n  \"c\": {\n    \"b\": [5,6],\n    \"a\": [3,4]}}");
	}
	
	{
		string item = jsonserialize([&](jsonserializer& s) {
			s.items(
				"num", 42,
				"str", (cstring)"test",
				"inner", [&](jsonserializer& s){
					s.item("a", 1);
					s.item("b", 2);
					s.item("c", 3);
				});
			});
		assert_eq(item, "{\"num\":42,\"str\":\"test\",\"inner\":{\"a\":1,\"b\":2,\"c\":3}}");
	}
}

test("JSON deserialization 2", "json", "serialize")
{
	{
		int x = jsondeserialize<int>("42");
		assert_eq(x, 42);
	}
	
	{
		ser1 item = jsondeserialize<ser1>("{\"a\":1,\"b\":2}");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	
	{
		ser2 item = jsondeserialize<ser2>("{ \"c\": {\"a\":1,\"b\":2}, \"d\": {\"a\":3,\"b\":4} }");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 2);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	//the system should not be order-sensitive
	{
		ser2 item = jsondeserialize<ser2>("{ \"d\": {\"b\":4,\"a\":3}, \"c\": {\"a\":1,\"b\":2} }");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 2);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	//in case of dupes, last one should win; extraneous nodes should be cleanly ignored
	{
		ser1 item = jsondeserialize<ser1>("{ \"a\":1, \"b\":2, \"x\":[1,2,3], \"a\":3, \"a\":4 }");
		assert_eq(item.a, 4);
		assert_eq(item.b, 2);
	}
	
	{
		ser3 item = jsondeserialize<ser3>("{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8}");
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
		ser4 item = jsondeserialize<ser4>("{ \"data\": [ 1, 2, 3 ] }");
		assert_eq(tostring_dbg(item.data), "[1,2,3]");
	}
	
	{
		ser5 item = jsondeserialize<ser5>("{ \"data\": [ { \"a\":1,\"b\":2 }, { \"a\":3,\"b\":4 } ] }");
		assert_eq(item.data.size(), 2);
		assert_eq(item.data[0].a, 1);
		assert_eq(item.data[0].b, 2);
		assert_eq(item.data[1].a, 3);
		assert_eq(item.data[1].b, 4);
	}
	
	{
		//ensure finish_item() isn't screwing up
		ser6 item = jsondeserialize<ser6>("{ \"foo\": {\"bar\": {}}, \"par\": { \"data\": [ 1, 2, 3 ] } }");
		assert_eq(item.par.data.size(), 3);
		assert_eq(item.par.data[0], 1);
		assert_eq(item.par.data[1], 2);
		assert_eq(item.par.data[2], 3);
	}
	
	{
		ser7 item = jsondeserialize<ser7>("{\"f\":170,\"g\":43690,\"h\":2863311530,\"i\":2863311530,\"j\":1.2297829382473032e+19}");
		assert_eq(item.f, 0xAA);
		assert_eq(item.g, 0xAAAA);
		assert_eq(item.h, 0xAAAAAAAA);
		assert_eq(item.i, 0xAAAAAAAA);
		assert_eq(item.j, 0xAAAAAAAAAAAAA000);
	}
	
	{
		ser9 item = jsondeserialize<ser9>("{ \"data\": \"test\" }");
		assert_eq(item.data, "test");
	}
	
	{
		ser10 item = jsondeserialize<ser10>("{ \"data\": [ \"foo\", \"test\", \"slightly longer string\" ] }");
		assert(item.data.contains("foo"));
		assert(item.data.contains("test"));
		assert(item.data.contains("slightly longer string"));
	}
	
	{
		ser11 item = jsondeserialize<ser11>( "{\"data\":{\"foo\":\"bar\",\"a\":\"b\",\"longstringlongstring\":\"floating munchers\"}}");
		assert_eq(item.data.get_or("foo", "FAIL"), "bar");
		assert_eq(item.data.get_or("a", "FAIL"), "b");
		assert_eq(item.data.get_or("longstringlongstring", "FAIL"), "floating munchers");
	}
	
	{
		ser12 item = jsondeserialize<ser12>("{\"foo\":[12,34,56,78],\"t\":true,\"f\":false}");
		assert_eq(tostring_dbg(item.foo), "[12,34,56,78]");
		assert_eq(item.t, true);
		assert_eq(item.f, false);
	}
	
	{
		ser12 item = jsondeserialize<ser12>("{\"foo\":[],\"t\":true,\"f\":false}");
		assert_eq(tostring_dbg(item.foo), "[0,0,0,0]"); // it's fixed size, it should have size 4
		assert_eq(item.t, true);
		assert_eq(item.f, false);
	}
	
	{
		ser12 item = jsondeserialize<ser12>("{\"foo\":[1,2,3,4,5,6,7],\"t\":true,\"f\":false}");
		assert_eq(tostring_dbg(item.foo), "[1,2,3,4]"); // overflow should discard the excess
		assert_eq(item.t, true);
		assert_eq(item.f, false);
	}
	
	{
		ser12 item = jsondeserialize<ser12>("{\"foo\":[1,2,3,4,5,6,7],\"t\":true,\"f\":false,\"foo\":[8,9]}");
		assert_eq(tostring_dbg(item.foo), "[8,9,3,4]"); // there's no reason to prefer this behavior, but I want to know if it changes
		assert_eq(item.t, true);
		assert_eq(item.f, false);
	}
	
	{
		ser13 item = jsondeserialize<ser13>("{\"foo\":\"12345678\",\"bar\":\"12345678\"}");
		assert_eq(tostringhex(item.foo), "12345678");
		assert_eq(tostringhex(item.bar), "12345678");
	}
	
	{
		ser14 item = jsondeserialize<ser14>("{\"foo\":123456789}");
		assert_eq(item.foo.n, 123456789);
	}
	
	{
		ser15 item = jsondeserialize<ser15>("{\"a\":123,\"c\":true,\"d\":456}");
		assert_eq(item.a, 123);
		assert_eq(item.b, false);
		assert_eq(item.c, true);
		assert_eq(item.d, 456);
	}
	
	// these pass if they do not give infinite loops or valgrind errors, or otherwise explode
	{
		jsondeserialize<ser1>("{ \"a\":1, \"b\":2, \"q\":*, \"a\":3, \"a\":4 }");
		jsondeserialize<ser5>("{\"data\":[{\"a\":\"a\n[]\"}]}");
		jsondeserialize<ser5>("{\"data\":[{\"a\":[]}]}");
		jsondeserialize<map<string,int>>("{\"aaaaaaaaaaaaaaaa\":1,\"bbbbbbbbbbbbbbbb\":2}");
	}
	
	//wrong type, or weird partitioning
	{
		ser1 item = jsondeserialize<ser1>("{\"a\":1,\"a\":[],\"b\":2}");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	{
		ser1 item = jsondeserialize<ser1>("{\"a\":1,\"a\":{},\"b\":2}");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	{
		ser1 item = jsondeserialize<ser1>("{\"a\":1,\"a\":[1,2,3],\"b\":2}");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	{
		ser1 item = jsondeserialize<ser1>("{\"a\":1,\"a\":{\"x\":4,\"y\":5},\"b\":2}");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	{
		ser2 item = jsondeserialize<ser2>("{ \"c\": {\"a\":1}, \"d\": {\"a\":3,\"b\":4}, \"c\": {} }");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 0);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	{
		int num;
		string str;
		map<string,int> inner;
		jsondeserialize("{\"num\":42,\"str\":\"test\",\"inner\":{\"a\":1,\"b\":2,\"c\":3}}",
			[&](jsondeserializer& s) {
				s.items(
					"num", num,
					"str", str,
					"inner", [&](jsondeserializer& s){
						while (s.has_item())
							s.item(inner.get_create(s.name()));
					});
				});
		assert_eq(num, 42);
		assert_eq(str, "test");
		if (sizeof(size_t)==8)
			assert_eq(tostring_dbg(inner), "{b => 2, c => 3, a => 1}");
		else
			assert_eq(tostring_dbg(inner), "TODO: determine the current order");
	}
	
	{
		int calls = 0;
		jsondeserialize("{\"a\":\"1\",\"a\":\"2\",\"a\":[[]],\"a\":\"3\"}",
			[&](jsondeserializer& s) {
				s.items(
					"a", [&](cstring str) {
						assert_eq(str, tostring(++calls));
					});
				});
		assert_eq(calls, 3);
	}
}


test("BML serialization 2", "bml", "serialize")
{
	{
		ser1 item;
		item.a = 1;
		item.b = 2;
		
		assert_eq(bmlserialize(item), "a=1\nb=2");
	}
	
	{
		ser2 item;
		item.c.a = 1;
		item.c.b = 2;
		item.d.a = 3;
		item.d.b = 4;
		assert_eq(bmlserialize(item), "c a=1 b=2\nd a=3 b=4");
	}
	
	{
		ser4 item;
		item.data.append(1);
		item.data.append(2);
		item.data.append(3);
		assert_eq(bmlserialize(item), "data=1\ndata=2\ndata=3");
	}
	
	{
		ser5 item;
		item.data.append();
		item.data.append();
		item.data[0].a=1;
		item.data[0].b=2;
		item.data[1].a=3;
		item.data[1].b=4;
		assert_eq(bmlserialize(item), "data a=1 b=2\ndata a=3 b=4");
	}
	
	{
		ser6 item;
		item.par.data.append(1);
		item.par.data.append(2);
		item.par.data.append(3);
		assert_eq(bmlserialize(item), "par data=1 data=2 data=3");
	}
	
	{
		ser7 item;
		item.f = 0xAA;
		item.g = 0xAAAA;
		item.h = 0xAAAAAAAA;
		item.i = 0xAAAAAAAA;
		item.j = 0xAAAAAAAAAAAAAAAA;
		assert_eq(bmlserialize(item), "f=AA\ng=AAAA\nh=AAAAAAAA\ni=AAAAAAAA\nj=AAAAAAAAAAAAAAAA");
	}
	
	{
		ser9 item;
		item.data = "test";
		assert_eq(bmlserialize(item), "data=test");
	}
	
	{
		ser10 item;
		item.data.add("foo");
		item.data.add("test");
		item.data.add("string with spaces");
		//sets are unordered, this can give spurious failures
		if (sizeof(size_t) == 8)
			assert_eq(bmlserialize(item), "data=test\ndata=\"string with spaces\"\ndata=foo");
		else
			assert_eq(bmlserialize(item), "TODO: determine the current order");
	}
	
	{
		ser11 item;
		item.data.insert("foo", "bar");
		item.data.insert("test", "value with spaces");
		item.data.insert("key with spaces", "test");
		if (sizeof(size_t) == 8)
			assert_eq(bmlserialize(item), "data test=\"value with spaces\" -key-20with-20spaces=test foo=bar");
		else
			assert_eq(bmlserialize(item), "TODO: determine the current order");
	}
	
	{
		ser13 item;
		item.foo[0] = 0x12;
		item.foo[1] = 0x34;
		item.foo[2] = 0x56;
		item.foo[3] = 0x78;
		item.bar.append(0x12);
		item.bar.append(0x34);
		item.bar.append(0x56);
		item.bar.append(0x78);
		assert_eq(bmlserialize(item), "foo=12345678\nbar=12345678");
	}
	
	{
		ser14 item;
		item.foo.n = 123456789;
		assert_eq(bmlserialize(item), "foo=123456789");
	}
	
	//currently unimplemented
	//{
	//	ser15 item = bmldeserialize<ser15>("a=123\nc=true\nd=456");
	//	assert_eq(item.a, 123);
	//	assert_eq(item.b, false);
	//	assert_eq(item.c, true);
	//	assert_eq(item.d, 456);
	//}
	
	// serializing a lambda to bml is currently unimplemented
}

test("BML deserialization 2", "bml", "serialize")
{
	{
		ser1 item = bmldeserialize<ser1>("a=1\nb=2");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	
	{
		ser2 item = bmldeserialize<ser2>("c a=1 b=2\nd a=3 b=4");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 2);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	//the system should not be order-sensitive
	//in case of dupes, last one should win; extraneous nodes should be cleanly ignored
	{
		ser2 item = bmldeserialize<ser2>("d b=4 a=3 q\nc a=42 b=2 a=1\nq");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 2);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	{
		ser4 item = bmldeserialize<ser4>("data=1\ndata=2\ndata=3");
		assert_eq(tostring_dbg(item.data), "[1,2,3]");
	}
	
	{
		ser4 item = bmldeserialize<ser4>("data=1\ndata=2\ntrash=true\ndata=3");
		assert_eq(tostring_dbg(item.data), "[1,2,3]");
	}
	
	{
		ser5 item = bmldeserialize<ser5>("data a=1 b=2\ndata a=3 b=4");
		assert_eq(item.data.size(), 2);
		assert_eq(item.data[0].a, 1);
		assert_eq(item.data[0].b, 2);
		assert_eq(item.data[1].a, 3);
		assert_eq(item.data[1].b, 4);
	}
	
	{
		ser6 item = bmldeserialize<ser6>("par data=1 data=2 data=3");
		assert_eq(item.par.data.size(), 3);
		assert_eq(item.par.data[0], 1);
		assert_eq(item.par.data[1], 2);
		assert_eq(item.par.data[2], 3);
	}
	
	{
		ser7 item = bmldeserialize<ser7>("f=AA\ng=AAAA\nh=AAAAAAAA\ni=AAAAAAAA\nj=AAAAAAAAAAAAAAAA");
		assert_eq(item.f, 0xAA);
		assert_eq(item.g, 0xAAAA);
		assert_eq(item.h, 0xAAAAAAAA);
		assert_eq(item.i, 0xAAAAAAAA);
		assert_eq(item.j, 0xAAAAAAAAAAAAAAAA);
	}
	
	{
		ser9 item = bmldeserialize<ser9>("data=test");
		assert_eq(item.data, "test");
	}
	
	{
		ser10 item = bmldeserialize<ser10>("data=foo\ndata=\"string with spaces\"\ndata=test");
		assert(item.data.contains("foo"));
		assert(item.data.contains("test"));
		assert(item.data.contains("string with spaces"));
	}
	
	{
		ser11 item = bmldeserialize<ser11>("data"
		                                   " foo=bar"
		                                   " -key-20with-20spaces=test"
		                                   " test=\"string with spaces\"");
		assert_eq(item.data.get_or("foo", "FAIL"), "bar");
		assert_eq(item.data.get_or("test", "FAIL"), "string with spaces");
		assert_eq(item.data.get_or("key with spaces", "FAIL"), "test");
	}
	
	{
		ser13 item = bmldeserialize<ser13>("foo=12345678\nbar=12345678");
		assert_eq(item.foo[0], 0x12);
		assert_eq(item.foo[1], 0x34);
		assert_eq(item.foo[2], 0x56);
		assert_eq(item.foo[3], 0x78);
		assert_eq(item.bar.size(), 4);
		assert_eq(item.bar[0], 0x12);
		assert_eq(item.bar[1], 0x34);
		assert_eq(item.bar[2], 0x56);
		assert_eq(item.bar[3], 0x78);
	}
	
	{
		ser14 item = bmldeserialize<ser14>("foo=123456789");
		assert_eq(item.foo.n, 123456789);
	}
	
	//currently unimplemented
	//{
	//	ser15 item = bmldeserialize<ser15>("a=123\nc=true\nd=456");
	//	assert_eq(item.a, 123);
	//	assert_eq(item.b, false);
	//	assert_eq(item.c, true);
	//	assert_eq(item.d, 456);
	//}
	
	{
		int num = -1;
		array<string> strs;
		map<string,int> inner;
		bmldeserialize("num=42\nstrs=test\nstrs=aaa\ninner=test\n a=1\n b=2\n c=3\n",
			[&](bmldeserializer& s) {
				s.items(
					"num", num,
					"strs", strs,
					"inner", [&](bmldeserializer& s, cstring name){
						assert_eq(name, "test");
						while (s.has_item())
							s.item(inner.get_create(s.name()));
					});
				});
		assert_eq(num, 42);
		assert_eq(tostring_dbg(strs), "[test,aaa]");
		if (sizeof(size_t)==8)
			assert_eq(tostring_dbg(inner), "{b => 2, c => 3, a => 1}");
		else
			assert_eq(tostring_dbg(inner), "TODO: determine the current order");
	}
}
#endif
