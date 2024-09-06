#pragma once
#include "global.h"
#include "time.h"
#include "test.h"
#include <coroutine>
#ifdef _WIN32
typedef void* HANDLE;
#endif
#ifndef ARLIB_OPT
#include "os.h"
#endif

// Arlib producers and consumers are the equivalent of callback+userdata or signals in most runloops.
// Unlike said other runloops,
// - Arlib producers and waiters are intended to be stored inline in an owning object.
// - Arlib producer/waiter connections are strictly singleton, no multicasts. Can't even throw a value into the void if nobody's listening.
// - Arlib producer/waiter connections are one-shot; once anything is produced, it's disconnected.
//     It's safe to connect it again, including from the completion handler.
// - Arlib is well integrated with C++20 coroutines (which implies a few architectural constraints).
// (TODO: use other runloops more so I have more to say about them)

// To do async stuff as a coroutine, simply return an async<int>, and co_await other async functions.

// Unfortunately, these things are both tightly intertwined and templates, so it's hard to separate interface from implementation,
// and reading the header doesn't make for good documentation. The public interface (minus dtors, and copy/move/default ctors) is

#if 2+2 == 5 // not if 0, to trick github into highlighting the syntax
template<typename T>
class async {
public:
	async(producer<T, any>* prod);
	void then(waiter<T, any>* next);
	
	using promise_type = ...;
	... operator co_await();
};

template<typename T>
class producer {
public:
	producer(producer_inner<T>);
	
	void complete(T val); // if T is void, this argument is absent
	async<T> complete_sync(T val);
	
	bool has_waiter();
	void moved(); // If you have an array<> of producer and need to resize it, do so, then call this function on every member.
};
// The member argument must point to your producer within its containing class. The function can be any member with matching signature.
template<producer<T> Towner:: * memb, void(Towner::*fn_cancel)()>
producer_inner<T> make_producer();

template<typename T>
class waiter {
public:
	waiter(waiter_inner<T>);
	
	bool is_waiting();
	void cancel(); // Does nothing if there's no waiter, no need to call is_waiting() first.
	void moved();
};
// Same as for producers.
template<waiter<T> Towner:: * memb, void(Towner::*fn)(T)>
waiter_inner<T> make_waiter();
// You can also pass a lambda, if you don't have an object (mostly useful for tests).
// The lambda can't capture; if you need that, use the above variant.
template<fn_cancel = [](){ ... }>
waiter_inner<T> make_waiter();

// An example of producer/waiter:

class my_coro {
	producer<size_t> prod = make_producer<&my_coro::prod, &my_coro::cancel>();
	waiter<int> wait = make_waiter<&my_coro::wait, &my_coro::complete>();
	void complete(int val)
	{
		prod.complete(val*2);
	}
	void cancel()
	{
		wait.cancel();
	}
	
public:
	async<size_t> do_thing(bool sync)
	{
		if (sync)
		{
			return prod.complete_sync(42);
			// or, if you don't know if it will complete synchronously,
			async<size_t> ret = &prod;
			if (maybe)
				prod.complete(42);
			return ret;
			// both of which will store 42 in the async<>, and the waiter will complete as soon as it's connected.
		}
		else
		{
			async_thing().then(&wait);
			return &prod;
		}
	}
};

// There's also a polymorphic waiter.
// It has the same members as a normal waiter, except you can't pass it directly to async::then().
// Instead, you must call my_async.then(waiter.with(make_waiter<>())).
template<typename T>
class waiter_polymorph {
public:
	waiter<T>* with(waiter_inner<T>);
};

// You can, of course, have more or fewer waiters or producers.
// Arrays of waiters or producers require each object to hold a pointer to the parent (or find it in a thread local, or something).

