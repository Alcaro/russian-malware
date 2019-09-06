#include "serialize.h"
#include "set.h"
#include "test.h"

#ifdef ARLIB_TEST
struct ser1 {
	int a;
	int b;
	
//template<typename T> void serialize(T& s) {
//if(!s.serializing)puts("a:"+s.next());
//s.item("a", a);
//if(!s.serializing)puts("b:"+s.next());
//s.item("b", b);
//if(!s.serializing)puts("c:"+s.next());
//}
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
	ser3 mem;
	int count = 0;
	template<typename T> void serialize(T& s) { mem.serialize(s); count++; }
};

struct ser5 {
	array<int> data;
	SERIALIZE(data);
};

struct ser6 {
	array<ser1> data;
	SERIALIZE(data);
};

struct ser7 {
	ser5 par;
	SERIALIZE(par);
};

struct ser8 {
	//signed char a;
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
		//s.hex("a", a);
		//s.hex("b", b);
		//s.hex("c", c);
		//s.hex("d", d);
		//s.hex("e", e);
		s.hex("f", f);
		s.hex("g", g);
		s.hex("h", h);
		s.hex("i", i);
		s.hex("j", j);
	}
};

struct ser9 {
	byte foo[4];
	array<byte> bar;
	
	explicit operator string() const { return "FAIL"; } // should use serialize_as if it exists
	void operator=(cstring str) { foo[0] = 0xDE; foo[1] = 0xAD; foo[2] = 0xBE; foo[3] = 0xEF; }
	
	template<typename T>
	void serialize(T& s)
	{
		s.hex("foo", arrayvieww<byte>(foo));
		s.hex("bar", bar);
	}
};

struct ser10 {
	string data;
	SERIALIZE(data);
};

struct ser11 {
	set<string> data;
	SERIALIZE(data);
};

struct ser12 {
	map<string,string> data;
	SERIALIZE(data);
};

struct ser13 { // can't be bml serialized, json only; 'foo=1 foo=2 foo=3 foo=4' can't keep track of how much is written
	byte foo[4];
	bool t;
	bool f;
	
	template<typename T>
	void serialize(T& s)
	{
		s.item("foo", foo);
		s.item("t", t);
		s.item("f", f);
	}
};

class int_wrap {
public:
	uint32_t n;
	explicit operator uint32_t() const { return n; }
	void operator=(uint32_t n) { this->n = n; }
	
	explicit operator string() const { return "FAIL"; } // should use serialize_as if it exists
	void operator=(cstring str) { n = 0xDEADBEEF; }
	
	typedef uint32_t serialize_as;
};
class almost_int {
public:
	uint64_t n;
	explicit operator string() const { return tostring(n); }
	//almost_int(cstring str) { fromstring(str, n); }
	void operator=(cstring str) { fromstring(str, n); }
};
struct ser14 {
	int_wrap foo;
	almost_int bar;
	
	template<typename T>
	void serialize(T& s)
	{
		s.item("foo", foo);
		s.item("bar", bar);
	}
};

test("BML serialization", "bml", "serialize")
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
		ser5 item;
		item.data.append(1);
		item.data.append(2);
		item.data.append(3);
		assert_eq(bmlserialize(item), "data=1\ndata=2\ndata=3");
	}
	
	{
		ser6 item;
		item.data.append();
		item.data.append();
		item.data[0].a=1;
		item.data[0].b=2;
		item.data[1].a=3;
		item.data[1].b=4;
		assert_eq(bmlserialize(item), "data a=1 b=2\ndata a=3 b=4");
	}
	
	{
		ser7 item;
		item.par.data.append(1);
		item.par.data.append(2);
		item.par.data.append(3);
		assert_eq(bmlserialize(item), "par data=1 data=2 data=3");
	}
	
	{
		ser8 item;
		item.f = 0xAA;
		item.g = 0xAAAA;
		item.h = 0xAAAAAAAA;
		item.i = 0xAAAAAAAA; // this could have another eight As on linux, but why would I even do that
		item.j = 0xAAAAAAAAAAAAAAAA;
		assert_eq(bmlserialize(item), "f=AA\ng=AAAA\nh=AAAAAAAA\ni=AAAAAAAA\nj=AAAAAAAAAAAAAAAA");
	}
	
	{
		ser9 item;
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
		ser10 item;
		item.data = "test";
		assert_eq(bmlserialize(item), "data=test");
	}
	
	{
		ser11 item;
		item.data.add("foo");
		item.data.add("test");
		item.data.add("C:/Users/Administrator/My Documents/!TOP SECRET!.docx");
		//the set is unordered, this can give spurious failures
		if (sizeof(size_t) == 8)
			assert_eq(bmlserialize(item), "data=test\ndata=\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\"\ndata=foo");
		else
			assert_eq(bmlserialize(item), "data=\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\"\ndata=foo\ndata=test");
	}
	
	{
		ser12 item;
		item.data.insert("foo", "bar");
		item.data.insert("test", "C:/Users/Administrator/My Documents/!TOP SECRET!.docx");
		item.data.insert("C:/Users/Administrator/My Documents/!TOP SECRET!.docx", "test");
		if (sizeof(size_t) == 8)
			assert_eq(bmlserialize(item), "data"
			                              " test=\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\""
			                              " -C-3A-2FUsers-2FAdministrator-2FMy-20Documents-2F-21TOP-20SECRET-21.docx=test"
			                              " foo=bar");
		else
			assert_eq(bmlserialize(item), "data"
			                              " -C-3A-2FUsers-2FAdministrator-2FMy-20Documents-2F-21TOP-20SECRET-21.docx=test"
			                              " foo=bar"
			                              " test=\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\"");
	}
	
	{
		ser14 item;
		item.foo.n = 123456789;
		item.bar.n = 1234567890987654321;
		assert_eq(bmlserialize(item), "foo=123456789\nbar=1234567890987654321");
	}
}

