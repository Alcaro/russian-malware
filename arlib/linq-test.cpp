#include "array.h"
#include "set.h"
#include "linq.h"
#include "test.h"

#ifndef ARLIB_OBJNAME
//for checking that it optimizes properly
int AAAAAAAAAAAAAAAAAAAAAAA()
{
	int w[] = { 1, 2, 3, 4, 5 };
	int r = 0;
	for (int n : arrayview<int>(w)
		// { 1, 2, 3, 4, 5 }
		.where([](int n) -> bool { return n&1; })
		// { 1, 3, 5 }
		.select([&](int n) -> short { return n*2; })
		// { 2, 6, 10 }
		)
	{
		r += n;
	}
	// expected: r = 18
	return r;
}
#endif

#ifdef ARLIB_TEST
test("LINQ", "array,set", "linq")
{
	{
		array<int> x = { 1, 2, 3 };
		array<short> y = x.select([&](int n) -> short { return n+x.size(); });
		assert_eq(y.size(), 3);
		assert_eq(y[0], 4);
		assert_eq(y[1], 5);
		assert_eq(y[2], 6);
	}
	
	{
		set<int> x = { 1, 2, 3 };
		set<short> y = x.select([&](int n) -> short { return n+x.size(); });
		assert_eq(y.size(), 3);
		assert(y.contains(4));
		assert(y.contains(5));
		assert(y.contains(6));
	}
	
	{
		array<int> x = { 1, 2, 3 };
		int i = 1;
		for (int n : x.select([](int n) -> short { return n*2; }))
		{
			assert_eq(n, i*2);
			i++;
		}
		assert_eq(i, 4);
	}
	
	{
		array<int> x = { 0, 1, 2, 3, 4, 5 };
		array<short> y = x
			// { 0, 1, 2, 3, 4, 5 }
			.where([](int n) -> bool { return n & 1; })
			// { 1, 3, 5 }
			.select([](int n) -> short { return n*2; })
			// { 2, 6, 10 }
			;
		array<short> z = { 2, 6, 10 };
		assert(y == z);
	}
	
	{
		array<int> x = { 1, 4, 9, 16, 25 };
		assert_eq(x.first([](int x) -> bool { return x>5; }), 9);
		//passes if it does not read outside x
		arrayview<int> y = arrayview<int>(x.ptr(), 100);
		assert_eq(y.first([](int x) -> bool { return x>5; }), 9);
		
		assert_eq(x.select([](size_t i, int x) -> int { return i+x; }).join(), 0+1 + 1+4 + 2+9 + 3+16 + 4+25);
	}
	
	{
		struct nocopy2 : nocopy {};
		nocopy2 x[10];
		assert_eq(arrayview<nocopy2>(x)
		            .where([&](const nocopy2& y)->bool { return (&y - x)&1; })
		            .select([&](const nocopy2& y)->const nocopy2& { return (&y)[-1]; })
		            .select([&](const nocopy2& y)->int { return &y - x; })
		            .as_array()
		            .join(),
		          1-1 + 3-1 + 5-1 + 7-1 + 9-1);
	}
	
	{
		string foo[5] = { "muncher", "robot", "floating muncher", "walrus", "viking" };
		arrayview<string> bar = foo;
		array<string> baz = bar.where([](cstring str)->bool { return str.contains("muncher"); });
		assert_eq(baz.join(","), "muncher,floating muncher");
	}
	
	{
		array<int> x = { 1, 2, 3, 4, 5 };
		assert_eq(x.where([](int x) -> bool { return x>5; }).join(), 0); // not allowed to assume the sequence is nonempty
	}
}
#endif