namespace runloop2 {
	// Registering a wait is O(1), with no syscalls or malloc,
	//  except if this is the max number of concurrent waiters in the runloop's lifetime,
	//  in which case O(1) number of malloc and syscalls may happen.
	//  (Even if the max was exceeded, registering a wait is still amortized O(1).)
#ifdef __unix__
	async<void> await_fd(int fd, bool want_write);
	async<void> await_read(int fd) { return await_fd(fd, false); }
	async<void> await_write(int fd) { return await_fd(fd, true); }
#endif
#ifdef _WIN32
	// Limited to MAXIMUM_WAIT_OBJECTS (64) because lol windows.
	// OVERLAPPED and completion routines is often better,
	//  but continuing to use a socket after cancelling an operation is undefined,
	//  also because lol windows (and there's no completion routine for connecting a socket).
	// I/O Completion Ports also exist, but they do (as far as I can determine) not interact with window messages in any way.
	// If I need something with a large number of HANDLEs, I'll do one of
	// - disassemble MsgWaitForMultipleObjects until I find the queue HANDLE
	// - make an alternate IOCP-based runloop backend and force any bulk processing onto a separate thread
	// - spawn a thread whose job is to wait for an IOCP, and wake main thread whenever anything happens
	// - switch to WSARecv's completion routines and hope nobody's trying to connect 64 sockets simultaneously
	// - make an IOCP-based backend and use AttachThreadInput on a side thread to wake main when receiving a window message (if it works)
	// - MWFMO with an iocp as the only handle (seems to be undocumented and win10 only - use only if ossified, i.e. implemented in Wine)
	// - or simply tell people to use Linux.
	async<void> await_handle(HANDLE h);
#endif
	
	async<void> await_timeout(timestamp timeout);
	async<void> in_ms(int ms);
	
	// Schedules this coroutine for execution when the caller returns to the runloop.
	// They will execute in order of being added. There are no guarantees on how they're ordered relative to fds and timers.
	void schedule(std::coroutine_handle<> coro);
	
	// Runs the runloop. Returns after at least one event is dispatched, or if wait=false, returns immediately.
	// Returns whether anything happened.
	// Can't be called recursively.
	bool step(bool wait=true);
	
	void run(async<void> event); // Spins the runloop until the given event completes.
}

// An allocatable array is like a normal array, but you can allocate T-type objects from it.
// Objects will be returned in arbitrary order, with no attention paid to cacheline packing.
// The memory overhead over a normal array is a single pointer, but the object must contain
//  (directly or indirectly) an integer-type field capable of storing array indices, to be used as freelist.
// Allocating and deallocating is O(1), with no malloc or syscalls.
// The object also allows calling a function on each object on resize.
// The intended usecase is storing an array of some struct containing producer or waiter, hence why it's in this file.
// For waiter<void> with empty handler, consider instead co_holder.
template<typename T, auto get_freelist, auto on_move = nullptr>
class allocatable_array {
public:
	T* alloc();
	// Adds the item to the freelist. The item is still usable until the next alloc(), other than its freelist member.
	void dealloc(T* item);
	size_t size();
	T* begin();
	T* end();
}

// A co_mutex is a mutex for coroutines; only one coroutine at the time may enter this region.
// This object is designed for coroutines only; normal functions cannot call it.
// Unlike a normal mutex, waiters are ordered.
// The object is not thread safe. Arlib does not, at this point, support any kind of interaction between coroutines and threads.
// Releasing the mutex will send the successor to runloop2::schedule(), not run it immediately; code like
// async<void> my_coro() { { auto lock = co_await mut1; co_await something(); } { auto lock = co_await mut2; co_await something2(); } }
// will not allow any coroutine to overtake another, even if some of the something()s are synchronous.
// The object does not allocate any memory, other than what runloop2::schedule() does.
class co_mutex {
public:
	// The mutex is unlocked in this object's destructor.
	// Since cancellation is a thing, there's no async<void> lock() / async<void> unlock().
	class lock {
	public:
		bool locked();
		void release();
	};
	async<lock> operator co_await();
	bool locked(); // Mostly usable for debugging and testing.
};

