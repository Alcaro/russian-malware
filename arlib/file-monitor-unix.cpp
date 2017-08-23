#include "file.h"

#if defined(__unix__)
#include <unistd.h>
#include <poll.h>
#define RD_EV (POLLIN |POLLRDHUP|POLLHUP|POLLERR)
#define WR_EV (POLLOUT|POLLRDHUP|POLLHUP|POLLERR)

int fd_monitor_oneshot(arrayview<int> fds, bool* can_read, bool* can_write, int timeout_ms)
{
	if (can_read) *can_read = false;
	if (can_write) *can_write = false;
	
	if (!fds.size())
	{
		//this is gonna screw up if timeout is infinite and there are no sockets
		//but in that case, you deserve a screwup
		usleep(timeout_ms*1000);
		return -1;
	}
	
	short events = (can_read ? POLLIN : 0) | (can_write ? POLLOUT : 0);
	
	array<pollfd> pfd;
	pfd.resize(fds.size());
	
	for (size_t i=0;i<fds.size();i++)
	{
		pfd[i].fd = fds[i];
		pfd[i].events = events;
	}
	
	poll(pfd.ptr(), pfd.size(), timeout_ms);
	
	for (size_t i=0;i<fds.size();i++)
	{
		if (pfd[i].revents & events)
		{
			if (can_read) *can_read = (pfd[i].revents & RD_EV);
			if (can_write) *can_write = (pfd[i].revents & WR_EV);
			return i;
		}
	}
	
	return -1;
}

/*
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

class fd_mon_t : nocopy {
	map<int, function<void(int)>> read_act;
	map<int, function<void(int)>> write_act;
	//TODO: epoll maybe?
	
	int selfpipe[2];
	
	mutex_rec mut;
	bool initialized = false;
	
	void process()
	{
		while (true)
		{
			fd_set readfds;
			fd_set writefds;
			FD_ZERO(&readfds);
			FD_ZERO(&writefds);
			int maxfd = 0;
			
			synchronized(mut)
			{
				for (auto& n : read_act)
				{
					FD_SET(n.key, &readfds);
					if (n.key > maxfd) maxfd = n.key;
				}
				for (auto& n : write_act)
				{
					FD_SET(n.key, &writefds);
					if (n.key > maxfd) maxfd = n.key;
				}
			}
			
			select(maxfd+1, &readfds, &writefds, NULL, NULL);
			
			synchronized(mut)
			{
				//see safety guarantees in set.h for what happens on concurrent modification
				//skipping is fine, they show up on next select()
				//dupes are fine, the handlers know to O_NONBLOCK
				for (auto& n : read_act)
				{
					if (FD_ISSET(n.key, &readfds)) n.value(n.key);
				}
				for (auto& n : write_act)
				{
					if (FD_ISSET(n.key, &writefds)) n.value(n.key);
				}
			}
		}
	}
	
	void monitor_raw(int fd, function<void(int)> on_read, function<void(int)> on_write)
	{
		if (on_read) read_act.insert(fd, on_read);
		else read_act.remove(fd);
		
		if (on_write) write_act.insert(fd, on_write);
		else write_act.remove(fd);
	}
	
	void initialize()
	{
		if (initialized) return;
		initialized = true;
		
		if (pipe2(selfpipe, O_NONBLOCK|O_CLOEXEC) < 0) abort();
		//if (pipe(selfpipe) < 0) abort();
		//fcntl(selfpipe[0], F_SETFL, fcntl(selfpipe[0], F_GETFL) | O_NONBLOCK);
		//fcntl(selfpipe[1], F_SETFL, fcntl(selfpipe[1], F_GETFL) | O_NONBLOCK);
		
		function<void(int)> discard = [](int fd){ char x[16]; x[0] = read(fd, &x, 16); };
		monitor_raw(selfpipe[0], discard, NULL);
		
		thread_create(bind_this(&fd_mon_t::process));
	}
	
public:
	void monitor(int fd, function<void(int)> on_read, function<void(int)> on_write)
	{
		synchronized(mut)
		{
			//at this point, process() is known to not be in a handler (unless it's recursive), or it would've held the lock
			initialize();
			
			monitor_raw(fd, on_read, on_write);
			//wake up process(), so it adds the new fd
			//discard return value, failure probably means thread is busy and pipe is full
			if (write(selfpipe[1], "", 1) < 0) {}
		}
	}
};

//there's no need for more than one of these
static fd_mon_t fd_mon;
void fd_monitor(int fd, function<void(int)> on_read, function<void(int)> on_write)
{
	fd_mon.monitor(fd, on_read, on_write);
}
*/
#endif
