#include "runloop.h"
#include "set.h"
#ifdef ARLIB_TESTRUNNER
#include "process.h"
#endif
#include <time.h>
#include "test.h" // for the runloop-is-empty check

#ifdef __linux__
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

namespace {
class runloop_linux : public runloop {
public:
	#define RD_EV (EPOLLIN |EPOLLRDHUP|EPOLLHUP|EPOLLERR)
	#define WR_EV (EPOLLOUT|EPOLLRDHUP|EPOLLHUP|EPOLLERR)
	
	int epoll_fd;
	bool exited = false;
	
	struct fd_cbs {
#ifdef ARLIB_TESTRUNNER
		char* valgrind_dummy; // an uninitialized malloc(1), used to print stack trace of the guilty allocation
#endif
		function<void(uintptr_t)> cb_read;
		function<void(uintptr_t)> cb_write;
	};
	map<int,fd_cbs> fdinfo;
	
	struct timer_cb {
#ifdef ARLIB_TESTRUNNER
		char* valgrind_dummy;
#endif
		unsigned id; // -1 if marked for removal
		unsigned ms;
		bool repeat;
		bool finished = false;
		struct timespec next;
		function<void()> cb;
	};
	//TODO: this should probably be a priority queue instead
	array<timer_cb> timerinfo;
	
#ifdef ARLIB_THREAD
	int submit_fds[2] = { -1, -1 };
#endif
	
	
	/*private*/ static void timespec_now(struct timespec * ts)
	{
		clock_gettime(CLOCK_MONOTONIC, ts);
	}
	
	/*private*/ static void timespec_add(struct timespec * ts, unsigned ms)
	{
		ts->tv_sec += ms/1000;
		ts->tv_nsec += (ms%1000)*1000000;
		if (ts->tv_nsec > 1000000000)
		{
			ts->tv_sec++;
			ts->tv_nsec -= 1000000000;
		}
	}
	
	//returns milliseconds
	/*private*/ static int64_t timespec_sub(struct timespec * ts1, struct timespec * ts2)
	{
		int64_t ret = (ts1->tv_sec - ts2->tv_sec) * 1000;
		ret += (ts1->tv_nsec - ts2->tv_nsec) / 1000000;
		return ret;
	}
	
	/*private*/ static bool timespec_less(struct timespec * ts1, struct timespec * ts2)
	{
		if (ts1->tv_sec < ts2->tv_sec) return true;
		if (ts1->tv_sec > ts2->tv_sec) return false;
		return (ts1->tv_nsec < ts2->tv_nsec);
	}
	
	
	
	runloop_linux()
	{
		epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	}
	