// A co_holder tracks multiple coroutines. If deleted, it cancels them all.
class co_holder {
public:
	void add(async<void> item);
	void reset();
};
#endif


template<typename T> std::coroutine_handle<> coro_from_promise(T& prom)
{
	static_assert(sizeof(prom.initial_suspend()) >= 0);
	return std::coroutine_handle<T>::from_promise(prom);
}

template<typename T> class async;
template<typename T> class producer;
template<typename T> class waiter;

template<typename T> class producer_coro;
template<typename T> class waiter_coro;
template<typename R1, typename R2> class multi_waiter_t;

// c++ needs an absurd amount of trickery around optional parameters...
template<typename T> using empty_if_void = std::conditional_t<std::is_same_v<T, void>, empty_class, T>;

template<typename T>
class waiter {
protected:
	// just friend all, no point figuring out which can be refactored out (or just removed outright)
	template<typename Tf> friend class async;
	template<typename Tf> friend class producer;
	template<typename Tf> friend class waiter;
	template<typename Tf> friend class producer_coro;
	template<typename Tf> friend class waiter_coro;
	
public:
	producer<T>* prod = nullptr;
protected:
	
	typedef void(*complete_t_void)(waiter<T>* self);
	typedef void(*complete_t_val)(waiter<T>* self, empty_if_void<T> val);
public:
	typedef std::conditional_t<std::is_same_v<T, void>, complete_t_void, complete_t_val> complete_t;
protected:
	complete_t complete;
	
	waiter(const waiter&) = delete;
	waiter(waiter&& other)
	{
		this->prod = other.prod;
		this->complete = other.complete;
		other.prod = nullptr;
	}
public:
	waiter(complete_t complete) : complete(complete) {}
	waiter() requires (std::is_same_v<T, void>) : waiter<T>([](waiter<T>* self) { self->prod = nullptr; }) {}
	
	bool is_waiting() { return this->prod != nullptr; }
	void cancel()
	{
		if (prod)
			prod->cancel(prod);
	}
	void moved()
	{
		if (this->prod)
			this->prod->wait = this;
	}
	~waiter() { cancel(); }
};

template<typename T>
class waiter_polymorph : private waiter<T> {
public:
	waiter_polymorph() : waiter<T>(nullptr) {}
	waiter_polymorph(const waiter_polymorph&) = delete;
	
	bool is_waiting() { return waiter<T>::is_waiting(); }
	void cancel() { waiter<T>::cancel(); }
	void moved() { waiter<T>::moved(); }
	
	waiter<T>* with(typename waiter<T>::complete_t fn)
	{
		this->complete = fn;
		return this;
	}
};

template<auto memb, auto fn, typename Twait, typename Towner, typename Targ>
typename waiter<Targ>::complete_t
make_waiter_inner_bind(Twait Towner:: * memb_, void(Towner::*fn_)(Targ)) requires (!std::is_same_v<Targ, void>)
{
	static_assert(std::is_same_v<Twait, waiter<Targ>> || std::is_same_v<Twait, waiter_polymorph<Targ>>);
	return [](waiter<Targ>* self, Targ val)
	{
		self->prod = nullptr;
		(container_of<memb>((Twait*)self)->*fn)(std::move(val));
	};
}
template<auto memb, auto fn, typename Twait, typename Towner>
typename waiter<void>::complete_t
make_waiter_inner_bind(Twait Towner:: * memb_, void(Towner::*fn_)())
{
	static_assert(std::is_same_v<Twait, waiter<void>> || std::is_same_v<Twait, waiter_polymorph<void>>);
	return [](waiter<void>* self)
	{
		self->prod = nullptr;
		(container_of<memb>((Twait*)self)->*fn)();
	};
}
template<auto memb, auto fn>
auto make_waiter()
{
	return make_waiter_inner_bind<memb, fn>(memb, fn);
}