test("BML deserialization", "bml", "serialize")
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
	
	//the system is allowed to loop, but only if there's bogus or extraneous nodes
	//we want O(n) runtime for a clean document, so ensure no looping
	//this includes missing and duplicate elements, both of which are possible for serialized arrays
	{
		ser4 item = bmldeserialize<ser4>("a=1\nb=2\nd=4\ne=5\ne=5\nf=6");
		assert_eq(item.count, 1);
	}
	
	{
		ser5 item = bmldeserialize<ser5>("data=1\ndata=2\ndata=3");
		assert_eq(item.data.size(), 3);
		assert_eq(item.data[0], 1);
		assert_eq(item.data[1], 2);
		assert_eq(item.data[2], 3);
	}
	
	{
		ser6 item = bmldeserialize<ser6>("data a=1 b=2\ndata a=3 b=4");
		assert_eq(item.data.size(), 2);
		assert_eq(item.data[0].a, 1);
		assert_eq(item.data[0].b, 2);
		assert_eq(item.data[1].a, 3);
		assert_eq(item.data[1].b, 4);
	}
	
	{
		ser7 item = bmldeserialize<ser7>("par data=1 data=2 data=3");
		assert_eq(item.par.data.size(), 3);
		assert_eq(item.par.data[0], 1);
		assert_eq(item.par.data[1], 2);
		assert_eq(item.par.data[2], 3);
	}
	
	{
		ser8 item = bmldeserialize<ser8>("f=AA\ng=AAAA\nh=AAAAAAAA\ni=AAAAAAAA\nj=AAAAAAAAAAAAAAAA");
		assert_eq(item.f, 0xAA);
		assert_eq(item.g, 0xAAAA);
		assert_eq(item.h, 0xAAAAAAAA);
		assert_eq(item.i, 0xAAAAAAAA);
		assert_eq(item.j, 0xAAAAAAAAAAAAAAAA);
	}
	
	{
		ser9 item = bmldeserialize<ser9>("foo=12345678\nbar=12345678");
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
		ser10 item = bmldeserialize<ser10>("data=test");
		assert_eq(item.data, "test");
	}
	
	{
		ser11 item = bmldeserialize<ser11>("data=foo\ndata=\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\"\ndata=test");
		assert(item.data.contains("foo"));
		assert(item.data.contains("test"));
		assert(item.data.contains("C:/Users/Administrator/My Documents/!TOP SECRET!.docx"));
	}
	
	{
		ser12 item = bmldeserialize<ser12>("data"
		                                   " foo=bar"
		                                   " -C-3A-2FUsers-2FAdministrator-2FMy-20Documents-2F-21TOP-20SECRET-21.docx=test"
		                                   " test=\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\"");
		assert_eq(item.data.get("foo"), "bar");
		assert_eq(item.data.get("test"), "C:/Users/Administrator/My Documents/!TOP SECRET!.docx");
		assert_eq(item.data.get("C:/Users/Administrator/My Documents/!TOP SECRET!.docx"), "test");
	}
	
	{
		ser14 item = bmldeserialize<ser14>("foo=123456789\nbar=1234567890987654321");
		assert_eq(item.foo.n, 123456789);
		assert_eq(item.bar.n, 1234567890987654321);
	}
	
	{
		cstring bml = R"(
a
 c
  e=0
  f=0
 d
  e=3
  f=4
a
 c
  e=1
 c
  f=2
 q
b
 d
  e=0
  f=0
  q=0
  f=8
  q f=0
  e=7
 c
 c
 c
  e=5
  f=6
q
a
b
 c
 d
c e=0
q
)";
		int ace = 9;
		int acf = 9;
		int ade = 9;
		int adf = 9;
		int bce = 9;
		int bcf = 9;
		int bde = 9;
		int bdf = 9;
		
		//this is some pretty ugly code, it's not meant to be used like this. real uses look better
		bmldeserializer s(bml);
		
		ser_enter(s)
		{
			//with properly written serializers, while/if makes no difference
			//TODO: find a way to represent these operations that serializer can use too
			while (s.next() == "a") ser_enter(s)
			{
				while (s.next() == "c") ser_enter(s)
				{
					s.item("e", ace);
					s.item("f", acf);
				}
				while (s.next() == "d") ser_enter(s)
				{
					s.item("e", ade);
					s.item("f", adf);
				}
			}
			if (s.next() == "b") ser_enter(s)
			{
				if (s.next() == "c") ser_enter(s)
				{
					s.item("e", bce);
					s.item("f", bcf);
				}
				if (s.next() == "d") ser_enter(s)
				{
					s.item("e", bde);
					s.item("f", bdf);
				}
			}
		}
		
		string values = tostring(ace)+tostring(acf)+tostring(ade)+tostring(adf)+
		                tostring(bce)+tostring(bcf)+tostring(bde)+tostring(bdf);
		assert_eq(values, "12345678");
		assert_eq(ace, 1);
		assert_eq(acf, 2);
		assert_eq(ade, 3);
		assert_eq(adf, 4);
		assert_eq(bce, 5);
		assert_eq(bcf, 6);
		assert_eq(bde, 7);
		assert_eq(bdf, 8);
	}
}

