#pragma once
#include "global.h"

//A runloop keeps track of a number of file descriptors, calling their handlers whenever the relevant operation is available.
//There are no fairness guarantees. If a fd doesn't properly read its data, it may inhibit other fds.
//Do not call enter() or step() while inside a callback. However, set_* and exit() are fine.
class runloop : nomove { // Objects are expected to keep pointers to their runloop, so no moving.
protected:
	runloop() {}
public:
	static runloop* global(); // The global runloop handles GUI events, in addition to whatever fds it's told to track.
	static runloop* create(); // For best results, only use this one on secondary threads, and only one per thread.
	
	//Callback argument is the fd, in case one object maintains multiple fds. To remove, set both callbacks to NULL.
	//A fd can only be used once per runloop.
	virtual void set_fd(uintptr_t fd, function<void(uintptr_t)> cb_read, function<void(uintptr_t)> cb_write = NULL) = 0;
	
	virtual void set_timer_oneshot(time_t when, function<void()> callback) = 0;
	virtual void set_timer_interval(unsigned ms, function<void()> callback) = 0;
	
	//Runs until ->exit() is called. Recommended for most programs.
	virtual void enter() = 0;
	virtual void exit() = 0;
	
	//Runs until there are no more events to process, then returns. Recommended for high-performance programs like games. Call it frequently.
	virtual void step() = 0;
	
	virtual ~runloop() {}
};
