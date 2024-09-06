#ifdef _WIN32
#include "runloop2.h"
#include <windows.h>
#ifdef ARLIB_SOCKET
#include "socket.h"
#endif

#if defined(ARLIB_GAME) || defined(ARLIB_GUI)
#define ENABLE_MSGPUMP
void _window_process_events();
#endif

namespace {

class runloop2_windows;
runloop2_windows& get_loop();

class runloop2_windows {
public:
#if !defined(ENABLE_MSGPUMP)
	static const bool has_gui_events = false;
#elif !defined(ARLIB_THREAD)
	static const bool has_gui_events = true;
#else
	bool has_gui_events = false;
#endif
	
#ifdef ARLIB_TESTRUNNER
	timestamp last_iter;
	bool test_has_runloop;
#endif
	
	struct handle_node {
		producer<void> prod = make_producer<&handle_node::prod, &handle_node::cancel>();
		void cancel() { get_loop().handle_cancel(this); }
		HANDLE h;
	};
	allocatable_array<handle_node, [](handle_node* n) { return (uintptr_t*)&n->h; }, [](handle_node* n) { n->prod.moved(); }> handles;
	
	async<void> handle_set(HANDLE h)
	{
		handle_node& n = *handles.alloc();
		n.h = h;
		return &n.prod;
	}
	void handle_activate(handle_node* n)
	{
		handles.dealloc(n);
		n->prod.complete();
	}
	void handle_cancel(handle_node* n)
	{
		handles.dealloc(n);
	}
	
	struct timer_node {
		producer<void> prod = make_producer<&timer_node::prod, &timer_node::cancel>();
		void cancel() { get_loop().timer_cancel(this); }
		timestamp timeout;
	};
	allocatable_array<timer_node, [](timer_node* n) { return &n->timeout.sec; }, [](timer_node* n) { n->prod.moved(); }> timers;
	
	async<void> timer_set(timestamp timeout)
	{
		timer_node& n = *timers.alloc();
		n.timeout = timeout;
		return &n.prod;
	}
	void timer_activate(timer_node* n)
	{
		timers.dealloc(n);
		n->prod.complete();
	}
	void timer_cancel(timer_node* n)
	{
		timers.dealloc(n);
	}
	
	
	fifo<std::coroutine_handle<>> scheduled;
	void schedule(std::coroutine_handle<> coro)
	{
		scheduled.push(coro);
	}
	