template<typename Tl, typename Targ>
typename waiter<Targ>::complete_t
make_waiter_inner_lambda(void(Tl::*fn_)(Targ val) const) requires (!std::is_same_v<Targ, void>)
{
	return [](waiter<Targ>* self, Targ val) { self->prod = nullptr; Tl()(std::move(val)); };
}
template<typename Tl>
typename waiter<void>::complete_t
make_waiter_inner_lambda(void(Tl::*fn_)() const)
{
	return [](waiter<void>* self) { self->prod = nullptr; Tl()(); };
}
template<auto fn>
auto make_waiter()
{
	return make_waiter_inner_lambda(&decltype(fn)::operator());
}

template<typename T>
class producer {
protected:
	template<typename Tf> friend class async;
	template<typename Tf> friend class producer;
	template<typename Tf> friend class waiter;
	template<typename Tf> friend class producer_coro;
	template<typename Tf> friend class waiter_coro;
	
	static void cancel_s(producer<T>* self)
	{
		if (self->wait)
			self->wait->prod = nullptr;
		self->wait = nullptr;
	}
	
public:
	waiter<T>* wait = nullptr;
	typedef void(*cancel_t)(producer<T>* self);
protected:
	cancel_t cancel;
	
	producer(const producer&) = delete;
public:
	producer() : cancel(cancel_s) {}
	producer(cancel_t cancel) : cancel(cancel) {}
	
	void moved()
	{
		if (this->wait)
			this->wait->prod = this;
	}
	
	void complete() requires (std::is_same_v<T, void>)
	{
		waiter<T>* wait = this->wait;
		this->wait = nullptr;
#ifndef ARLIB_OPT
		if (wait == nullptr)
			debug_fatal_stack("attempt to complete() a producer with no waiter; did you mean complete_sync()?\n");
#endif
		wait->prod = nullptr; // if it crashes here, you probably called complete when you meant complete_sync
		wait->complete(wait);
	}
	
	void complete(empty_if_void<T> val) requires (!std::is_same_v<T, void>)
	{
		waiter<T>* wait = this->wait;
		this->wait = nullptr;
#ifndef ARLIB_OPT
		if (wait == nullptr)
			debug_fatal_stack("attempt to complete() a producer with no waiter; did you mean complete_sync()?\n");
#endif
		wait->prod = nullptr; // if it crashes here, you probably called complete when you meant complete_sync
		wait->complete(wait, std::move(val));
	}
	
	static async<T> complete_sync() requires (std::is_same_v<T, void>)
	{
		return async<T>(typename async<T>::create_sync());
	}
	
	static async<T> complete_sync(empty_if_void<T> val) requires (!std::is_same_v<T, void>)
	{
		return async<T>(typename async<T>::create_sync(), std::move(val));
	}
	
	bool has_waiter()
	{
		return this->wait != nullptr;
	}
	
#ifndef ARLIB_OPT
	~producer()
	{
		if (wait)
			debug_fatal_stack("can't destruct a producer with a connected waiter, destroy the waiter first\n");
	}
#endif
};

template<auto memb, auto fn_cancel, typename Tprod, typename Towner>
typename producer<Tprod>::cancel_t
make_producer_inner_bind(producer<Tprod> Towner:: * memb_, void(Towner::*fn_cancel_)())
{
	return [](producer<Tprod>* self)
	{
		if (self->wait)
			self->wait->prod = nullptr;
		self->wait = nullptr;
		(container_of<memb>(self)->*fn_cancel)();
	};
}
template<auto memb, auto fn_cancel>
auto make_producer()
{
	return make_producer_inner_bind<memb, fn_cancel>(memb, fn_cancel);
}

//template<typename Tl>
//producer<Tprod>::complete_t
//make_producer_inner_lambda(void(Tl::*fn_cancel_)() const)
//{
//	return [](waiter<void>* self) { self->prod = nullptr; Tl()(); };
//}
//template<auto fn_cancel>
//auto make_producer()
//{
//	return make_producer_inner_lambda(&decltype(fn_cancel)::operator());
//}

