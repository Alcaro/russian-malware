#pragma once
#include "global.h"
#include "time.h"
#include "test.h"
#include <coroutine>
#ifdef _WIN32
typedef void* HANDLE;
#endif

#if 0
// To do async stuff as a coroutine, simply return an async<int>, and co_await other async functions.
// Doing async stuff as a normal function requires a fair bit of boilerplate:

class my_coro {
	struct waiter_t : public waiter_fn<int, waiter_t> {
		void complete(int val) { container_of<&my_coro::wait>(this)->complete(val); }
	} wait;
	struct producer_t : public producer_fn<size_t, producer_t> {
		void cancel() { container_of<&my_coro::prod>(this)->cancel(); }
	} prod;
	// If you have an array<> of producer_fn and need to resize it, do so, then call update_waiter() on every member.
	// There is no such equivalent for waiter_fn or coroutines.
	// Waiters and producers also have is_waiting() and has_waiter() members, respectively, to tell if anything's connected.
	// If you use has_waiter() only and don't care about cancel(), you can instead simply use
	producer_fn<void> prod2;
	// which, like update_waiter(), is not available for coroutines. Only applies to void producers.
	// If a coroutine isn't supposed to complete, you can use
	waiter_fn<void, void> wait2;
	// or runloop2::detach(), if cancellation isn't needed either.
	
	// If cancel() and complete() are simple enough, you can inline them into waiter_t and producer_t.
	void cancel()
	{
		wait.cancel();
	}
	void complete(int val)
	{
		prod.complete(val*2);
	}
public:
	async<size_t> do_thing(bool sync)
	{
		if (sync)
			return &prod.complete(42);
		else
			async_thing().then(&wait);
		return &prod;
	}
};

// You can, of course, have more or fewer waiters or producers.
// Arrays of waiters or producers require each object to hold a pointer to the parent (or find it in a thread local, or something).

// Unfortunately, these things are both tightly intertwined and templates, so it's hard to separate interface from implementation,
// and reading the header doesn't make for good documentation. The public interface is

template<typename T>
class async {
public:
	async(producer<T>* prod);
	using promise_type = producer_coro<T>;
	waiter_coro<T> operator co_await();
	void then(waiter<T>& next);
	// Returns another async<T>, returning def if the operation times out.
	// If T is void, there's no def parameter; instead, it returns async<bool>, false for timeout.
	// Can only be used by coroutines; for normal functions, declare two waiters instead.
	template<auto def> auto with_timeout(timestamp timeout);
};
namespace runloop2 {
	// The coroutine will be woken exactly once. To be awoken again, it must call await_read() again.
	// Registering a wait is O(1), with no syscalls or malloc.
#ifdef __unix__
	async<void> await_fd(int fd, bool want_write);
	async<void> await_read(int fd);
	async<void> await_write(int fd);
#endif
#ifdef _WIN32
	// Avoid if possible, it won't scale beyond MAXIMUM_WAIT_OBJECTS. Prefer OVERLAPPED and completion routines.
	async<void> await_handle(HANDLE h);
#endif
	
	// Used to implement async::with_timeout(), but also usable by standalone functions.
	async<void> await_timeout(timestamp timeout);
	
	// Schedules this coroutine for execution when the caller returns to the runloop.
	// They will execute in order of being added. There are no guarantees on how they're ordered relative to fds and timeouts.
	void schedule(std::coroutine_handle<> coro);
	// Adds a waiter for the event that simply does nothing. Useful mostly for tests.
	// If you want to be able to cancel it, or be told when it's done, use another waiter.
	void detach(async<void> event);
	
	// Runs the runloop. Returns after at least one event is dispatched, or if wait=false, returns immediately.
	// Returns whether anything happened.
	// Can't be called recursively.
	bool step(bool wait=true);
	void run(async<void> event); // Spins the runloop until the given event completes.
	
#ifdef ARLIB_THREAD
	// Creates a new thread, which runs the given coroutine until it completes.
	// (You can pass a producer_fn, but it won't have any way to access the new thread, so don't do that.)
	// The coroutine can use every runloop facility, except step() and run().
	void run_thread(async<void> event);
#endif
}