	void set_fd(uintptr_t fd, function<void(uintptr_t)> cb_read, function<void(uintptr_t)> cb_write) override
	{
		fd_cbs& cb = fdinfo.get_create(fd);
#ifdef ARLIB_TESTRUNNER
		if (!cb.valgrind_dummy)
			cb.valgrind_dummy = malloc(1);
#endif
		cb.cb_read  = cb_read;
		cb.cb_write = cb_write;
		
		epoll_event ev = {}; // shut up valgrind, I only need events and data.fd, the rest of data will just come back out unchanged
		ev.events = (cb_read ? RD_EV : 0) | (cb_write ? WR_EV : 0);
		ev.data.fd = fd;
		if (ev.events)
		{
			epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev); // one of these two will fail (or do nothing), we'll ignore that
			epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
		}
		else
		{
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
#ifdef ARLIB_TESTRUNNER
			free(cb.valgrind_dummy);
#endif
			fdinfo.remove(fd);
		}
	}
	
	
	uintptr_t raw_set_timer_rel(unsigned ms, bool repeat, function<void()> callback) override
	{
		unsigned timer_id = 1;
		for (size_t i=0;i<timerinfo.size();i++)
		{
			if (timerinfo[i].id == (unsigned)-1) continue;
			if (timerinfo[i].id >= timer_id)
			{
				timer_id = timerinfo[i].id+1;
			}
		}
		
		timer_cb& timer = timerinfo.append();
#ifdef ARLIB_TESTRUNNER
		timer.valgrind_dummy = malloc(1);
#endif
		timer.repeat = repeat;
		timer.id = timer_id;
		timer.ms = ms;
		timer.cb = callback;
		timespec_now(&timer.next);
		timespec_add(&timer.next, ms);
		return timer_id;
	}
	
	
	void raw_timer_remove(uintptr_t id) override
	{
		if (id == 0) return;
		
		for (size_t i=0;i<timerinfo.size();i++)
		{
			if (timerinfo[i].id == id)
			{
				timerinfo[i].id = -1;
				return;
			}
		}
		abort(); // happens if that timer doesn't exist
	}
	
	
	void step(bool wait) override
	{
		struct timespec now;
		timespec_now(&now);
//printf("runloop: time is %lu.%09lu\n", now.tv_sec, now.tv_nsec);
		
		int next = INT_MAX;
		
		for (size_t i=0;i<timerinfo.size();i++)
		{
		again: ;
			timer_cb& timer = timerinfo[i];
//printf("runloop: scheduled event at %lu.%09lu\n", timer.next.tv_sec, timer.next.tv_nsec);
			
			if (timer.id == (unsigned)-1)
			{
#ifdef ARLIB_TESTRUNNER
				free(timer.valgrind_dummy);
#endif
				timerinfo.remove(i);
				
				//funny code to avoid (size_t)-1 being greater than timerinfo.size()
				if (i == timerinfo.size()) break;
				goto again;
			}
			
			if (timer.finished) continue;
			
			int next_ms = timespec_sub(&timer.next, &now);
			if (next_ms <= 0)
			{
//printf("runloop: calling event scheduled %dms ago\n", -next_ms);
				timer.next = now;
				timespec_add(&timer.next, timer.ms);
				next_ms = timer.ms;
				
				timer.cb(); // WARNING: May invalidate 'timer'. timerinfo[i] remains valid.
				next = 0; // ensure it returns quickly, since it did something
				if (!timerinfo[i].repeat) timerinfo[i].finished = true;
			}
			
			if (next_ms < next) next = next_ms;
		}
		
		if (next == INT_MAX) next = -1;
		if (!wait) next = 0;
		
		if (wait && fdinfo.size() == 0 && next == -1)
		{
#ifdef ARLIB_TESTRUNNER
			assert(!"runloop is empty and will block forever");
#else
			abort();
#endif
		}
		
		
		epoll_event ev[16];
//printf("runloop: waiting %d ms\n", next);
		int nev = epoll_wait(epoll_fd, ev, 16, next);
		for (int i=0;i<nev;i++)
		{
			fd_cbs& cbs = fdinfo[ev[i].data.fd];
			     if ((ev[i].events & RD_EV) && cbs.cb_read)  cbs.cb_read( ev[i].data.fd);
			else if ((ev[i].events & WR_EV) && cbs.cb_write) cbs.cb_write(ev[i].data.fd);
		}
	}
	
#ifdef ARLIB_THREAD
	void prepare_submit() override
	{
		if (submit_fds[0] >= 0) return;
		if (pipe2(submit_fds, O_CLOEXEC) < 0) abort();
		this->set_fd(submit_fds[0], bind_this(&runloop_linux::submit_cb), NULL);
	}
	void submit(function<void()>&& cb) override
	{
		//full pipe should be impossible
		static_assert(sizeof(cb) <= PIPE_BUF);
		if (write(submit_fds[1], &cb, sizeof(cb)) != sizeof(cb)) abort();
		memset(&cb, 0, sizeof(cb));
	}
	/*private*/ void submit_cb(uintptr_t)
	{
		function<void()> cb;
		//we know the write pushed a complete one of those, we can assume we can read it out
		if (read(submit_fds[0], &cb, sizeof(cb)) != sizeof(cb)) abort();
		cb();
	}
#endif
	
