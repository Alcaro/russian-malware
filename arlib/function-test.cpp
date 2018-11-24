#include "function.h"
#include "test.h"

namespace {
static int r42() { return 42; }
static int add42(int x) { return x+42; }
class adder {
	int x;
public:
	adder(int x) : x(x)
	{
		n_adders++;
	}
	~adder() { n_adders--; }
	int add(int y) { return x+y; }
	function<int(int)> wrap() { return bind_this(&adder::add); }
	
	static int n_adders;
};
int adder::n_adders = 0;
}

test("function", "", "function")
{
	{
		function<int()> r42w = r42;
		assert_eq(r42w(), 42);
	}
	
	{
		function<int(int)> a42w = add42;
		assert_eq(a42w(10), 52);
	}
	
	{
		adder a42(42);
		function<int(int)> a42w = a42.wrap();
		assert_eq(a42w(10), 52);
		
		assert_eq(adder::n_adders, 1);
	}
	assert_eq(adder::n_adders, 0);
	
	{
		function<int(int)> a42w = bind_ptr_del(&adder::add, new adder(42));
		assert_eq(a42w(10), 52);
		
		assert_eq(adder::n_adders, 1);
	}
	assert_eq(adder::n_adders, 0);
	
	{
		function<int(int)> a42w = bind_lambda([](int x)->int { return x+42; });
		assert_eq(a42w(10), 52);
	}
	
	{
		int n = 42;
		function<int(int)> a42w = bind_lambda([=](int x)->int { return x+n; });
		n = -42;
		assert_eq(a42w(10), 52);
	}
	
	{
		int n = -42;
		function<int(int)> a42w = bind_lambda([&](int x)->int { return x+n; });
		n = 42;
		assert_eq(a42w(10), 52);
	}
	
	{
		int n = 42;
		function<int(int)> a42w = bind_lambda([](int* xp, int x)->int { return x+*xp; }, &n);
		assert_eq(a42w(10), 52);
	}
	
	{
		int a = 2;
		int b = 3;
		int c = 5;
		function<int()> mul = bind_lambda([=]()->int { return a*b*c; });
		a = 1;
		b = 1;
		c = 1;
		assert_eq(mul(), 2*3*5);
	}
	
	{
		int n = 42;
		auto no_bind = []() -> int { return 42; };
		n++;
		auto val_bind = [n]() -> int { return n; };
		n++;
		auto ref_bind = [&n]() -> int { return n; };
		n++;
		
		assert_eq(no_bind(), 42);
		assert_eq(val_bind(), 43);
		assert_eq(ref_bind(), 45);
		
		assert_eq(sizeof(no_bind), 1);
		assert_eq(sizeof(val_bind), sizeof(int));
		assert_eq(sizeof(ref_bind), sizeof(int*));
		
		assert_eq(std::is_trivially_move_constructible<decltype(no_bind)>::value, true);
		assert_eq(std::is_trivially_move_constructible<decltype(val_bind)>::value, true);
		assert_eq(std::is_trivially_move_constructible<decltype(ref_bind)>::value, true);
	}
}

test("","","")
{
assert(!"rewrite function header to more argument packs; less six-arg limits, self inclusion, and specializations whose errors suck");
}