// An allocatable array is like a normal array, but you can allocate T-type objects from it.
// Objects will be returned in arbitrary order, with no attention paid to cacheline packing.
// The memory overhead over a normal array is a single pointer, but the object must contain
//  (directly or indirectly) an integer-type field capable of storing array indices, to be used as freelist.
// Allocating and deallocating is O(1), with no malloc or syscalls.
// The object also allows calling a function on each object on resize.
// The intended usecase is storing an array of some struct containing producer_fn.
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
// Unlike a normal mutex, waiters will be released in the same order as they enter.
// The object is not thread safe. Arlib does not, at this point, support any kind of interaction between coroutines and threads.
// Releasing the mutex will send the successor to runloop2::schedule(), not run it immediately; code like
// async<void> my_coro() { { auto lock = co_await mut1; co_await something(); } { auto lock = co_await mut2; co_await something_else(); } }
// will not allow any coroutine to overtake another, even if some of the something()s are synchronous.
// The object does not allocate any memory, other than what runloop2::schedule() does.
class co_mutex {
public:
	// The mutex is unlocked in this object's destructor.
	// Since cancellation is a thing, there's no async<void> lock().
	class lock {
	public:
		bool locked();
		void release();
	};
	async<lock> operator co_await();
	bool locked(); // Mostly usable for debugging and testing.
};

// Like test(), but it's a coroutine. Test passes if the coroutine returns void, like normal tests.
#define co_test(name, needs, provides) ...
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
template<typename T, typename T2 = void> class producer_fn;
template<typename T> class waiter_coro;
template<typename T, typename T2 = void> class waiter_fn;

template<typename T>
class async {
	producer<T>* prod;
public:
	async(producer<T>* prod) : prod(prod) {}
	using promise_type = producer_coro<T>;
	waiter_coro<T> operator co_await() { return prod; }
	void then(waiter<T>* next) { next->cancel(); next->inner_prod = prod; prod->inner_then(next); }
	template<auto def> auto with_timeout(timestamp timeout);
};

template<typename T>
class producer {
public:
	virtual void inner_then(waiter<T>* next) = 0;
private:
	friend class waiter<T>;
	virtual void inner_cancel() = 0;
};

template<typename T>
class waiter {
public:
	producer<T>* inner_prod = nullptr;
	~waiter() { cancel(); }
	void cancel() { if (inner_prod) inner_prod->inner_cancel(); inner_prod = nullptr; }
	virtual void inner_complete(T val) = 0;
};


template<typename T>
class producer_coro : public producer<T> {
	waiter<T>* next;
public:
	async<T> get_return_object() { return this; }
	std::suspend_always initial_suspend() { return {}; } // must suspend at some point, otherwise .inner_then() is called on a UAF
	std::suspend_never final_suspend() noexcept { return {}; }
	void unhandled_exception()
	{
		next->inner_prod = nullptr;
		if (!test_skipped())
			assert_unreachable();
	}
	void return_value(T val) { next->inner_complete(std::move(val)); }
	
	void inner_then(waiter<T>* next) override
	{
		this->next = next;
		coro_from_promise(*this).resume();
	}
	
private:
	void inner_cancel() override
	{
		coro_from_promise(*this).destroy();
	}
};

template<typename T, typename T2>
class producer_fn : public producer<T> {
	variant<T, waiter<T>*> contents;
	
public:
	producer_fn() = default;
	producer_fn(const producer_fn&) = delete;
	producer_fn(producer_fn&&) = default;
	producer_fn& operator=(const producer_fn&) = delete;
	producer_fn& operator=(producer_fn&&) = default;
	
	void inner_then(waiter<T>* next) override final
	{
		if (contents.template contains<T>())
			next->inner_complete(contents.template get_destruct<T>());
		else
			contents.template construct<waiter<T>*>(next);
	}
	
	producer_fn& complete(T val)
	{
		if (contents.template contains<waiter<T>*>())
			contents.template get_destruct<waiter<T>*>()->inner_complete(std::move(val));
		else
			contents.template construct<T>(std::move(val));
		return *this;
	}
	
	bool has_waiter() { return contents.template contains<waiter<T>*>(); }
	bool has_value() { return contents.template contains<T>(); }
	
	void update_waiter()
	{
		waiter<T>** w = contents.template try_get<waiter<T>*>();
		if (!w)
			return;
#ifndef ARLIB_OPT
		if (!w[0] || !w[0]->inner_prod)
			abort(); // should be impossible
#endif
		w[0]->inner_prod = this;
	}
	
private:
	void inner_cancel() override final
	{
		contents.destruct_any();
		if constexpr (!std::is_same_v<T2, void>)
		{
			static_assert(std::is_base_of_v<producer_fn, T2>);
			((T2*)this)->cancel();
		}
	}
};


template<typename T>
class waiter_coro : public waiter<T> {
	variant<T, std::coroutine_handle<>> contents;
public:
	waiter_coro(producer<T>* prod) { this->inner_prod = prod; prod->inner_then(this); }
	void inner_complete(T val) override final
	{
		if (contents.template contains<std::coroutine_handle<>>())
		{
			std::coroutine_handle<> coro = contents.template get_destruct<std::coroutine_handle<>>();
			contents.template construct<T>(std::move(val));
			coro.resume();
		}
		else
		{
			contents.template construct<T>(std::move(val));
		}
	}
	
