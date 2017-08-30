#include "runloop.h"

#ifdef __linux__
class runloop_linux : public runloop {

/*
	virtual void set_fd(uintptr_t fd, function<void(uintptr_t)> cb_read, function<void(uintptr_t)> cb_write = NULL);
	
	virtual void set_timer_oneshot(time_t when, function<void()> callback);
	virtual void set_timer_interval(unsigned ms, function<void()> callback);
	
	//Runs until ->exit() is called. Recommended for event-driven programs.
	virtual void enter() = 0; // { is_terminated = false; while (!is_terminated) step(true); }
	virtual void exit() = 0; // { is_terminated = true; }
	
	//Runs until there are no more events to process, then returns.
	//Use only if you've got your own synchronization mechanisms to ensure you're not wasting 100% CPU.
	virtual void step() = 0; // { step(false); }
	
	
*/

/*
fd_mon::fd_mon() { epoll_fd = epoll_create1(EPOLL_CLOEXEC); }

void fd_mon::monitor(int fd, void* key, bool read, bool write)
{
	epoll_event ev = {}; // shut up valgrind, I only need events and data.fd, the rest of data will just come back out unchanged
	ev.events = (read ? RD_EV : 0) | (write ? WR_EV : 0);
	ev.data.ptr = key;
	if (ev.events)
	{
		epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev); // one of these two will fail (or do nothing), we'll ignore that
		epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
	}
	else
	{
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
	}
}

void* fd_mon::select(bool* can_read, bool* can_write, int timeout_ms)
{
again: ;
	epoll_event ev;
	int nev = epoll_wait(epoll_fd, &ev, 1, timeout_ms);
	if (nev == 1)
	{
		if (can_read) *can_read = (ev.events & RD_EV);
		if (can_write) *can_write = (ev.events & WR_EV);
		return ev.data.ptr;
	}
	if (nev == 0)
	{
		if (can_read) *can_read = false;
		if (can_write) *can_write = false;
		return NULL;
	}
	goto again; // probably EINTR, the other errors won't happen
}

fd_mon::~fd_mon() { close(epoll_fd); }
*/

};

//runloop* runloop::create()
//{
//	return new runloop_linux();
//}
#endif

#ifdef ARGUI_NONE
//runloop* runloop::global()
//{
	//static runloop* ret = NULL;
	//if (!ret) ret = runloop::create();
	//return ret;
//}
#endif
