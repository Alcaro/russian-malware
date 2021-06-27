#pragma once
#include "global.h"
#ifdef _WIN32
#include <windows.h>
#endif

// TODO: rewrite this thing
// - creating a socket should be async, this DNS-then-forward wrapper is silly (socks5 should be async too)
// - backpressure; socketbuf is flawed at best, socket::write must report completion
// - concurrency pushback; object X may only handle one concurrent event
//     for example a GUI program that sends HTTP requests; nested GUI event handling is impossible to reason about
//     another example: simply an echo server
//       read "hello ", async write "hello "
//       read "world\n", async write "world\n"
//     they must be in the right order, and a bounded number, so the next read must be delayed until the write is done
//   for extra fun:
//   - object X may handle multiple fds
//   - object X may call into object Y, which also handles events
//   - all relevant overhead must be optimized out as far as possible
//   - an async function must be cancellable, without leaking anything important
// proposed solution:
//   instead of adding sockets to a runloop, add read/write operations (callers will rarely or never read+write on same fd)
//   implemented using io_uring (Linux 5.1, march 2019)
//     usable extensions: IORING_OP_SENDMSG 5.3 sep 2019, IORING_OP_TIMEOUT 5.4 nov 2019, IORING_OP_ASYNC_CANCEL 5.5 january 2020,
//       IORING_OP_READ 5.6 march 2020 (does same thing as IORING_OP_READV, just easier to use)
//   for windows, alertable I/O / APC completion routines (ReadFileEx, WSARecv)
//     WSARecv is documented Vista+, but so is recv, so docs are untrustworthy - needs testing
//     if it is Vista+, sockets will stay with WFMO on XP (ReadFileEx is known good on XP)
//   need to research cancellation a bit more - I need a way to ensure kernel does not use the relevant buffer after I've deallocated it
//   it'll map less well to gtk/glib runloop, but that's glibs problem
// needs c++20 coroutines, available in gcc 11 and clang 9, and available to me in ubuntu 22.04; anything else would be lambda nesting hell
// I'll also put the runloop in a thread-local variable, passing it around everywhere is annoying
//   may make testing slightly harder, but no big deal

//A runloop keeps track of a number of file descriptors, calling their handlers whenever the relevant operation is available.
//Event handlers must handle the event. If they don't, the handler may be called again forever and block everything else.
//Do not call enter() or step() while inside a callback. The others are safe.
//A runloop is less thread safe than most objects. It may only ever be use from one thread, even if access is serialized.
//Anything placed inside a runloop (socket, process, ...) gains the same thread affinity.
class runloop : nocopy {
protected:
	runloop() {}
public:
	//The global runloop handles GUI events, in addition to whatever fds it's told to track.
	//The global runloop belongs to the main thread. Don't delete it, or call this function from any other thread.
	//This function always returns the same object. It may be called on non-main threads,
	// but only if prepare_submit() has already been called.
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
	
#ifdef __unix__
	//The callback argument is the fd, to allow one object to maintain multiple fds.
	//A fd can only be used once per runloop. If that fd is already there, it's removed prior to binding the new callbacks.
	//If only one callback is provided, events of the other kind are ignored.
	//If both reading and writing is possible, only the read callback is called.
	//If the other side of the fd is closed, it's considered both readable and writable.
	//To remove it, pass NULL for both callbacks.
	virtual void set_fd(uintptr_t fd, function<void(uintptr_t)> cb_read, function<void(uintptr_t)> cb_write = NULL) = 0;
#else
	//Accepts anything WaitForSingleObject does. It's caller's job to define sensible HANDLEs. Like on Linux, passing NULL removes it.
	virtual void set_object(HANDLE h, function<void(HANDLE)> cb) = 0;
#endif
	
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
	// as long as the first prepare_submit() has finished before the first submit() starts.
	virtual void prepare_submit() = 0;
#endif
	
	//TODO: Slots, objects where there's no reason to have more than one per runloop (like DNS),
	// so they can be shared among runloop users
	
	// Executes the runloop until ->exit() is called. Recommended for most programs.
	// The runloop may dispatch a few events after exit(), but will not go idle.
	virtual void enter() = 0;
	virtual void exit() = 0;
	
	//Runs until there are no more events to process, then returns. Usable if you require tighter control over the runloop.
	//If wait is true, waits for at least one event before returning.
	virtual void step(bool wait = false) = 0;
	
#ifdef ARLIB_TESTRUNNER
	virtual void assert_empty() = 0;
#endif
	