	bool await_ready() { return contents.template contains<T>(); }
	void await_suspend(std::coroutine_handle<> coro)
	{
		contents.template construct<std::coroutine_handle<>>(coro);
	}
	T await_resume()
	{
		this->inner_prod = nullptr;
		return contents.template get_destruct<T>();
	}
};

template<typename T, typename T2>
class waiter_fn : public waiter<T> {
public:
	bool is_waiting() { return this->inner_prod; }
	void inner_complete(T val) override final
	{
		this->inner_prod = nullptr;
		static_assert(std::is_base_of_v<waiter_fn, T2>);
		((T2*)this)->complete(std::move(val));
	}
};

// Keep the above classes synchronized with the below.
// I'd prefer to deduplicate them, but C++ doesn't permit that.

template<> class async<void>;
template<> class producer<void>;
template<> class waiter<void>;

template<> class producer_coro<void>;
template<typename T2> class producer_fn<void, T2>;
template<> class waiter_coro<void>;
template<typename T2> class waiter_fn<void, T2>;

template<>
class async<void> {
	producer<void>* prod;
public:
	async(producer<void>* prod) : prod(prod) {}
	using promise_type = producer_coro<void>;
	inline waiter_coro<void> operator co_await();
	inline void then(waiter<void>* next);
	inline auto with_timeout(timestamp timeout);
};

template<>
class producer<void> {
public:
	virtual void inner_then(waiter<void>* next) = 0;
private:
	friend class waiter<void>;
	virtual void inner_cancel() = 0;
};

template<>
class waiter<void> {
public:
	producer<void>* inner_prod = nullptr;
	~waiter() { cancel(); }
	void cancel() { if (inner_prod) inner_prod->inner_cancel(); inner_prod = nullptr; }
	virtual void inner_complete() = 0;
};


template<>
class producer_coro<void> : public producer<void> {
	waiter<void>* next;
public:
	async<void> get_return_object() { return this; }
	std::suspend_always initial_suspend() { return {}; } // must suspend at some point, otherwise .inner_then() is called on a UAF
	std::suspend_never final_suspend() noexcept { return {}; }
	void unhandled_exception()
	{
		next->inner_prod = nullptr;
		if (!test_skipped())
			assert_unreachable();
	}
	void return_void() { next->inner_complete(); }
	
	void inner_then(waiter<void>* next) override
	{
		this->next = next;
		coro_from_promise(*this).resume();
	}
	
private:
	void inner_cancel() override
	{
		coro_from_promise(*this).destroy();
	}
};

template<typename T2>
class producer_fn<void, T2> : public producer<void> {
	variant<void, waiter<void>*> contents;
	
public:
	producer_fn() = default;
	producer_fn(const producer_fn&) = delete;
	producer_fn(producer_fn&&) = default;
	producer_fn& operator=(const producer_fn&) = delete;
	producer_fn& operator=(producer_fn&&) = default;
	
	void inner_then(waiter<void>* next) override final
	{
		if (contents.template contains<void>())
		{
			contents.template destruct<void>();
			next->inner_complete();
		}
		else
			contents.template construct<waiter<void>*>(next);
	}
	
	producer_fn& complete()
	{
		if (contents.template contains<waiter<void>*>())
			contents.template get_destruct<waiter<void>*>()->inner_complete();
		else
			contents.template construct<void>();
		return *this;
	}
	
	bool has_waiter() { return contents.template contains<waiter<void>*>(); }
	bool has_value() { return contents.template contains<void>(); }
	
	void update_waiter()
	{
		waiter<void>** w = contents.template try_get<waiter<void>*>();
		if (!w)
			return;
#ifndef ARLIB_OPT
		if (!w[0] || !w[0]->inner_prod)
			abort(); // should be impossible
#endif
		w[0]->inner_prod = this;
	}
	
private:
	void inner_cancel() override final
	{
		contents.destruct_any();
		if constexpr (!std::is_same_v<T2, void>)
		{
			static_assert(std::is_base_of_v<producer_fn, T2>);
			((T2*)this)->cancel();
		}
	}
};


template<>
class waiter_coro<void> : public waiter<void> {
	variant<void, std::coroutine_handle<>> contents;
public:
	waiter_coro(producer<void>* prod) { this->inner_prod = prod; prod->inner_then(this); }
	void inner_complete() override final
	{
		if (contents.template contains<std::coroutine_handle<>>())
		{
			std::coroutine_handle<> coro = contents.template get_destruct<std::coroutine_handle<>>();
			contents.template construct<void>();
			coro.resume();
		}
		else
		{
			contents.template construct<void>();
		}
	}
	
