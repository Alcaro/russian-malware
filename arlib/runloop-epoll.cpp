#ifdef __linux__
#include "runloop.h"
#include "set.h"
#include "test.h"
#include "process.h"
#include <time.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

// TODO: replace epoll with normal poll, epoll doesn't help at our small scale

namespace {
class runloop_linux : public runloop {
public:
	#define RD_EV (EPOLLIN |EPOLLRDHUP|EPOLLHUP|EPOLLERR)
	#define WR_EV (EPOLLOUT|EPOLLRDHUP|EPOLLHUP|EPOLLERR)
	
	int epoll_fd;
	bool exited;
	
	struct fd_cbs {
#ifdef ARLIB_TESTRUNNER
		char* valgrind_dummy; // an unused malloc(1), used as a stack trace that can be printed by double freeing
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
			cb.valgrind_dummy = xmalloc(1);
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
		timer.valgrind_dummy = xmalloc(1);
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
		
#ifndef ARLIB_OPT
		if (wait && fdinfo.size() == 0 && next == -1)
		{
#ifdef ARLIB_TESTRUNNER
			assert(!"runloop is empty and will block forever");
#else
			abort();
#endif
		}
#endif
		
		
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
		// this relies on function's ctor not allocating, and its dtor doing nothing if given an all-zero object
		memset((void*)&cb, 0, sizeof(cb));
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
	again:
		for (auto& pair : fdinfo)
		{
			int fd = pair.key;
#ifdef ARLIB_THREAD
			if (fd == submit_fds[0]) continue;
#endif
			if (fd != process::_sigchld_fd_runloop_only())
			{
				test_nothrow {
					if (RUNNING_ON_VALGRIND)
					{
						test_fail("fd left in runloop, check whoever allocated the following");
						free(pair.value.valgrind_dummy); // intentional double free, to make valgrind print a stack trace of the malloc
					}
					else
						printf("ERROR: fd %d left in runloop\n", pair.key);
				}
			}
			
			set_fd(fd, nullptr, nullptr);
			goto again;
		}
		while (timerinfo.size())
		{
			if (timerinfo[0].id != (unsigned)-1)
			{
				test_nothrow {
					if (RUNNING_ON_VALGRIND)
					{
						test_fail("timer left in runloop, check whoever allocated the following");
						free(timerinfo[0].valgrind_dummy); // intentional double free
					}
					else
						test_fail("timer left in runloop");
				}
			}
			free(timerinfo[0].valgrind_dummy);
			timerinfo.remove(0);
		}
	}
#endif
};
}

runloop* runloop::create()
{
	return runloop_wrap_blocktest(new runloop_linux());
}

#ifdef ARGUI_NONE
runloop* runloop::global()
{
	static runloop* ret = NULL;
	if (!ret) ret = runloop::create();
	return ret;
}
#endif
#endif
