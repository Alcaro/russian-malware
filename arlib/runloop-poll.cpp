#if defined(__unix__) && (defined(ARLIB_GUI_GTK3) || !__has_include(<glib.h>))
// if GLib is included but ARGUI is disabled, using this thing is unwise; block it
#include "runloop2.h"
#include "array.h"
#include <sys/poll.h>
#ifdef ARLIB_SOCKET
#include "socket.h"
#endif

#ifdef ARLIB_GUI_GTK3
#include <gtk/gtk.h>
#define EXTRA_POLLFDS
#endif

namespace {

class runloop2_poll;
runloop2_poll& get_loop();

class runloop2_poll {
public:
#if !defined(ARLIB_GUI)
	static constexpr bool has_gui_events = false;
#elif !defined(ARLIB_THREAD)
	static constexpr bool has_gui_events = true;
#else
	// TODO: figure out what to do here
	//bool has_gui_events = false;
#endif
	
#ifdef ARLIB_TESTRUNNER
	timestamp last_iter;
	bool test_has_runloop;
#endif
	
	class fds_t {
		struct waiter_node {
			producer<void> prod = make_producer<&waiter_node::prod, &waiter_node::cancel>();
			void cancel() { get_loop().fds.cancel(this); }
		};
		array<waiter_node> waiting; // a merged array would be cleaner, but two separate means I can pass wait_fd directly to kernel
	public:
		array<struct pollfd> wait_fd;
#ifdef EXTRA_POLLFDS
		size_t n_wait_fds() { return max_used; }
#else
		size_t n_wait_fds() { return waiting.size(); }
#endif
	private:
		// linked list of unused nodes, for O(1) alloc-free await_fd()
		// last element in the list (or this list head) has next == waiting.size()
		// the actual link is ~wait_fd[n].fd
		size_t freelist;
#ifdef EXTRA_POLLFDS
		size_t max_used = 0;
#endif
		
		size_t idx_for(waiter_node* n)
		{
			return n - waiting.ptr();
		}
		
		void cancel(waiter_node* n)
		{
			size_t idx = idx_for(n);
			wait_fd[idx].fd = ~freelist;
			wait_fd[idx].revents = 0; // zero this one, in case poll() claimed it's active but it's not dispatched yet
			freelist = idx;
		}
		
		void reallocate(size_t newsize)
		{
#ifdef EXTRA_POLLFDS
			size_t cursize = max_used;
#else
			size_t cursize = waiting.size();
#endif
			waiting.resize(newsize);
			wait_fd.resize(newsize);
			for (size_t i=0;i<cursize;i++)
				waiting[i].prod.moved();
#ifndef EXTRA_POLLFDS
			for (size_t i=cursize;i<newsize;i++)
				wait_fd[i].fd = ~(i+1);
#endif
		}
		
	public:
		void reserve_space(size_t n)
		{
#ifdef EXTRA_POLLFDS
			if (freelist == max_used)
			{
				wait_fd[freelist].fd = ~(max_used+1);
				max_used++;
			}
			if (max_used+n > waiting.size())
			{
				size_t newsize = waiting.size();
				while (max_used+n > newsize)
					newsize *= 2;
				reallocate(newsize);
			}
#else
			if (freelist == waiting.size())
				reallocate(waiting.size()*2);
#endif
		}
		
		fds_t()
		{
			reallocate(8);
			freelist = 0;
		}
		
		async<void> await_fd(int fd, bool want_write)
		{
			reserve_space(1);
			
			static short rd_ev = (POLLIN |POLLHUP|POLLERR|POLLNVAL); // kernel always returns HUP/ERR/NVAL, whether requested or not
			static short wr_ev = (POLLOUT|POLLHUP|POLLERR|POLLNVAL); // let's set them anyways, it looks cleaner
			waiter_node& ch = waiting[freelist];
			struct pollfd& pfd = wait_fd[freelist];
			freelist = ~pfd.fd;
			pfd = { fd, (want_write ? wr_ev : rd_ev), 0 };
			return &ch.prod;
		}
		