	void enter() override
	{
		exited = false;
		while (!exited) step(true);
	}
	
	void exit() override
	{
		exited = true;
	}
	
	~runloop_linux()
	{
#ifdef ARLIB_THREAD
		if (submit_fds[0] >= 0)
		{
			this->set_fd(submit_fds[0], NULL, NULL);
			close(submit_fds[0]);
			close(submit_fds[1]);
		}
#endif
#ifdef ARLIB_TESTRUNNER
		assert_empty();
#endif
		close(epoll_fd);
	}
	
#ifdef ARLIB_TESTRUNNER
	void assert_empty() override
	{
		bool is_empty = true;
	again:
		for (auto& pair : fdinfo)
		{
			int fd = pair.key;
#ifdef ARLIB_THREAD
			if (fd == submit_fds[0]) continue;
#endif
			if (fd != process::_sigchld_fd_runloop_only())
			{
				if (RUNNING_ON_VALGRIND)
				{
					test_nothrow {
						test_fail("fd left in runloop, check whoever allocated the following");
					}
					free(pair.value.valgrind_dummy); // intentional double free, to make valgrind print a stack trace of the malloc
				}
				else
					printf("ERROR: fd %d left in runloop\n", pair.key);
				is_empty = false;
			}
			set_fd(fd, nullptr, nullptr);
			goto again;
		}
		while (timerinfo.size())
		{
			if (timerinfo[0].id != (unsigned)-1)
			{
				if (RUNNING_ON_VALGRIND)
					free(timerinfo[0].valgrind_dummy); // intentional double free
				else
					printf("ERROR: timer left in runloop\n");
			}
			free(timerinfo[0].valgrind_dummy);
			timerinfo.remove(0);
		}
		assert(is_empty);
	}
#endif
};
}

runloop* runloop::create()
{
	return runloop_wrap_blocktest(new runloop_linux());
}
#endif



uintptr_t runloop::raw_set_timer_abs(time_t when, function<void()> callback)
{
	time_t now = time(NULL);
	unsigned ms = (now < when ? (when-now)*1000 : 0);
	return raw_set_timer_rel(ms, false, callback);
}

#ifdef ARGUI_NONE
runloop* runloop::global()
{
	//ignore thread safety, this function can only be used from main thread
	static runloop* ret = NULL;
	if (!ret) ret = runloop_wrap_blocktest(runloop::create());
	return ret;
}
#endif

#if defined(_WIN32) && defined(ARLIB_TESTRUNNER)
runloop* runloop::create()
{
	test_expfail("unimplemented");
	abort();
}
#endif

#include "os.h"
#include "thread.h"
#include "socket.h"
#include "test.h"

#ifdef ARLIB_TESTRUNNER
class runloop_blocktest : public runloop {
	runloop* loop;
	bool can_exit = false;
	
	uint64_t us = 0;
	uint64_t loopdetect = 0;
	/*private*/ void begin()
	{
		uint64_t new_us = time_us_ne();
		if (new_us/1000000/10 != us/1000000/10) loopdetect = 0;
		loopdetect++;
		if (loopdetect == 10000) assert(!"10000 runloop iterations in 10 seconds");
		us = new_us;
	}
	/*private*/ void end()
	{
		uint64_t new_us = time_us_ne();
		uint64_t diff = new_us-us;
		_test_runloop_latency(diff);
	}
	