test("JSON serialization", "json", "serialize")
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
		ser5 item;
		item.data.append(1);
		item.data.append(2);
		item.data.append(3);
		assert_eq(jsonserialize(item), "{\"data\":[1,2,3]}");
	}
	
	{
		ser6 item;
		item.data.append();
		item.data.append();
		item.data[0].a=1;
		item.data[0].b=2;
		item.data[1].a=3;
		item.data[1].b=4;
		assert_eq(jsonserialize(item), "{\"data\":[{\"a\":1,\"b\":2},{\"a\":3,\"b\":4}]}");
	}
	
	{
		ser7 item;
		item.par.data.append(1);
		item.par.data.append(2);
		item.par.data.append(3);
		assert_eq(jsonserialize(item), "{\"par\":{\"data\":[1,2,3]}}");
	}
	
	{
		ser8 item;
		item.f = 0xAA;
		item.g = 0xAAAA;
		item.h = 0xAAAAAAAA;
		item.i = 0xAAAAAAAA; // this could have another eight As on linux, but why would I even do that
		item.j = 0xAAAAAAAAAAAAA000; // rounded due to float precision
		assert_eq(jsonserialize(item), "{\"f\":170,\"g\":43690,\"h\":2863311530,\"i\":2863311530,\"j\":12297829382473031680}");
	}
	
	{
		ser9 item;
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
		ser10 item;
		item.data = "test";
		assert_eq(jsonserialize(item), "{\"data\":\"test\"}");
	}
	
	{
		ser11 item;
		item.data.add("foo");
		item.data.add("test");
		item.data.add("C:/Users/Administrator/My Documents/!TOP SECRET!.docx");
		//the set is unordered, this can give spurious failures
		if (sizeof(size_t) == 8)
			assert_eq(jsonserialize(item), "{\"data\":[\"test\",\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\",\"foo\"]}");
		else
			assert_eq(jsonserialize(item), "{\"data\":[\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\",\"foo\",\"test\"]}");
	}
	
	{
		ser12 item;
		item.data.insert("foo", "bar");
		item.data.insert("test", "C:/Users/Administrator/My Documents/!TOP SECRET!.docx");
		item.data.insert("C:/Users/Administrator/My Documents/!TOP SECRET!.docx", "test");
		if (sizeof(size_t) == 8)
			assert_eq(jsonserialize(item), "{\"data\":{\"test\":\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\","
			                              "\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\":\"test\","
			                              "\"foo\":\"bar\"}}");
		else
			assert_eq(jsonserialize(item), "{\"data\":{\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\":\"test\","
			                              "\"foo\":\"bar\","
			                              "\"test\":\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\"}}");
	}
	
	{
		ser13 item;
		item.foo[0] = 12;
		item.foo[1] = 34;
		item.foo[2] = 56;
		item.foo[3] = 78;
		item.t = true;
		item.f = false;
		assert_eq(jsonserialize(item), "{\"foo\":[12,34,56,78],\"t\":true,\"f\":false}");
	}
	
	{
		ser14 item;
		item.foo.n = 123456789;
		item.bar.n = 1234567890987654321;
		assert_eq(jsonserialize(item), "{\"foo\":123456789,\"bar\":\"1234567890987654321\"}");
	}
}