template<typename T>
class async : public waiter<T> {
	template<typename Tf> friend class async;
	template<typename Tf> friend class producer;
	template<typename Tf> friend class waiter;
	template<typename Tf> friend class producer_coro;
	template<typename Tf> friend class waiter_coro;
	template<typename R1, typename R2> friend class multi_waiter_t;
	
	static const bool value_in_prod = (sizeof(empty_if_void<T>) <= sizeof(producer<T>*));
	
	[[no_unique_address]]
	std::conditional_t<value_in_prod, empty_class, variant_raw<empty_if_void<T>>> value;
	
	void mark_consumed()
	{
#ifndef ARLIB_OPT
		this->prod = nullptr;
		this->complete = (typename waiter<T>::complete_t)(void*)1;
#endif
	}
	
	variant_raw<T>* get_store()
	{
		if constexpr (value_in_prod)
			return (variant_raw<T>*)&this->prod;
		else
			return &value;
	}
	void set_value(empty_if_void<T> val)
	{
		get_store()->template construct<T>(std::move(val));
	}
	T get_value()
	{
#ifndef ARLIB_OPT
		if (!is_complete())
			debug_fatal_stack("can't call get_value if this async didn't complete synchronously\n");
#endif
		if constexpr (std::is_same_v<T, void>)
		{
			mark_consumed();
		}
		else
		{
			T ret = get_store()->template get_destruct<T>();
			mark_consumed();
			return ret;
		}
	}
	
	static void complete_s(waiter<T>* self_) requires (std::is_same_v<T, void>)
	{
		async* self = (async*)self_;
		self->prod = nullptr;
		self->complete = nullptr;
	}
	
	static void complete_s(waiter<T>* self_, empty_if_void<T> val) requires (!std::is_same_v<T, void>)
	{
		async* self = (async*)self_;
		self->prod = nullptr;
		self->complete = nullptr;
		self->set_value(std::move(val));
	}
	
	bool is_complete()
	{
		return (this->complete == nullptr);
	}
	
	class create_sync {};
	
	async(create_sync) requires std::is_same_v<T, void> : waiter<T>(nullptr)
	{
		this->prod = nullptr;
		this->complete = nullptr;
	}
	
	async(create_sync, empty_if_void<T> val) requires (!std::is_same_v<T, void>) : waiter<T>(nullptr)
	{
		this->prod = nullptr;
		this->complete = nullptr;
		this->set_value(std::move(val));
	}
	
public:
	async(nullptr_t) = delete;
	async(producer<T>* prod) : waiter<T>(&async::complete_s)
	{
		this->prod = prod;
		prod->wait = this;
	}
	async(const async&) = delete;
	async(async&& other) : waiter<T>(std::move((waiter<T>&)other))
	{
		this->value = other.value;
		other.mark_consumed();
	}
	~async()
	{
#ifndef ARLIB_OPT
		if (is_complete())
			debug_fatal_stack("can't discard a sync async without calling .then()\n");
		if (this->prod)
			debug_fatal_stack("can't discard a waiting async without calling .then()\n");
#endif
		this->prod = nullptr; // null it out, in case the return value is here (nulling here allows parent's dtor to be optimized out)
	}
	
	using promise_type = producer_coro<T>;
	waiter_coro<T> operator co_await() { return this; }
	
	void then(waiter<T>* next)
	{
		if (is_complete())
		{
			if constexpr (std::is_same_v<T, void>)
			{
				get_value();
				next->complete(next);
			}
			else
			{
				next->complete(next, get_value());
			}
		}
		else
		{
#ifndef ARLIB_OPT
			if (next->prod)
			{
				debug_warn_stack("attempted to assign a producer to an already-busy waiter\n");
				next->cancel();
			}
#endif
			next->prod = this->prod;
			this->prod->wait = next;
			this->prod = nullptr;
		}
	}
};

