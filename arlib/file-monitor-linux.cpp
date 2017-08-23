#include "global.h"

#ifdef __linux__
#include "file.h"
#include "array.h"
#include "set.h"
#include "thread.h"

#include <sys/epoll.h>
#include <unistd.h>

#define RD_EV (EPOLLIN |EPOLLRDHUP|EPOLLHUP|EPOLLERR)
#define WR_EV (EPOLLOUT|EPOLLRDHUP|EPOLLHUP|EPOLLERR)

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


#ifdef ARLIB_THREAD
class fd_mon_thr : nocopy {
	map<int, function<void(int)>> read_act;
	map<int, function<void(int)>> write_act;
	
	bool initialized = false;
	fd_mon sub;
	mutex_rec mut;
	
	void process()
	{
		while (true)
		{
			bool read;
			bool write;
			uintptr_t fd = (uintptr_t)sub.select(&read, &write);
			
			synchronized(mut)
			{
				if (read)   read_act.get_or(fd, NULL)(fd);
				if (write) write_act.get_or(fd, NULL)(fd);
			}
		}
	}
	
	void monitor_raw(int fd, function<void(int)> on_read, function<void(int)> on_write)
	{
		if (on_read) read_act.insert(fd, on_read);
		else read_act.remove(fd);
		
		if (on_write) write_act.insert(fd, on_write);
		else write_act.remove(fd);
		
		sub.monitor(fd, (void*)(uintptr_t)fd, on_read, on_write);
	}
	
	void initialize()
	{
		if (initialized) return;
		initialized = true;
		thread_create(bind_this(&fd_mon_thr::process));
	}
	
public:
	void monitor(int fd, function<void(int)> on_read, function<void(int)> on_write)
	{
		synchronized(mut)
		{
			//at this point, process() is known to not be in a handler (unless it's recursive), or it would've held the lock
			initialize();
			
			monitor_raw(fd, on_read, on_write);
		}
	}
};

static fd_mon_thr g_fd_mon_thr;
void fd_mon_thread(int fd, function<void(int)> on_read, function<void(int)> on_write)
{
	g_fd_mon_thr.monitor(fd, on_read, on_write);
}
#endif
#endif
