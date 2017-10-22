#pragma once
#include "global.h"

//A runloop keeps track of a number of file descriptors, calling their handlers whenever the relevant operation is available.
//There are no fairness guarantees. If an event doesn't terminate or unset itself properly, it may inhibit other fds.
//Do not call enter() or step() while inside a callback. However, set_*(), remove() and exit() are fine.
class runloop {
protected:
	runloop() {}
public:
	//The global runloop handles GUI events, in addition to whatever fds it's told to track. Always returns the same object.
	//Don't call from anything other than the main thread.
	static runloop* global();
	
	//Using multiple runloops per thread is generally a bad idea.
	static runloop* create();
	
#ifndef _WIN32 // fd isn't a defined concept on windows
	//Callback argument is the fd, in case one object maintains multiple fds.
	//A fd can only be used once per runloop. If that fd is already there, it's removed prior to binding the new callbacks.
	//If the new callbacks are both NULL, it's removed. The return value can still safely be passed to remove().
	//If only one callback is provided, events of the other kind are ignored.
	//If both reading and writing is possible, only the read callback is called.
	//If the other side of the fd is closed, it's considered both readable and writable.
	virtual uintptr_t set_fd(uintptr_t fd, function<void(uintptr_t)> cb_read, function<void(uintptr_t)> cb_write = NULL) = 0;
#else
	//TODO: figure out sockets on windows (other fds aren't needed)
	//virtual uintptr_t set_socket(socket* sock, function<void()> cb_read, function<void()> cb_write) = 0;
#endif
	
	//Runs once.
	uintptr_t set_timer_abs(time_t when, function<void()> callback);
	//Runs repeatedly. To stop it, remove() it, or return false from the callback. Don't do both.
	//Accuracy is not guaranteed; it may or may not round the timer frequency to something it finds appropriate,
	// in either direction, and may or may not try to 'catch up' if a call is late (or early).
	//Don't use for anything that needs tighter timing than ±1 second.
	virtual uintptr_t set_timer_rel(unsigned ms, function<bool()> callback) = 0;
	
	//Will be called next time no other events (other than other idle callbacks) are ready, or earlier.
	//Like set_timer_rel, the return value tells whether to call it again later.
	virtual uintptr_t set_idle(function<bool()> callback) { return set_timer_rel(0, callback); }
	
	//Deletes an existing timer and adds a new one.
	uintptr_t set_timer_abs(uintptr_t prev_id, time_t when, function<void()> callback) { remove(prev_id); return set_timer_abs(when, callback); }
	uintptr_t set_timer_rel(uintptr_t prev_id, unsigned ms, function<bool()> callback) { remove(prev_id); return set_timer_rel(ms, callback); }
	uintptr_t set_idle(uintptr_t prev_id, function<bool()> callback) { remove(prev_id); return set_idle(callback); }
	
	//Return value from each set_*() is a token which can be used to cancel the event. remove(0) is guaranteed to be ignored.
	virtual void remove(uintptr_t id) = 0;
	
	//Executes the mainloop until ->exit() is called. Recommended for most programs.
	virtual void enter() = 0;
	virtual void exit() = 0;
	
	//Runs until there are no more events to process, then returns. Usable if you require control over the runloop.
	virtual void step() = 0;
	
	//Deleting a non-global runloop is fine, but leave the global one alone.
	//Don't delete a non-empty runloop, the contents will use-after-free.
	virtual ~runloop() {}
};