template<typename T>
class producer_coro : public producer<T> {
	static void cancel_s(producer<T>* self_)
	{
		producer_coro* self = (producer_coro*)self_;
		// unlike function producer, this object is destroyed after cancellation, so self->wait is never null
		self->wait->prod = nullptr;
#ifndef ARLIB_OPT
		self->wait = nullptr;
#endif
		coro_from_promise(*self).destroy();
	}
	
public:
	producer_coro() : producer<T>(&producer_coro::cancel_s) {}
	producer_coro(const producer_coro&) = delete;
	
	async<T> get_return_object() { return this; }
	std::suspend_never initial_suspend() { return {}; }
	std::suspend_never final_suspend() noexcept { return {}; }
	void unhandled_exception() { if (this->wait) this->wait->prod = nullptr; this->wait = nullptr; _test_coro_exception(); }
	
	void return_value(T val)
	{
		waiter<T>* wait = this->wait;
		this->wait = nullptr;
		wait->prod = nullptr;
		wait->complete(wait, std::move(val));
	}
};

template<> // can't declare return_void and return_value in the same promise for some complicated reason
class producer_coro<void> : public producer<void> {
	static void cancel_s(producer<void>* self_)
	{
		producer_coro* self = (producer_coro*)self_;
		// unlike function producer, this object is destroyed after cancellation, so self->wait is never null
		self->wait->prod = nullptr;
#ifndef ARLIB_OPT
		self->wait = nullptr;
#endif
		coro_from_promise(*self).destroy();
	}
	
public:
	producer_coro() : producer<void>(&producer_coro::cancel_s) {}
	producer_coro(const producer_coro&) = delete;
	
	async<void> get_return_object() { return this; }
	std::suspend_never initial_suspend() { return {}; }
	std::suspend_never final_suspend() noexcept { return {}; }
	void unhandled_exception() { if (this->wait) this->wait->prod = nullptr; this->wait = nullptr; _test_coro_exception(); }
	
	void return_void()
	{
		waiter<void>* wait = this->wait;
		this->wait = nullptr;
		wait->prod = nullptr;
		wait->complete(wait);
	}
};

template<typename T>
class waiter_coro : public waiter<T> {
	friend class async<T>;
	
	variant_raw<std::coroutine_handle<>, empty_if_void<T>> contents;
	
	static void complete_s(waiter<T>* self_) requires (std::is_same_v<T, void>)
	{
		waiter_coro* self = (waiter_coro*)self_;
		self->prod = nullptr;
		std::coroutine_handle<> coro = self->contents.template get_destruct<std::coroutine_handle<>>();
		coro.resume();
	}
	
	static void complete_s(waiter<T>* self_, empty_if_void<T> val) requires (!std::is_same_v<T, void>)
	{
		waiter_coro* self = (waiter_coro*)self_;
		self->prod = nullptr;
		std::coroutine_handle<> coro = self->contents.template get_destruct<std::coroutine_handle<>>();
		self->contents.template construct<T>(std::move(val));
		coro.resume();
	}
	
	waiter_coro(async<T>* src) : waiter<T>(&waiter_coro::complete_s)
	{
		if (src->is_complete())
		{
			this->complete = nullptr;
			if constexpr (!std::is_same_v<T, void>)
				this->contents.template construct<T>(src->get_value());
			else
				src->get_value();
		}
		else
		{
			this->prod = src->prod;
			this->prod->wait = this;
			src->prod = nullptr;
		}
	}
	waiter_coro(const waiter_coro&) = delete;
	
public:
	bool await_ready() { return (this->complete == nullptr); }
	void await_suspend(std::coroutine_handle<> coro) { contents.template construct<std::coroutine_handle<>>(coro); }
	T await_resume()
	{
		if constexpr (!std::is_same_v<T, void>)
			return contents.template get_destruct<T>();
	}
};


