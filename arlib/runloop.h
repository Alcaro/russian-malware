#pragma once
#include "global.h"

//A runloop keeps track of a number of file descriptors, calling their handlers whenever the relevant operation is available.
//Event handlers must handle the event. If they don't, the handler may be called again forever and block everything else.
//Do not call enter() or step() while inside a callback. The others are safe.
//Like most other objects, a runloop is not thread safe.
class runloop : nocopy {
protected:
	runloop() {}
public:
	//The global runloop handles GUI events, in addition to whatever fds it's told to track. Always returns the same object.
	//The global runloop belongs to the main thread. Don't delete it, or call this function from any other thread.
	static runloop* global();
	
	//For non-primary threads. Using multiple runloops per thread is generally a bad idea.
	static runloop* create();
	
//#error rewrite the entire concept here; the loop should contain only some source objects,
//#error which loop calls update() on; they tell which events should wake them up, as well as processing whatever they wanted last time
//#error valid options: immediately, when time_us_ne is above the given value, when the given fd is readable, when fd is writable
//#error somewhat like gmainloop, except no repeatedly poking idle sources
//#error there will, of course, also be a function on the loop to tell it to update its records of some object,
//#error as well as adding/removing sources
//#error the current system causes way too many issues with the sockets violating the API
	
#ifndef _WIN32 // fd isn't a defined concept on windows
	//The callback argument is the fd, to allow one object to maintain multiple fds.
	//A fd can only be used once per runloop. If that fd is already there, it's removed prior to binding the new callbacks.
	//If the new callbacks are both NULL, it's removed. The return value can still safely be passed to remove().
	//If only one callback is provided, events of the other kind are ignored.
	//If both reading and writing is possible, only the read callback is called.
	//If the other side of the fd is closed, it's considered both readable and writable.
	virtual uintptr_t set_fd(uintptr_t fd, function<void(uintptr_t)> cb_read, function<void(uintptr_t)> cb_write = NULL) = 0;
#else
	//TODO: figure out sockets and gui events on windows (other fds aren't needed)
	//virtual uintptr_t set_socket(socket* sock, function<void()> cb_read, function<void()> cb_write) = 0;
#endif
	
private:
	virtual uintptr_t set_timer_rel(unsigned ms, bool repeat, function<void()> callback) = 0;
public:
	//Runs once. The return value can be used to remove() the callback before it arrives.
	uintptr_t set_timer_abs(time_t when, function<void()> callback);
	//Runs repeatedly. To stop, remove() it.
	//Accuracy is not guaranteed; it may or may not round the timer frequency to something it finds appropriate,
	// in either direction, and may or may not try to 'catch up' if a call is late (or early).
	//Don't use for anything that needs tighter timing than ±1 second.
	uintptr_t set_timer_loop(unsigned ms, function<void()> callback) { return set_timer_rel(ms, true, callback); }
	uintptr_t set_timer_once(unsigned ms, function<void()> callback) { return set_timer_rel(ms, false, callback); }
	
	//Will be called next time no other events (other than other idle callbacks) are ready, or earlier.
	// Runs once; if you want another, set a new one.
	virtual uintptr_t set_idle(function<void()> callback) { return set_timer_rel(0, false, callback); }
	
	//Deletes an existing timer and adds a new one.
	uintptr_t set_timer_abs(uintptr_t prev_id, time_t when, function<void()> callback)
	{
		if (prev_id) remove(prev_id);
		return set_timer_abs(when, callback);
	}
	uintptr_t set_timer_loop(uintptr_t prev_id, unsigned ms, function<void()> callback)
	{
		if (prev_id) remove(prev_id);
		return set_timer_loop(ms, callback);
	}
	uintptr_t set_timer_once(uintptr_t prev_id, unsigned ms, function<void()> callback)
	{
		if (prev_id) remove(prev_id);
		return set_timer_once(ms, callback);
	}
	uintptr_t set_idle(uintptr_t prev_id, function<void()> callback)
	{
		if (prev_id) remove(prev_id);
		return set_idle(callback);
	}
	
	//Return value from each set_*() is a token which can be used to cancel the event. remove(0) is guaranteed to be ignored.
	//Returns 0, so you can use 'id = loop->remove(id)'.
	virtual uintptr_t remove(uintptr_t id) = 0;
	
#ifdef ARLIB_THREAD
	//submit(fn) calls fn() on the runloop's thread, as soon as possible.
	//Unlike the other functions on this object, submit() may be called from a foreign thread.
	// It may also be called from signal handlers.
	//Make sure either the function has no destructor, or there is no other reference to cb;
	// otherwise, there will be a race condition on the reference count.
	virtual void submit(function<void()>&& cb) = 0;
	//prepare_submit() must be called on the owning thread before submit() is allowed.
	//There is no way to 'unprepare' submit(), other than deleting the runloop.
	//It is safe to call prepare_submit() multiple times, even concurrently with submit(),
	// as long as the first one has finished before the first submit() starts.
	virtual void prepare_submit() = 0;
#endif
	
	//TODO: Slots, objects where there's no reason to have more than one per runloop (like DNS),
	// so they can be shared among runloop users
	
	//Executes the mainloop until ->exit() is called. Recommended for most programs.
	virtual void enter() = 0;
	virtual void exit() = 0;
	
	//Runs until there are no more events to process, then returns. Usable if you require tighter control over the runloop.
	//If wait is true, waits for at least one event before returning.
	virtual void step(bool wait = false) = 0;
	
	//Delete everything using the runloop (sockets, HTTP or anything else using sockets, etc) before deleting the loop itself,
	// or said contents will use-after-free.
	// The runloop itself doesn't need to be empty (contents will be removed without being called), though
	//You can't remove GUI stuff from the global runloop, so you can't delete it.
	// Not even on process exit; cleanup on process exit is a waste of time, anyways.
	virtual ~runloop() = 0;
};
inline runloop::~runloop() {}

#ifdef ARLIB_TEST
runloop* runloop_wrap_blocktest(runloop* inner);
//used if multiple tests use the global runloop, the time spent elsewhere looks like huge runloop latency
void runloop_blocktest_recycle(runloop* loop);
#endif
