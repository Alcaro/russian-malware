#pragma once
#include "string.h"
#include "runloop2.h"

#ifdef __linux__
// Tells you when the given file may have changed, as in file::readall() would return something different.
// Refers to the filename only. May return false positives if, for example, the written bytes are the ones that were already there.
class inotify {
	int m_fd;
	uint32_t m_last_dispatch;
	
	struct ino_watch {
		string fn;
		int wd;
		uint32_t last_dispatch;
		function<void(cstring)> cb;
	};
	array<ino_watch> m_watches;
	
	waiter<void> m_wait = make_waiter<&inotify::m_wait, &inotify::wait_done>();
	
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	
public:
	inotify();
	void add(cstring fn, function<void(cstring)> cb);
	
	// Calls the given callbacks, except the named file.
	// Intended to be called after this program has written to that file, so the program won't see its own changes.
	// If this isn't called, it's processed in the runloop.
	void dispatch_except(cstring fn);
private:
	void set_watch(ino_watch& wa);
	void wait_done();
public:
	~inotify();
};
#endif