class co_holder {
	array<waiter<void>> waiters;
public:
	co_holder()
	{
		waiters.resize(8);
	}
	void add(async<void> item)
	{
		for (waiter<void>& wait : waiters)
		{
			if (!wait.is_waiting())
			{
				item.then(&wait);
				return;
			}
		}
		size_t sz = waiters.size();
		waiters.resize(sz*2);
		for (waiter<void>& wait : waiters)
		{
			wait.moved();
		}
		item.then(&waiters[sz]);
	}
	void reset()
	{
		for (waiter<void>& wait : waiters)
			wait.cancel();
		waiters.resize(8);
	}
};

namespace runloop2 {
#ifdef __unix__
	async<void> await_fd(int fd, bool want_write);
	inline async<void> await_read(int fd) { return await_fd(fd, false); }
	inline async<void> await_write(int fd) { return await_fd(fd, true); }
#endif
#ifdef _WIN32
	async<void> await_handle(HANDLE h);
#endif
	
	async<void> await_timeout(timestamp timeout);
	async<void> in_ms(int ms);
	
	void schedule(std::coroutine_handle<> coro);
	
	void run(async<void> event);
	bool step(bool wait=true);
	
	// Implementation detail of socket::dns(). Should not be used by anyone else.
	void* get_dns();
	
#ifdef ARLIB_TESTRUNNER
	void test_begin();
	void test_end();
#endif
}

template<typename T, auto get_freelist, auto on_move = nullptr>
class allocatable_array {
	array<T> inner;
	size_t freelist = 0;
	
	void reallocate(size_t newsize)
	{
		size_t cursize = inner.size();
		inner.resize(newsize);
		for (size_t i=0;i<cursize;i++)
		{
			if constexpr (!std::is_same_v<decltype(on_move), nullptr_t>)
				on_move(&inner[i]);
		}
		for (size_t i=cursize;i<newsize;i++)
			get_freelist(&inner[i])[0] = i+1;
	}
public:
	T* alloc()
	{
		if (freelist == inner.size())
		{
			if (inner.size() == 0)
				reallocate(8);
			else
				reallocate(inner.size()*2);
		}
		
		T* ret = &inner[freelist];
		freelist = get_freelist(ret)[0];
		return ret;
	}
	void dealloc(T* item)
	{
		get_freelist(item)[0] = freelist;
		freelist = item - inner.ptr();
	}
	
	size_t size() { return inner.size(); }
	T* begin() { return inner.begin(); }
	T* end() { return inner.end(); }
};

// A co_mutex stops multiple coroutines from running in the same critical section.
// They will continue in the same order they stop, so you can send three HTTP requests to the same socket,
//  enter a co_mutex, and read the responses.
// Usable by coroutines only, not manual waiters.
// Despite the name, it's not thread safe (just like all other runloop-related objects).
class co_mutex {
public:
	class lock {
		friend class co_mutex;
		co_mutex* parent;
		void consume(lock& other)
		{
			parent = other.parent;
			other.parent = nullptr;
			parent->the_lock = this;
		}
	public:
		lock() : parent(nullptr) {}
		lock(co_mutex* parent) : parent(parent) { parent->the_lock = this; }
		lock(lock&& other) { consume(other); }
		lock& operator=(lock&& other) { release(); consume(other); return *this; }
		~lock() { release(); }
		
		bool locked() { return parent != nullptr; }
		void release()
		{
			if (parent)
				parent->unlock();
			parent = nullptr;
		}
	};
private:
	class waiter {
		friend class co_mutex;
		std::coroutine_handle<> coro;
		waiter* next = nullptr;
		co_mutex* parent;
	public:
		waiter(co_mutex* parent) : parent(parent)
		{
			parent->last[0] = this;
			parent->last = &next;
		}
		waiter(waiter&&) = delete;
		~waiter()
		{
			waiter** iter = &parent->first;
			while (*iter != this)
				iter = &iter[0]->next;
			*iter = this->next;
			if (parent->last == &next)
				parent->last = iter;
		}
		
