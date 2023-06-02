#include "inotify.h"

#ifdef __linux__
#include <sys/inotify.h>
#include <unistd.h>
#include "file.h"

inotify::inotify()
{
	m_fd = inotify_init1(IN_NONBLOCK|IN_CLOEXEC);
	m_last_dispatch = 0;
	runloop2::await_read(m_fd).then(&m_wait);
}

void inotify::add(cstring fn, function<void(cstring)> cb)
{
	ino_watch& wa = m_watches.append();
	wa.fn = fn;
	wa.cb = cb;
	wa.last_dispatch = m_last_dispatch;
	set_watch(wa);
}

void inotify::set_watch(ino_watch& wa)
{
	wa.wd = inotify_add_watch(m_fd, wa.fn, IN_MODIFY|IN_DELETE_SELF|IN_MOVE_SELF);
	if (wa.wd < 0)
	{
		string tmp = wa.fn;
		while (wa.wd < 0)
		{
			tmp = file::resolve(tmp+"/..");
			wa.wd = inotify_add_watch(m_fd, tmp, IN_DELETE_SELF|IN_MOVE_SELF|IN_CREATE|IN_MOVED_TO);
		}
	}
}

void inotify::dispatch_except(cstring fn)
{
	uint8_t buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t len = read(m_fd, buf, sizeof(buf));
	if (len < 0) abort(); // should be impossible
	
	m_last_dispatch++;
	
	if (fn)
	{
		for (ino_watch& wa : m_watches)
		{
			if (wa.fn == fn)
				wa.last_dispatch = m_last_dispatch;
		}
	}
	
	inotify_event* ev;
	for (uint8_t* iter = buf; iter < buf+len; iter += sizeof(inotify_event)+ev->len)
	{
		ev = (inotify_event*)iter;
		for (ino_watch& wa : m_watches)
		{
			if (ev->wd == wa.wd)
			{
				if (ev->mask & (IN_IGNORED|IN_DELETE_SELF|IN_MOVE_SELF|IN_CREATE|IN_MOVED_TO))
				{
					inotify_rm_watch(m_fd, wa.wd); // fails if there's an IN_IGNORED, but that's harmless, watch numbers aren't reused
					set_watch(wa);
				}
				if (wa.last_dispatch != m_last_dispatch)
					RETURN_IF_CALLBACK_DESTRUCTS(wa.cb(wa.fn));
				wa.last_dispatch = m_last_dispatch;
				break;
			}
		}
	}
}

void inotify::wait_done()
{
	dispatch_except("");
	runloop2::await_read(m_fd).then(&m_wait);
}

inotify::~inotify()
{
	close(m_fd);
}
#endif