		void activate(size_t idx)
		{
			wait_fd[idx].fd = ~freelist;
			freelist = idx;
			waiting[idx].prod.complete();
		}
		
#ifdef EXTRA_POLLFDS
		size_t n_extra_pollfd() { return wait_fd.size() - max_used; }
		struct pollfd * extra_pollfd() { return wait_fd.ptr() + max_used; }
#endif
	};
	fds_t fds;
	
	
	class timeout_t {
		struct waiter_node {
			producer<void> prod = make_producer<&waiter_node::prod, &waiter_node::cancel>();
			void cancel() { get_loop().timeouts.cancel(this); }
			timestamp timeout;
		};
	public:
		allocatable_array<waiter_node, [](waiter_node* n) { return &n->timeout.sec; }, [](waiter_node* n) { n->prod.moved(); }> waiting;
	private:
		void cancel(waiter_node* n)
		{
			waiting.dealloc(n);
		}
		
	public:
		async<void> await_timeout(timestamp timeout)
		{
			waiter_node* n = waiting.alloc();
			n->timeout = timeout;
			return &n->prod;
		}
		
		void activate(waiter_node* n)
		{
			waiting.dealloc(n);
			n->prod.complete();
		}
	};
	timeout_t timeouts;
	
	
	// TODO: find a way to move this to a global array of 0ms timeouts
	// without getting into trouble regarding threading
	fifo<std::coroutine_handle<>> scheduled;
	
	
	void schedule(std::coroutine_handle<> coro)
	{
		scheduled.push(coro);
	}
	bool step(bool wait)
	{
	again:
		bool ret = false;
		
		timestamp timeout = timestamp::at_never();
		for (auto& node : timeouts.waiting)
		{
			if (node.prod.has_waiter() && node.timeout <= timeout)
				timeout = node.timeout;
		}
		
		timestamp now = timestamp::now();
		duration dur = timeout - now;
#ifdef ARLIB_GUI_GTK3
		GMainContext* gmain_ctx;
		size_t gmain_n_pollfd;
		gint gmain_priority;
		if (has_gui_events)
		{
			gmain_ctx = g_main_context_default(); // g_main_context_query needs this (_prepare doesn't, unclear if glib bug)
			g_main_context_acquire(gmain_ctx);
			g_main_context_prepare(gmain_ctx, &gmain_priority);
			
		query_again:
			int timeout_ms;
			// GPollFD docs say must be same as pollfd, but the structs are
			//  struct pollfd { int fd; short int events; short int revents; };
			//  struct _GPollFD { gint fd; gushort events; gushort revents; };
			// which are not compatible, the latter two fields change signedness
			// it works in practice, but this assert is unusable
			//static_assert(std::is_layout_compatible_v<struct pollfd, GPollFD>);
			gmain_n_pollfd = g_main_context_query(gmain_ctx, gmain_priority, &timeout_ms, (GPollFD*)fds.extra_pollfd(), fds.n_extra_pollfd());
			if (gmain_n_pollfd > fds.n_extra_pollfd())
			{
				fds.reserve_space(gmain_n_pollfd);
				goto query_again;
			}
			
			if (timeout_ms >= 0)
				dur = min(dur, duration::ms(timeout_ms));
		}
#endif
		if (dur.sec < 0 || !scheduled.empty() || !wait)
			dur = { 0, 0 };
		
		test_rethrow();
#ifdef ARLIB_TESTRUNNER
		test_has_runloop = true;
		test_iter_end();
#endif
		size_t n_fds_to_poll = fds.n_wait_fds();
#ifdef ARLIB_GUI_GTK3
		n_fds_to_poll += gmain_n_pollfd;
#endif
		ppoll(fds.wait_fd.ptr(), n_fds_to_poll, (struct timespec*)&dur, nullptr);
#ifdef ARLIB_TESTRUNNER
		test_iter_begin();
#endif
		
#ifdef ARLIB_GUI_GTK3
		if (has_gui_events)
		{
			ret |= g_main_context_check(gmain_ctx, gmain_priority, (GPollFD*)fds.extra_pollfd(), gmain_n_pollfd);
			g_main_context_dispatch(gmain_ctx);
			g_main_context_release(gmain_ctx);
		}
#endif
		
		now = timestamp::now();
		
		// the loop must be index-based, activating a coroutine can add more waiters which can reallocate
		// (and the fd object contains two different arrays to loop over)
		for (size_t n=0;n<fds.n_wait_fds();n++)
		{
			if (fds.wait_fd[n].revents != 0)
			{
				fds.activate(n);
				ret = true;
			}
		}
		for (auto& node : timeouts.waiting)
		{
			if (node.prod.has_waiter() && node.timeout <= now)
			{
				timeouts.activate(&node);
				ret = true;
			}
		}
		while (!scheduled.empty())
		{
			scheduled.pop().resume();
			ret = true;
		}
		test_rethrow();
		
		if (ret && has_gui_events)
		{
			// if there are many events of different priorities ready to run, g_main_context_prepare only processes those of highest priority
			// loop a few more times until glib is done with everything
			wait = false;
			goto again;
		}
		
		return ret;
	}
	