	bool step(bool wait)
	{
		timestamp timeout = timestamp::at_never();
		for (auto& node : timers)
		{
			if (node.prod.has_waiter() && node.timeout <= timeout)
				timeout = node.timeout;
		}
		
		HANDLE hs[MAXIMUM_WAIT_OBJECTS];
		handle_node* nodes[MAXIMUM_WAIT_OBJECTS];
		size_t n_hs = 0;
		for (auto& node : handles)
		{
			if (node.prod.has_waiter() && (int)n_hs < MAXIMUM_WAIT_OBJECTS-has_gui_events)
			{
				nodes[n_hs] = &node;
				hs[n_hs] = node.h;
				n_hs++;
			}
		}
		
		timestamp now = timestamp::now();
		duration dur = timeout - now;
		
		if (dur.sec < 0 || !scheduled.empty() || !wait)
			dur = { 0, 0 };
		
		test_rethrow();
#ifdef ARLIB_TESTRUNNER
		test_has_runloop = true;
		test_iter_end();
#endif
		uint32_t wait_ret;
#ifdef ENABLE_MSGPUMP
		if (has_gui_events)
		{
			if (n_hs > MAXIMUM_WAIT_OBJECTS-1) // just ignore the excess ones; it's a poor solution,
				n_hs = MAXIMUM_WAIT_OBJECTS-1; // but I haven't been able to find anything better, and I don't need it anyways
			wait_ret = MsgWaitForMultipleObjectsEx(n_hs, hs, dur.ms(), QS_ALLEVENTS, MWMO_ALERTABLE|MWMO_INPUTAVAILABLE);
			_window_process_events();
		}
		else
#endif
		{
			if (n_hs > MAXIMUM_WAIT_OBJECTS)
				n_hs = MAXIMUM_WAIT_OBJECTS;
			if (n_hs == 0) { SleepEx(dur.ms(), true); wait_ret = WAIT_TIMEOUT; }
			else wait_ret = WaitForMultipleObjectsEx(n_hs, hs, false, dur.ms(), true);
		}
#ifdef ARLIB_TESTRUNNER
		test_iter_begin();
#endif
		
		if (wait_ret < n_hs)
			handle_activate(nodes[wait_ret]);
		
		now = timestamp::now();
		
		for (auto& node : timers)
		{
			if (node.prod.has_waiter() && node.timeout <= now)
				timer_activate(&node);
		}
		
		while (!scheduled.empty())
		{
			scheduled.pop().resume();
		}
		
		return false;
	}
	
#ifdef ARLIB_SOCKET
	void* dns = nullptr;
#ifndef ARLIB_THREAD
	runloop2_windows()
	{
		dns = socket2::dns_create();
	}
#endif
	~runloop2_windows()
	{
#if defined(ARLIB_THREAD) || defined(ARLIB_TESTRUNNER)
		if (dns)
#endif
			socket2::dns_destroy(dns);
	}
	void* get_dns()
	{
#if defined(ARLIB_THREAD) || defined(ARLIB_TESTRUNNER)
		if (!dns)
			dns = socket2::dns_create();
#endif
		return dns;
	}
#endif
#ifdef ARLIB_TESTRUNNER
	void test_iter_begin()
	{
		last_iter = timestamp::now();
	}
	void test_iter_end()
	{
		if (test_has_runloop)
			_test_runloop_latency(timestamp::now() - last_iter);
	}
	void test_begin()
	{
		test_has_runloop = false;
		test_iter_begin();
	}
	void test_end()
	{
		test_iter_end();
#ifdef ARLIB_SOCKET
		if (dns)
		{
			socket2::dns_destroy(dns);
			dns = nullptr;
		}
#endif
		for (auto& node : handles)
			assert(!node.prod.has_waiter());
		for (auto& node : timers)
			assert(!node.prod.has_waiter());
		assert(scheduled.empty());
	}
#endif
};

#ifdef ARLIB_THREAD
static DWORD tls_id;
oninit_early()
{
	tls_id = TlsAlloc();
	runloop2_windows* g_loop = new runloop2_windows();
#ifdef ENABLE_MSGPUMP
	g_loop->has_gui_events = true;
#endif
	TlsSetValue(tls_id, g_loop);
}
runloop2_windows& get_loop()
{
	runloop2_windows* ret = (runloop2_windows*)TlsGetValue(tls_id);
	if (!ret)
	{
		ret = new runloop2_windows();
		TlsSetValue(tls_id, ret);
	}
	return *ret;
}

#else

static runloop2_windows loop;
runloop2_windows& get_loop() { return loop; }
#endif

}

namespace runloop2 {
	void schedule(std::coroutine_handle<> coro) { get_loop().schedule(coro); }
	bool step(bool wait) { return get_loop().step(wait); }
	void run(async<void> event)
	{
		waiter<void> wait;
		event.then(&wait);
		auto& loop = get_loop();
		while (wait.is_waiting())
			loop.step(true);
	}
	async<void> await_handle(HANDLE h) { return get_loop().handle_set(h); }
	async<void> await_timeout(timestamp timeout) { return get_loop().timer_set(timeout); }
	async<void> in_ms(int ms) { return get_loop().timer_set(timestamp::in_ms(ms)); }
#ifdef ARLIB_SOCKET
	void* get_dns() { return get_loop().get_dns(); }
#endif
#ifdef ARLIB_TESTRUNNER
	void test_begin() { get_loop().test_begin(); }
	void test_end() { get_loop().test_end(); }
#endif
}
#endif
