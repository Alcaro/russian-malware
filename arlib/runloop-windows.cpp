#ifdef _WIN32
#include "runloop2.h"
#include "socket.h"
#include <windows.h>

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
	static const bool is_global = false;
#elif defined(ARLIB_THREAD)
	bool is_global = false;
#else
	static const bool is_global = true;
#endif
	
	struct handle_node {
		struct prod_t : public producer_fn<void, prod_t> {
			void cancel() { get_loop().handle_cancel(container_of<&handle_node::prod>(this)); }
		} prod;
		HANDLE h;
	};
	allocatable_array<handle_node, [](handle_node* n) { return (uintptr_t*)&n->h; }, [](handle_node* n) { n->prod.update_waiter(); }> handles;
	
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
		struct prod_t : public producer_fn<void, prod_t> {
			void cancel() { get_loop().timer_cancel(container_of<&timer_node::prod>(this)); }
		} prod;
		timestamp timeout;
	};
	allocatable_array<timer_node, [](timer_node* n) { return &n->timeout.sec; }, [](timer_node* n) { n->prod.update_waiter(); }> timers;
	
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
		size_t n_hs = 0;
		for (auto& node : handles)
		{
			if (node.prod.has_waiter() && (int)n_hs < MAXIMUM_WAIT_OBJECTS-is_global)
				hs[n_hs++] = node.h;
		}
		
		timestamp now = timestamp::now();
		duration dur = timeout - now;
		
		if (dur.sec < 0 || !scheduled.empty() || !wait)
			dur = { 0, 0 };
		
		uint32_t wait_ret;
		if (is_global)
		{
#ifdef ENABLE_MSGPUMP
			wait_ret = MsgWaitForMultipleObjectsEx(n_hs, hs, dur.ms(), QS_ALLEVENTS, MWMO_ALERTABLE|MWMO_INPUTAVAILABLE);
			_window_process_events();
#endif
		}
		else
		{
			if (n_hs == 0) { SleepEx(dur.ms(), true); wait_ret = WAIT_TIMEOUT; }
			else wait_ret = WaitForMultipleObjectsEx(n_hs, hs, false, dur.ms(), true);
		}
		
		if (wait_ret < n_hs)
			handle_activate(&handles.begin()[wait_ret]);
		
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
	runloop2_windows()
	{
printf("mkrunloop=%p\n",this);
		dns = socket2::dns_create();
	}
	~runloop2_windows()
	{
printf("rmrunloop=%p\n",this);
		socket2::dns_destroy(dns);
	}
#endif
};

#ifdef ARLIB_THREAD
static DWORD tls_id;
oninit()
{
	tls_id = TlsAlloc();
	runloop2_windows* g_loop = new runloop2_windows();
#ifdef ENABLE_MSGPUMP
	g_loop->is_global = true;
#endif
	TlsSetValue(tls_id, g_loop);
}
ondeinit()
{
	delete &get_loop();
	TlsFree(tls_id);
}
runloop2_windows& get_loop()
{
	// TODO: creating a thread should also create a runloop
	return *(runloop2_windows*)TlsGetValue(tls_id);
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
		waiter_fn<void, void> wait;
		event.then(&wait);
		auto& loop = get_loop();
		while (wait.is_waiting())
			loop.step(true);
	}
	async<void> await_handle(HANDLE h) { return get_loop().handle_set(h); }
	async<void> await_timeout(timestamp timeout) { return get_loop().timer_set(timeout); }
#ifdef ARLIB_SOCKET
	void* get_dns() { return get_loop().dns; }
#endif
	co_waiter_void_multi co_wait_multi_inst;
}
#endif