	//don't carelessly inline into the lambdas; sometimes lambdas are deallocated by the callbacks, so 'this' is a use-after-free
	/*private*/ void do_cb(function<void(uintptr_t)> cb, uintptr_t arg)
	{
		this->begin();
		cb(arg);
		this->end();
	}
	/*private*/ void do_cb(function<void()> cb)
	{
		this->begin();
		cb();
		this->end();
	}
	
#ifndef _WIN32
	void set_fd(uintptr_t fd, function<void(uintptr_t)> cb_read, function<void(uintptr_t)> cb_write) override
	{
		function<void(uintptr_t)> cb_read_w;
		function<void(uintptr_t)> cb_write_w;
		if (cb_read)  cb_read_w  = bind_lambda([this, cb_read ](uintptr_t fd){ this->do_cb(cb_read,  fd); });
		if (cb_write) cb_write_w = bind_lambda([this, cb_write](uintptr_t fd){ this->do_cb(cb_write, fd); });
		loop->set_fd(fd, std::move(cb_read_w), std::move(cb_write_w));
	}
#endif
	
	uintptr_t raw_set_timer_rel(unsigned ms, bool repeat, function<void()> callback) override
	{
		function<void()> callback_w = bind_lambda([](){});
		if (callback) callback_w = bind_lambda([this, callback]() { this->do_cb(callback); });
		if (repeat) return loop->raw_set_timer_repeat(ms, callback_w);
		else return loop->raw_set_timer_once(ms, callback_w);
	}
	uintptr_t raw_set_idle(function<void()> callback) override
	{
		function<void()> callback_w = bind_lambda([](){});
		if (callback) callback_w = bind_lambda([this, callback]() { this->do_cb(callback); });
		return loop->raw_set_idle(callback_w);
	}
	void raw_timer_remove(uintptr_t id) override { loop->raw_timer_remove(id); }
	
#ifdef ARLIB_THREAD
	void prepare_submit() override { loop->prepare_submit(); }
	void submit(function<void()>&& cb) override { loop->submit(std::move(cb)); }
#endif
	
	void enter() override { can_exit = true; end(); loop->enter(); begin(); can_exit = false; }
	void exit() override { assert(can_exit); loop->exit(); }
	void step(bool wait) override { end(); loop->step(wait); begin(); }
	
public:
	runloop_blocktest(runloop* inner) : loop(inner)
	{
		begin();
	}
	~runloop_blocktest()
	{
		end();
		delete loop;
	}
	
	void recycle()
	{
		us = time_us_ne();
		loopdetect = 0;
	}
	
	void assert_empty() override
	{
		loop->assert_empty();
	}
};

runloop* runloop_wrap_blocktest(runloop* inner)
{
	return new runloop_blocktest(inner);
}

void runloop_blocktest_recycle(runloop* loop)
{
	((runloop_blocktest*)loop)->recycle();
}
#endif