		bool await_ready()
		{
			return !parent->the_lock;
		}
		void await_suspend(std::coroutine_handle<> coro)
		{
			this->coro = coro;
		}
		lock await_resume()
		{
			return parent;
		}
	};
	waiter* first = nullptr;
	waiter* * last = &first;
	lock* the_lock = nullptr;
	
	void unlock()
	{
		if (first)
			runloop2::schedule(first->coro);
		else
			the_lock = nullptr;
	}
	
public:
	waiter operator co_await() { return this; }
	bool locked() { return the_lock; }
	co_mutex() = default;
	co_mutex(co_mutex&&) = delete;
	~co_mutex()
	{
		if (the_lock)
			the_lock->parent = nullptr;
#ifndef ARLIB_OPT
		if (first) // allow deleting the co_mutex while someone has it locked (dtor ordering is tricky), but not while anyone's waiting
			abort();
#endif
	}
};

template<typename R1, typename R2>
class multi_waiter_t {
	variant_raw<std::coroutine_handle<>, variant<R1, R2>> contents;
	
	void complete_inner(variant<R1, R2> val)
	{
		std::coroutine_handle<> coro = contents.template get_destruct<std::coroutine_handle<>>();
		contents.template construct<variant<R1, R2>>(std::move(val));
		coro.resume();
	}
	
	template<typename T>
	void complete(T val)
	{
		variant<R1, R2> var;
		var.template construct<T>(std::move(val));
		complete_inner(std::move(var));
	}
	
	void complete_void()
	{
		variant<R1, R2> var;
		var.template construct<void>();
		complete_inner(std::move(var));
	}
	
	template<auto memb, typename T>
	auto get_waiter()
	{
		if constexpr (std::is_same_v<T, void>)
			return make_waiter<memb, &multi_waiter_t::complete_void>();
		else
			return make_waiter<memb, &multi_waiter_t::complete<T>>();
	}
	
	waiter<R1> wait1 = get_waiter<&multi_waiter_t::wait1, R1>();
	waiter<R2> wait2 = get_waiter<&multi_waiter_t::wait2, R2>();
	
public:
	multi_waiter_t(async<R1> a1, async<R2> a2)
	{
		if (a1.is_complete())
		{
			variant<R1, R2> var;
			if constexpr (std::is_same_v<R1, void>)
			{
				a1.get_value();
				var.template construct<R1>();
			}
			else
			{
				var.template construct<R1>(a1.get_value());
			}
			contents.template construct<variant<R1, R2>>(std::move(var));
			
			if (a2.is_complete())
				a2.get_value(); // just discard it
			else
				a2.then(&wait2);
		}
		else if (a2.is_complete())
		{
			a1.then(&wait1);
			wait1.cancel(); // and cancel it so await_ready doesn't do anything silly (cancel not needed in the above branch)
			
			variant<R1, R2> var;
			if constexpr (std::is_same_v<R2, void>)
			{
				a2.get_value();
				var.template construct<R2>();
			}
			else
			{
				var.template construct<R2>(a2.get_value());
			}
			contents.template construct<variant<R1, R2>>(std::move(var));
		}
		else
		{
			a1.then(&wait1);
			a2.then(&wait2);
		}
	}
	
	bool await_ready() { return (!wait1.is_waiting()); }
	void await_suspend(std::coroutine_handle<> coro) { contents.template construct<std::coroutine_handle<>>(coro); }
	variant<R1, R2> await_resume() { return contents.template get_destruct<variant<R1, R2>>(); }
};
template<typename R1, typename R2>
multi_waiter_t<R1, R2> multi_waiter(async<R1> a1, async<R2> a2)
{
	return { std::move(a1), std::move(a2) };
}
