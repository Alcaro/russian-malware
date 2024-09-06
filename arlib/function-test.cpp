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
	int addc(int y) const { return x+y; }
	void addp(int* y, int z) { *y += x+z; }
	function<int(int)> wrap() { return bind_this(&adder::add); }
	function<int(int)> wrapc() const { return bind_this(&adder::addc); }
	function<void(int*,int)> wrapp() { return bind_this(&adder::addp); }
	
	static int n_adders;
};
int adder::n_adders = 0;
}

test("function", "", "function")
{
	test_nomalloc {
		function<void()> fn;
		assert(!fn);
		fn = [](){};
		function<void()> fn2 = fn;
		assert(fn);
		assert(fn2);
		fn = nullptr;
		assert(!fn);
		
		big_function<void()> fn3;
		assert(!fn3);
		big_function<void()> fn4 = nullptr;
		assert(!fn4);
	}
	
	test_nomalloc {
		function<int()> r42w = r42;
		assert_eq(r42w(), 42);
	}
	
	test_nomalloc {
		function<int(int)> a42w = add42;
		assert_eq(a42w(10), 52);
	}
	
	test_nomalloc {
		adder a42(42);
		function<int(int)> a42w = a42.wrap();
		assert_eq(a42w(10), 52);
		assert_eq(adder::n_adders, 1);
	}
	assert_eq(adder::n_adders, 0);
	
	test_nomalloc {
		adder a42(42);
		function<int(int)> a42w = a42.wrapc();
		assert_eq(a42w(10), 52);
		assert_eq(adder::n_adders, 1);
	}
	assert_eq(adder::n_adders, 0);
	
	test_nomalloc {
		adder a42(42);
		int a = 10;
		const function<void(int*, int)> a42w = a42.wrapp();
		a42w(&a, 10);
		assert_eq(a, 62);
		assert_eq(adder::n_adders, 1);
	}
	assert_eq(adder::n_adders, 0);
	
	//various weird things not used in practice; I'll reenable if needed
	//{ // bind_ptr_del allocates
	//	function<int(int)> a42w = bind_ptr_del(&adder::add, new adder(42));
	//	assert_eq(a42w(10), 52);
	//	assert_eq(adder::n_adders, 1);
	//}
	//assert_eq(adder::n_adders, 0);
	
	test_nomalloc {
		function<int(int)> a42w = [](int x)->int { return x+42; };
		assert_eq(a42w(10), 52);
	}
	
	test_nomalloc {
		int n = 42;
		function<int(int)> a42w = [=](int x)->int { return x+n; };
		n = -42;
		assert_eq(a42w(10), 52);
	}
	
	test_nomalloc {
		int n = -42;
		function<int(int)> a42w = [&](int x)->int { return x+n; };
		n = 42;
		assert_eq(a42w(10), 52);
	}
	
	test_nomalloc {
		int a = 1;
		function<void(int*)> inc = [](int* a){ ++*a; };
		inc(&a);
		inc(&a);
		assert_eq(a, 3);
	}
	
	test_nomalloc {
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
		
		assert_eq(std::is_trivially_move_constructible_v<decltype(no_bind)>, true);
		assert_eq(std::is_trivially_move_constructible_v<decltype(val_bind)>, true);
		assert_eq(std::is_trivially_move_constructible_v<decltype(ref_bind)>, true);
	}
	
	/*
	test_nomalloc {
		function<int(int arg, ...)> foo = [](int arg, ...)->int {
			va_list args;
			va_start(args, arg);
			return va_arg(args, int);
		};
		
		assert_eq(foo(42, 123), 123);
		assert_eq(foo(42, 123, 456), 123);
	}
	
	test_nomalloc {
		void* ctx;
		int(*foo)(void* ctx, int arg, ...) = function([](int arg, ...)->int {
			va_list args;
			va_start(args, arg);
			return va_arg(args, int);
		}).decompose(&ctx);
		
		assert_eq(foo(ctx, 42, 123), 123);
		assert_eq(foo(ctx, 42, 123, 456), 123);
	}
	*/
	
	// test deduction guide
	{
		function fn = function([](){ return 5; });
		assert_eq(fn(), 5);
		function fn2 = [](){ return 5; };
		assert_eq(fn2(), 5);
	}
}
