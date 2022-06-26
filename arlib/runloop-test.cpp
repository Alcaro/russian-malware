#include "runloop2.h"
#include "test.h"

namespace {

static int test_state = 0;

class inner_awaitable_t {
	// ideally, this would be producer_fn<float, &inner_awaitable_t::prod, &inner_awaitable_t::cancel> prod,
	// but c++ doesn't accept that kind of self/forward references
	struct prod_t : public producer_fn<float, prod_t> {
		void cancel() { container_of<&inner_awaitable_t::prod>(this)->cancel(); }
	} prod;
	
public:
	async<float> start_async()
	{
		return &prod;
	}
	async<float> start_sync()
	{
		prod.complete(1234.5);
		return &prod;
	}
	void cancel()
	{
		if (test_state == 7)
		{
			assert_eq(test_state++, 7);
			assert_eq(test_state++, 8);
		}
		else
		{
			assert_eq(test_state++, 9);
			assert_eq(test_state++, 10);
		}
	}
	void complete()
	{
		prod.complete(1234.5);
	}
};
inner_awaitable_t inner_awaitable;

async<int> coro_sync()
{
	co_return 42;
}

async<int> coro_async()
{
	assert_eq(co_await inner_awaitable.start_async(), 1234.5);
	co_return 42;
}

struct cancellable_coro {
	struct promise_type {
		cancellable_coro get_return_object() { return coro_from_promise(*this); }
		std::suspend_never initial_suspend() { return {}; }
		std::suspend_always final_suspend() noexcept { return {}; }
		void unhandled_exception() { __builtin_trap(); }
	};
	std::coroutine_handle<> coro;
	cancellable_coro(std::coroutine_handle<> coro) : coro(coro) {}
	cancellable_coro(const cancellable_coro&) = delete;
	cancellable_coro(cancellable_coro&& other)
	{
		coro = other.coro;
		other.coro = nullptr;
	}
	~cancellable_coro()
	{
		if (coro) coro.destroy();
	}
	void cancel()
	{
		assert_eq(test_state++, 7);
		assert_eq(test_state++, 8);
		coro.destroy();
		coro = nullptr;
	}
};

static cancellable_coro coro_call_coro()
{
	assert_eq(test_state++, 1);
	assert_eq(co_await coro_sync(), 42);
	assert_eq(test_state++, 2);
	assert_eq(co_await coro_sync(), 42);
	assert_eq(test_state++, 3);
	assert_eq(co_await coro_async(), 42);
	assert_eq(test_state++, 5);
	assert_eq(co_await coro_async(), 42);
	assert_eq(test_state++, 7);
}

static cancellable_coro coro_call_fn()
{
	test_nomalloc {
	assert_eq(test_state++, 1);
	assert_eq(co_await inner_awaitable.start_sync(), 1234.5);
	assert_eq(test_state++, 2);
	assert_eq(co_await inner_awaitable.start_sync(), 1234.5);
	assert_eq(test_state++, 3);
	assert_eq(co_await inner_awaitable.start_async(), 1234.5);
	assert_eq(test_state++, 5);
	}
	assert_eq(co_await inner_awaitable.start_async(), 1234.5);
	assert_eq(test_state++, 7);
}

class fn_caller {
	struct wait_t : public waiter_fn<int, wait_t> {
		void complete(int val) { container_of<&fn_caller::wait>(this)->complete1(val); }
	} wait;
	struct wait2_t : public waiter_fn<float, wait2_t> {
		void complete(float val) { container_of<&fn_caller::wait2>(this)->complete2(val); }
	} wait2;
	int state = 0;
public:
	void call_coro()
	{
		state = 0;
		complete1(42);
	}
	void call_fn()
	{
		state = 10;
		complete2(1234.5f);
	}
	void complete1(int val)
	{
		assert_eq(val, 42);
		switch (state++)
		{
		case 0:
			assert_eq(test_state++, 1);
			coro_sync().then(&wait);
			break;
		case 1:
			assert_eq(test_state++, 2);
			coro_sync().then(&wait);
			break;
		case 2:
			assert_eq(test_state++, 3);
			coro_async().then(&wait);
			break;
		case 3:
			assert_eq(test_state++, 5);
			coro_async().then(&wait);
			break;
		case 4:
			assert_eq(test_state++, 7);
			break;
		default:
			assert_unreachable();
			break;
		}
	}
	