	//Delete everything using the runloop (sockets, HTTP or anything else using sockets, etc) before deleting the loop itself,
	// or said contents will use-after-free.
	//You can't remove GUI stuff from the global runloop, so you can't delete it.
	// Not even on process exit; cleanup on process exit is a waste of time, anyways.
	virtual ~runloop() {}
	
	
	template<typename Tchild>
	class base_timer : nocopy {
		uintptr_t id = 0;
		
		runloop* loop()
		{
			return ((Tchild*)this)->get_loop();
		}
		
	public:
		//These two run once each.
		void set_once(unsigned ms, function<void()> callback)
		{
			reset();
			id = loop()->raw_set_timer_once(ms, callback);
		}
		void set_abs(time_t when, function<void()> callback)
		{
			reset();
			id = loop()->raw_set_timer_abs(when, callback);
		}
		//This one runs repeatedly. To stop, remove() it.
		//Accuracy is not guaranteed; it may or may not round the timer frequency to something it finds appropriate,
		// in either direction, and may or may not try to 'catch up' if a call is late (or early).
		void set_repeat(unsigned ms, function<void()> callback)
		{
			reset();
			id = loop()->raw_set_timer_repeat(ms, callback);
		}
		
		//Will be called next time no other events (other than other idle callbacks) are ready, or earlier.
		// Runs once.
		void set_idle(function<void()> callback)
		{
			reset();
			id = loop()->raw_set_idle(callback);
		}
		
		void reset()
		{
			if (id) loop()->raw_timer_remove(id);
			id = 0;
		}
		
		~base_timer() { reset(); }
	};
	
	class g_timer : public base_timer<g_timer> {
	public:
		runloop* get_loop() { return runloop::global(); }
	};
	
//DECL_TIMER declares a base_timer object as a member. It must be placed in class scope.
// It must be given a name for the member variable, and the type of the containing object.
// The container must
// - have a member variable or zero-argument function named 'loop', which returns the relevant runloop
// - not destroy the runloop before the timers. Using a autoptr<runloop>, placed before every DECL_TIMER, is fine.
// - not use virtual inheritance. Single and multiple inheritance is fine, as are virtual functions.
// All public members of base_timer are available on the resulting object.
//DECL_G_TIMER uses the global runloop.
#define DECL_TIMER(name, parent_t)                                                  \
	class JOIN(name, _t) : public runloop::base_timer<JOIN(name, _t)> {             \
		template<typename Tp>                                                       \
		typename std::enable_if_t<sizeof(&*std::declval<Tp>().loop) >= 0, runloop*> \
		get_loop_inner(Tp* parent)                                                  \
		{                                                                           \
			return parent->loop;                                                    \
		}                                                                           \
		template<typename Tp>                                                       \
		typename std::enable_if_t<sizeof(std::declval<Tp>().loop()) >= 0, runloop*> \
		get_loop_inner(Tp* parent)                                                  \
		{                                                                           \
			return parent->loop();                                                  \
		}                                                                           \
	public:                                                                         \
		runloop* get_loop() {                                                       \
			parent_t* parent_off = (parent_t*)nullptr;                              \
			JOIN(name,_t)* child_off = &parent_off->name;                           \
			size_t offset = (uint8_t*)child_off - (uint8_t*)parent_off;             \
			parent_t* parent = (parent_t*)((uint8_t*)this - offset);                \
			return get_loop_inner(parent);                                          \
		}                                                                           \
	} name; friend class JOIN(name, _t)
#define DECL_G_TIMER(name, parent_t) runloop::g_timer name
	
	//You probably don't want these. Use DECL_TIMER instead.
private:
	virtual uintptr_t raw_set_timer_rel(unsigned ms, bool repeat, function<void()> callback) = 0;
public:
	//Each timer must be removed once you're done. To do this, pass the function's return value to raw_timer_remove.
	//0 is guaranteed to never be returned, and to do nothing if passed to raw_timer_remove.
	uintptr_t raw_set_timer_abs(time_t when, function<void()> callback);
	uintptr_t raw_set_timer_repeat(unsigned ms, function<void()> callback) { return raw_set_timer_rel(ms, true, callback); }
	uintptr_t raw_set_timer_once(unsigned ms, function<void()> callback) { return raw_set_timer_rel(ms, false, callback); }
	virtual uintptr_t raw_set_idle(function<void()> callback) { return raw_set_timer_rel(0, false, callback); }
	
	virtual void raw_timer_remove(uintptr_t id) = 0;
};

#ifdef ARLIB_TESTRUNNER
runloop* runloop_wrap_blocktest(runloop* inner);
//used if multiple tests use the global runloop, the time spent elsewhere looks like huge runloop latency
void runloop_blocktest_recycle(runloop* loop);
#else
#define runloop_wrap_blocktest(x) x
#define runloop_blocktest_recycle(x)
#endif