	bool await_ready() { return contents.template contains<void>(); }
	void await_suspend(std::coroutine_handle<> coro)
	{
		contents.template construct<std::coroutine_handle<>>(coro);
	}
	void await_resume()
	{
		this->inner_prod = nullptr;
		contents.template destruct<void>();
	}
};

template<typename T2>
class waiter_fn<void, T2> : public waiter<void> {
public:
	bool is_waiting() { return this->inner_prod; }
	void inner_complete() override final
	{
		this->inner_prod = nullptr;
		if constexpr (!std::is_same_v<T2, void>)
		{
			static_assert(std::is_base_of_v<waiter_fn, T2>);
			((T2*)this)->complete();
		}
	}
};

waiter_coro<void> async<void>::operator co_await() { return prod; }
void async<void>::then(waiter<void>* next) { next->cancel(); next->inner_prod = prod; prod->inner_then(next); }


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
	
	void schedule(std::coroutine_handle<> coro);
	
	void run(async<void> event);
	bool step(bool wait=true);
	
	template<typename T>
	class async_with_timeout {
		producer<T>* prod;
	public:
		async_with_timeout(producer<T>* prod) : prod(prod) {}
		waiter_coro<T> operator co_await() { return prod; }
	};
	
	template<typename T, auto def>
	class with_timeout_t : public async_with_timeout<T> {
		struct prod_t : public producer_fn<T, prod_t> {
			void cancel() { container_of<&with_timeout_t::prod>(this)->cancel(); }
		} prod;
		
		struct wait1_t : public waiter_fn<T, wait1_t> {
			void complete(T val) { container_of<&with_timeout_t::wait1>(this)->complete1(std::move(val)); }
		} wait1;
		struct wait2_t : public waiter_fn<void, wait2_t> {
			void complete() { container_of<&with_timeout_t::wait2>(this)->complete2(); }
		} wait2;
		
		void cancel() {}
		void complete1(T val) { prod.complete(std::move(val)); }
		void complete2() { prod.complete(def); }
		
	public:
		with_timeout_t(async<T> ev, timestamp timeout) : runloop2::async_with_timeout<T>(&prod)
		{
			ev.then(&wait1);
			runloop2::await_timeout(timeout).then(&wait2);
		}
	};
	
	class with_timeout_void_t : public async_with_timeout<bool> {
		producer_fn<bool> prod;
		
		struct wait1_t : public waiter_fn<void, wait1_t> {
			void complete() { container_of<&with_timeout_void_t::wait1>(this)->complete1(); }
		} wait1;
		struct wait2_t : public waiter_fn<void, wait2_t> {
			void complete() { container_of<&with_timeout_void_t::wait2>(this)->complete2(); }
		} wait2;
		
		void complete1() { prod.complete(true); }
		void complete2() { prod.complete(false); }
		
	public:
		with_timeout_void_t(async<void> ev, timestamp timeout) : runloop2::async_with_timeout<bool>(&prod)
		{
			ev.then(&wait1);
			runloop2::await_timeout(timeout).then(&wait2);
		}
	};
	
	class co_waiter_void_multi : public waiter<void> {
	public:
		void inner_complete() override final {}
	};
	extern co_waiter_void_multi co_wait_multi_inst;
	inline void detach(async<void> event)
	{
		event.then(&co_wait_multi_inst);
		co_wait_multi_inst.inner_prod = nullptr;
	}
	
	// Implementation detail of socket::dns(). Should not be used by anyone else.
	void* get_dns();
}

template<typename T>
template<auto def>
auto async<T>::with_timeout(timestamp timeout)
{
	return runloop2::with_timeout_t<T, def>{ *this, timeout };
}
auto async<void>::with_timeout(timestamp timeout)
{
	return runloop2::with_timeout_void_t{ *this, timeout };
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
		if (first) // allow deleting the coro while someone has it locked (dtor ordering is tricky), but not while anyone's waiting
			abort();
#endif
	}
};

// TODO: move to test.h once coros are merged into Arlib proper
static inline void co_run_test(async<void> inner)
{
	struct waiter_t : public waiter_fn<void, waiter_t> {
		void complete() {} // nothing, it uses is_waiting() only
	};
	waiter_t wait_co;
	waiter_t wait_time;
	inner.then(&wait_co);
	runloop2::await_timeout(timestamp::now()+duration::ms(10000)).then(&wait_time);
	while (wait_co.is_waiting() && wait_time.is_waiting())
		runloop2::step();
	// don't worry too much about whether the coro completed or timed out, just let caller report too-slow
}
#define TESTFUNCNAME_CO JOIN(_testfunc_co, __LINE__)
#define co_test(name, needs, provides) \
	static async<void> TESTFUNCNAME_CO(); \
	test(name, needs, provides) { co_run_test(TESTFUNCNAME_CO()); } \
	static async<void> TESTFUNCNAME_CO()