	async<void> await_fd(int fd, bool want_write)
	{
		return fds.await_fd(fd, want_write);
	}
	
	async<void> await_timeout(timestamp timeout)
	{
		return timeouts.await_timeout(timeout);
	}
	
#ifdef ARLIB_SOCKET
	void* dns = nullptr;
	runloop2_poll()
	{
#ifndef ARLIB_THREAD
		dns = socket2::dns_create();
#endif
	}
	~runloop2_poll()
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
	
	size_t n_global_waits;
	
	void test_begin()
	{
		test_has_runloop = false;
		test_iter_begin();
		
		n_global_waits = 0;
		for (size_t n=0;n<fds.n_wait_fds();n++)
		{
			if (fds.wait_fd[n].fd >= 0)
				n_global_waits++;
		}
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
		size_t n_wait_fds = 0;
		for (size_t n=0;n<fds.n_wait_fds();n++)
		{
			if (fds.wait_fd[n].fd >= 0)
				n_wait_fds++;
		}
		assert_eq(n_global_waits, n_wait_fds);
		for (auto& node : timeouts.waiting)
			assert(!node.prod.has_waiter());
		assert(scheduled.empty());
	}
#endif
};

#ifdef ARLIB_THREAD
static thread_local runloop2_poll* g_loop;
runloop2_poll& get_loop()
{
	runloop2_poll*& loop = g_loop;
	if (!loop)
		loop = new runloop2_poll();
	return *loop;
}

#ifdef ARLIB_GUI_GTK3
oninit()
{
	get_loop().has_gui_events = true;
}
#endif

#else
__attribute__((init_priority(101))) // objects without a constructor priority are constructed after ones with
static runloop2_poll g_loop;
runloop2_poll& get_loop() { return g_loop; }
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
	async<void> await_fd(int fd, bool want_write) { return get_loop().await_fd(fd, want_write); }
	async<void> await_timeout(timestamp timeout) { return get_loop().await_timeout(timeout); }
	async<void> in_ms(int ms) { return get_loop().await_timeout(timestamp::in_ms(ms)); }
#ifdef ARLIB_SOCKET
	void* get_dns() { return get_loop().get_dns(); }
#endif
#ifdef ARLIB_TESTRUNNER
	void test_begin() { get_loop().test_begin(); }
	void test_end() { get_loop().test_end(); }
#endif
	co_waiter_void_multi co_wait_multi_inst;
}
#endif