	void complete2(float val)
	{
		assert_eq(val, 1234.5);
		switch (state++)
		{
		case 10:
			assert_eq(test_state++, 1);
			inner_awaitable.start_sync().then(&wait2);
			break;
		case 11:
			assert_eq(test_state++, 2);
			inner_awaitable.start_sync().then(&wait2);
			break;
		case 12:
			assert_eq(test_state++, 3);
			inner_awaitable.start_async().then(&wait2);
			break;
		case 13:
			assert_eq(test_state++, 5);
			inner_awaitable.start_async().then(&wait2);
			break;
		case 14:
			assert_eq(test_state++, 7);
			break;
		default:
			assert_unreachable();
			break;
		}
	}
};

}

test("runloop coroutines", "", "runloop")
{
	test_state = 0;
	{
		assert_eq(test_state++, 0);
		cancellable_coro ccc = coro_call_coro();
		
		assert_eq(test_state++, 4);
		inner_awaitable.complete();
		assert_eq(test_state++, 6);
		inner_awaitable.complete();
		assert_eq(test_state++, 8);
	}
	assert_eq(test_state++, 9);
	
	test_state = 0;
	{
		assert_eq(test_state++, 0);
		cancellable_coro ccc = coro_call_coro();
		
		assert_eq(test_state++, 4);
		inner_awaitable.complete();
		assert_eq(test_state++, 6);
		ccc.cancel();
		assert_eq(test_state++, 11);
	}
	assert_eq(test_state++, 12);
	
	test_state = 0;
	{
		assert_eq(test_state++, 0);
		cancellable_coro ccc = coro_call_fn();
		
		assert_eq(test_state++, 4);
		inner_awaitable.complete();
		assert_eq(test_state++, 6);
		inner_awaitable.complete();
		assert_eq(test_state++, 8);
	}
	assert_eq(test_state++, 9);
	
	test_state = 0;
	{
		assert_eq(test_state++, 0);
		cancellable_coro ccc = coro_call_fn();
		
		assert_eq(test_state++, 4);
		inner_awaitable.complete();
		assert_eq(test_state++, 6);
		ccc.cancel();
		assert_eq(test_state++, 11);
	}
	assert_eq(test_state++, 12);
	
	test_state = 0;
	{
		fn_caller fnc;
		assert_eq(test_state++, 0);
		fnc.call_coro();
		assert_eq(test_state++, 4);
		inner_awaitable.complete();
		assert_eq(test_state++, 6);
		inner_awaitable.complete();
		assert_eq(test_state++, 8);
	}
	assert_eq(test_state++, 9);
	
	test_state = 0;
	{
		fn_caller fnc;
		assert_eq(test_state++, 0);
		fnc.call_coro();
		assert_eq(test_state++, 4);
		inner_awaitable.complete();
		assert_eq(test_state++, 6);
	}
	assert_eq(test_state++, 9);
	
	test_state = 0;
	test_nomalloc
	{
		fn_caller fnc;
		assert_eq(test_state++, 0);
		fnc.call_fn();
		assert_eq(test_state++, 4);
		inner_awaitable.complete();
		assert_eq(test_state++, 6);
		inner_awaitable.complete();
		assert_eq(test_state++, 8);
	}
	assert_eq(test_state++, 9);
	
	test_state = 0;
	test_nomalloc
	{
		fn_caller fnc;
		assert_eq(test_state++, 0);
		fnc.call_fn();
		assert_eq(test_state++, 4);
		inner_awaitable.complete();
		assert_eq(test_state++, 6);
	}
	assert_eq(test_state++, 9);
}