test("JSON deserialization", "json", "serialize")
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
		ser1 item = jsondeserialize<ser1>("{ \"a\":1, \"b\":2, \"q\":0, \"a\":3, \"a\":4 }");
		assert_eq(item.a, 4);
		assert_eq(item.b, 2);
	}
	
	//these pass if they do not give infinite loops or valgrind errors, or otherwise explode
	{
		jsondeserialize<ser1>("{ \"a\":1, \"b\":2, \"q\":*, \"a\":3, \"a\":4 }");
		jsondeserialize<ser6>("{\"data\":[{\"a\":\"a\n[]\"}]}");
		jsondeserialize<map<string,int>>("{\"aaaaaaaaaaaaaaaa\":1,\"bbbbbbbbbbbbbbbb\":2}");
	}
	
	//the system is allowed to loop, but only if there's bogus or extraneous nodes
	//we want O(n) runtime for a clean document, so ensure no looping
	//this includes missing and duplicate elements, both of which are possible for serialized arrays
	{
		ser4 item = jsondeserialize<ser4>("{ \"a\":1, \"b\":2, \"c\":3, \"d\":4, \"e\":5, \"f\":6 }");
		assert_eq(item.count, 1);
	}
	
	{
		ser5 item = jsondeserialize<ser5>("{ \"data\": [ 1, 2, 3 ] }");
		assert_eq(item.data.size(), 3);
		assert_eq(item.data[0], 1);
		assert_eq(item.data[1], 2);
		assert_eq(item.data[2], 3);
	}
	
	{
		ser6 item = jsondeserialize<ser6>("{ \"data\": [ { \"a\":1,\"b\":2 }, { \"a\":3,\"b\":4 } ] }");
		assert_eq(item.data.size(), 2);
		assert_eq(item.data[0].a, 1);
		assert_eq(item.data[0].b, 2);
		assert_eq(item.data[1].a, 3);
		assert_eq(item.data[1].b, 4);
	}
	
	{
		//ensure finish_item() isn't screwing up
		ser7 item = jsondeserialize<ser7>("{ \"foo\": {\"bar\": {}}, \"par\": { \"data\": [ 1, 2, 3 ] } }");
		assert_eq(item.par.data.size(), 3);
		assert_eq(item.par.data[0], 1);
		assert_eq(item.par.data[1], 2);
		assert_eq(item.par.data[2], 3);
	}
	
	{
		ser8 item = jsondeserialize<ser8>("{ \"f\": 100, \"g\": 10000, \"h\": 1000000000, \"i\": 1000000000, \"j\": 1000000000000000000 } ");
		assert_eq(item.f, 100);
		assert_eq(item.g, 10000);
		assert_eq(item.h, 1000000000);
		assert_eq(item.i, 1000000000);
		assert_eq(item.j, 1000000000000000000ull); // this is the hex test object, json doesn't support hex
	}
	
	{
		ser9 item = jsondeserialize<ser9>("{ \"foo\": \"12345678\", \"bar\": \"12345678\" }");
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
		ser10 item = jsondeserialize<ser10>("{ \"data\": \"test\" }");
		assert_eq(item.data, "test");
	}
	
	{
		ser11 item = jsondeserialize<ser11>("{ \"data\": [ \"foo\", \"test\", \"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\" ] }");
		assert(item.data.contains("foo"));
		assert(item.data.contains("test"));
		assert(item.data.contains("C:/Users/Administrator/My Documents/!TOP SECRET!.docx"));
	}
	
	{
		ser12 item = jsondeserialize<ser12>(
		    "{ \"data\": { \"foo\": \"bar\", "
		                  "\"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\": \"test\", "
		                  "\"test\": \"C:/Users/Administrator/My Documents/!TOP SECRET!.docx\" } }");
		assert_eq(item.data.get_or("foo", "NONEXISTENT"), "bar");
		assert_eq(item.data.get_or("test", "NONEXISTENT"), "C:/Users/Administrator/My Documents/!TOP SECRET!.docx");
		assert_eq(item.data.get_or("C:/Users/Administrator/My Documents/!TOP SECRET!.docx", "NONEXISTENT"), "test");
	}
	
	{
		ser13 item = jsondeserialize<ser13>("{\"foo\":[12,34,56,78],\"t\":true,\"f\":false}");
		assert_eq(item.foo[0], 12);
		assert_eq(item.foo[1], 34);
		assert_eq(item.foo[2], 56);
		assert_eq(item.foo[3], 78);
		assert_eq(item.t, true);
		assert_eq(item.f, false);
	}
	
	{
		ser14 item = jsondeserialize<ser14>("{\"foo\":123456789,\"bar\":\"1234567890987654321\"}");
		assert_eq(item.foo.n, 123456789);
		assert_eq(item.bar.n, 1234567890987654321);
	}
}
#endif