#ifdef ARLIB_TEST
static void test_runloop(bool is_global)
{
#ifdef _WIN32
	test_expfail("unimplemented");
#else
	runloop* loop = (is_global ? runloop::global() : runloop::create());
	
	//must be before the other one, loop->enter() must be called to ensure it doesn't actually run
	loop->raw_timer_remove(loop->raw_set_timer_once(50, bind_lambda([]() { assert_unreachable(); })));
	
	//don't put this scoped, id is used later
	uintptr_t id = loop->raw_set_timer_repeat(20, bind_lambda([&]()
		{
			assert_ne(id, 0);
			uintptr_t id_copy = id; // the 'id' reference gets freed by loop->remove(), reset it before that and keep a copy
			id = 0;
			loop->raw_timer_remove(id_copy);
		}));
	assert_ne(id, 0); // no thinking -1 is the highest ID so 0 should be used
	
	{
		int64_t start = time_ms_ne();
		int64_t end = start;
		uint64_t id2 = loop->raw_set_timer_once(100, bind_lambda([&]() { end = time_ms_ne(); loop->exit(); }));
		loop->enter();
		
		assert_range(end-start, 75,200);
		loop->raw_timer_remove(id2);
	}
	
	assert_eq(id, 0);
	
#ifdef ARLIB_THREAD
	{
		loop->prepare_submit();
		uint64_t start_ms = time_ms_ne();
		function<void()> threadproc = bind_lambda([loop, start_ms]()
			{
				while (start_ms+100 > time_ms_ne()) usleep(1000);
				loop->submit(bind_lambda([loop]()
					{
						loop->exit();
					}));
			});
		thread_create(std::move(threadproc));
		loop->enter();
		uint64_t end_ms = time_ms_ne();
		assert_range(end_ms-start_ms, 75,200);
	}
#endif
	
	//I could stick in some fd tests, but the sockets test all plausible operations anyways.
	//Okay, they should vary which runloop they use.
	
	if (!is_global) delete loop;
#endif
}
test("global runloop", "function,array,set,time", "runloop")
{
	test_runloop(true);
}
class loop_tester {
public:
	autoptr<runloop> loop;
	DECL_TIMER(t1, loop_tester);
	DECL_TIMER(t2, loop_tester);
};
class loop_tester2 {
public:
	runloop* p_loop;
	runloop* loop() { return p_loop; }
	DECL_TIMER(t3, loop_tester2);
	DECL_TIMER(t4, loop_tester2);
};
test("private runloop", "function,array,set,time", "runloop")
{
	test_runloop(false);
	
	loop_tester holder;
	loop_tester2 holder2;
	holder.loop = runloop::create();
	holder2.p_loop = holder.loop;
	
	int n = 0;
	holder.t1.set_repeat(20, [&](){ n++; });
	holder.t2.set_once(50, [&](){ holder.loop->exit(); });
	holder2.t3.set_repeat(10, [&](){ n+=10; holder2.t3.reset(); });
	holder2.t4.set_repeat(30, [&](){ n+=100; });
	holder.loop->enter();
	assert_eq(n, 112);
}
test("epoll","","") { test_expfail("replace epoll with normal poll, epoll doesn't help at our small scale"); }

#ifdef ARLIB_SOCKET
static void test_runloop_2(bool is_global)
{
#ifdef _WIN32
	test_expfail("unimplemented");
#else
	runloop* loop = (is_global ? runloop::global() : runloop::create());
	
	socket* sock[2];
	uint8_t buf[2][8];
	size_t n_buf[2] = {0,0};
	for (int i=0;i<2;i++)
	{
		sock[i] = socket::create("muncher.se", 80, loop);
		sock[i]->callback([&, i]()
			{
				if (n_buf[i] < 8)
				{
					int bytes = sock[i]->recv(arrayvieww<byte>(buf[i]).skip(n_buf[i]));
					n_buf[i] += bytes;
				}
				if (n_buf[i] >= 8)
				{
					uint8_t discard[4096];
					sock[i]->recv(discard);
				}
				if (n_buf[0] == 8 && n_buf[1] == 8) loop->exit();
			});
		sock[i]->send(cstring("GET / HTTP/1.1\r\n").bytes());
	}
	uintptr_t t = loop->raw_set_timer_once(500, [&](){ loop->exit(); });
	loop->enter();
	loop->raw_timer_remove(t);
	
	for (int i=0;i<2;i++)
	{
		assert_eq(n_buf[i], 0);
		sock[i]->send(cstring("Host: muncher.se\r\nConnection: close\r\n\r\n").bytes());
	}
	uintptr_t t2 = loop->raw_set_timer_once(100, [&](){ loop->raw_timer_remove(t2); t2 = 0; });
	usleep(200*1000); // ensure everything is ready simultaneously
	runloop_blocktest_recycle(loop);
	t = loop->raw_set_timer_once(1000, [&](){ assert_unreachable(); });
	loop->enter();
	loop->raw_timer_remove(t);
	
	assert_eq(t2, 0);
	assert_eq(cstring(arrayview<byte>(buf[0])), "HTTP/1.1");
	assert_eq(cstring(arrayview<byte>(buf[1])), "HTTP/1.1");
	
	delete sock[0];
	delete sock[1];
	
	if (!is_global) delete loop;
#endif
}
test("runloop with sockets", "function,array,set,time", "runloop")
{
	test_skip("too slow");
	testcall(test_runloop_2(false));
	testcall(test_runloop_2(true));
}
#endif
#endif