co_test("runloop timeouts", "", "runloop")
{
	timestamp t1 = timestamp::now();
	co_await runloop2::await_timeout(timestamp::in_ms(10));
	timestamp t2 = timestamp::now();
	assert_range((t2-t1).ms(), 10, 100);
	
	co_await runloop2::await_timeout(timestamp::in_ms(-1000));
	co_await runloop2::await_timeout(timestamp::in_ms(-1000));
	co_await runloop2::await_timeout(timestamp::in_ms(-1000));
	co_await runloop2::await_timeout(timestamp::in_ms(-1000));
	timestamp t3 = timestamp::now();
	assert_range((t3-t2).ms(), 0, 100);
	
	struct prod_t : public producer_fn<ssize_t, prod_t> {
		void cancel() {}
	} prod;
	
	ssize_t n = co_await async<ssize_t>(&prod).with_timeout<-5>(timestamp::in_ms(10));
	assert_eq(n, -5);
	
	prod.complete(42);
	n = co_await async<ssize_t>(&prod).with_timeout<-5>(timestamp::in_ms(1000));
	assert_eq(n, 42);
	
	bool b = co_await runloop2::await_timeout(timestamp::in_ms(10)).with_timeout(timestamp::in_ms(100));
	assert_eq(b, true);
	b = co_await runloop2::await_timeout(timestamp::in_ms(100)).with_timeout(timestamp::in_ms(10));
	assert_eq(b, false);
}


static async<void> mut_test(co_mutex& mut, int& state, int a, int b, int c)
{
	assert_eq(++state, a);
	{
		auto lock = co_await mut;
		assert(mut.locked());
		assert_eq(++state, b);
		co_await runloop2::await_timeout(timestamp::in_ms(0));
	}
	assert_eq(++state, c);
}

static void my_cancel(async<void> event)
{
	struct noop_waiter_t : public waiter_fn<void, noop_waiter_t> { void complete() { __builtin_trap(); } };
	noop_waiter_t wait;
	event.then(&wait);
}

static void test1(std::initializer_list<int> holder)
{
	arrayview<int> vals = { holder.begin(), holder.size() };
	
	int end = 0;
	for (int v : vals)
		end = max(v, end);
	
	co_mutex mut;
	assert(!mut.locked());
	int state = 0;
	for (size_t i=0;i<vals.size();i+=3)
	{
		if (vals[i+2] != -1)
			runloop2::detach(mut_test(mut, state, vals[i], vals[i+1], vals[i+2]));
		else
			my_cancel(mut_test(mut, state, vals[i], vals[i+1], vals[i+2]));
	}
	while (state != end)
		runloop2::step();
	assert(!mut.locked());
	assert_eq(state, end);
}

test("co_mutex", "", "")
{
	co_mutex mut;
	int state = 0;
	assert(!mut.locked());
	runloop2::detach(mut_test(mut, state, 1, 2, 7));
	assert(mut.locked());
	runloop2::detach(mut_test(mut, state, 3, 8, 9));
	runloop2::detach(mut_test(mut, state, 4,10,11));
	my_cancel(mut_test(mut, state, 5,-1,-1));
	runloop2::detach(mut_test(mut, state, 6,12,13));
	
	while (state != 13)
		runloop2::step();
	assert(!mut.locked());
	assert_eq(state, 13);
	
	testcall(test1({ 1,2,7,  3,8,9,   4,10,11, 5,-1,-1, 6,12,13 }));
	testcall(test1({ 1,2,13, 3,-1,-1, 4,-1,-1, 5,-1,-1, 6,-1,-1, 7,-1,-1,  8,-1,-1,  9,-1,-1,  10,-1,-1, 11,-1,-1, 12,-1,-1 }));
	testcall(test1({ 1,2,-1, 3,4,-1,  5,6,-1,  7,8,-1,  9,10,-1, 11,12,-1, 13,14,-1, 15,16,-1, 17,18,-1, 19,20,-1, 21,22,-1 }));
	testcall(test1({ 1,2,13, 3,-1,-1, 4,-1,-1, 5,-1,-1, 6,14,15, 7,-1,-1,  8,-1,-1,  9,-1,-1,  10,-1,-1, 11,-1,-1, 12,-1,-1 }));
	testcall(test1({ 1,2,13, 3,14,15, 4,16,17, 5,18,19, 6,20,21, 7,22,23,  8,24,25,  9,26,27,  10,28,29, 11,-1,-1, 12,-1,-1 }));
}
