#include "runloop.h"
#include <time.h>

// TODO: find a better place for this
uintptr_t runloop::raw_set_timer_abs(time_t when, function<void()> callback)
{
	time_t now = time(NULL);
	unsigned ms = (now < when ? (when-now)*1000 : 0);
	return raw_set_timer_rel(ms, false, callback);
}

#ifdef ARLIB_TESTRUNNER
#include "test.h"
#include "os.h"
#include "thread.h"
#include "socket.h"
#include <unistd.h>

//#ifdef ARLIB_TESTRUNNER
class runloop_blocktest : public runloop {
	runloop* loop;
	bool can_exit = false;
	
#ifdef __unix__
	typedef uintptr_t fd_t;
#else
	typedef HANDLE fd_t;
#endif
	
	uint64_t us = 0;
	uint64_t loopdetect = 0;
	/*private*/ void begin()
	{
		uint64_t new_us = time_us_ne();
		if (new_us/1000000/10 != us/1000000/10) loopdetect = 0;
		loopdetect++;
		if (loopdetect == 10000) assert(!"10000 runloop iterations in 10 seconds");
		us = new_us;
	}
	/*private*/ void end()
	{
		uint64_t new_us = time_us_ne();
		uint64_t diff = new_us-us;
		_test_runloop_latency(diff);
	}
	
	//don't carelessly inline into the lambdas; sometimes lambdas are deallocated by the callbacks, so 'this' is a use-after-free
	/*private*/ void do_cb(function<void(fd_t)> cb, fd_t arg)
	{
		this->begin();
		cb(arg);
		this->end();
	}
	/*private*/ void do_cb(function<void()> cb)
	{
		this->begin();
		cb();
		this->end();
	}
	
#ifdef __unix__
	void set_fd(uintptr_t fd, function<void(uintptr_t)> cb_read, function<void(uintptr_t)> cb_write) override
	{
		function<void(uintptr_t)> cb_read_w;
		function<void(uintptr_t)> cb_write_w;
		if (cb_read)  cb_read_w  = bind_lambda([this, cb_read ](uintptr_t fd){ this->do_cb(cb_read,  fd); });
		if (cb_write) cb_write_w = bind_lambda([this, cb_write](uintptr_t fd){ this->do_cb(cb_write, fd); });
		loop->set_fd(fd, std::move(cb_read_w), std::move(cb_write_w));
	}
#else
	void set_object(HANDLE h, function<void(HANDLE)> cb) override
	{
		function<void(HANDLE)> cb_w;
		if (cb) cb_w  = bind_lambda([this, cb](HANDLE h){ this->do_cb(cb, h); });
		loop->set_object(h, std::move(cb_w));
	}
#endif
	
	uintptr_t raw_set_timer_rel(unsigned ms, bool repeat, function<void()> callback) override
	{
		function<void()> callback_w = bind_lambda([](){});
		if (callback) callback_w = bind_lambda([this, callback]() { this->do_cb(callback); });
		if (repeat) return loop->raw_set_timer_repeat(ms, callback_w);
		else return loop->raw_set_timer_once(ms, callback_w);
	}
	uintptr_t raw_set_idle(function<void()> callback) override
	{
		function<void()> callback_w = bind_lambda([](){});
		if (callback) callback_w = bind_lambda([this, callback]() { this->do_cb(callback); });
		return loop->raw_set_idle(callback_w);
	}
	void raw_timer_remove(uintptr_t id) override { loop->raw_timer_remove(id); }
	
#ifdef ARLIB_THREAD
	void prepare_submit() override { loop->prepare_submit(); }
	void submit(function<void()>&& cb) override { loop->submit(std::move(cb)); }
#endif
	
	void enter() override { can_exit = true; end(); loop->enter(); begin(); can_exit = false; }
	void exit() override { assert(can_exit); loop->exit(); }
	void step(bool wait) override { end(); loop->step(wait); begin(); }
	
public:
	runloop_blocktest(runloop* inner) : loop(inner)
	{
		begin();
	}
	~runloop_blocktest()
	{
		end();
		delete loop;
	}
	
	void recycle()
	{
		us = time_us_ne();
		loopdetect = 0;
	}
	
	void assert_empty() override
	{
		loop->assert_empty();
	}
};

runloop* runloop_wrap_blocktest(runloop* inner)
{
	return new runloop_blocktest(inner);
}

void runloop_blocktest_recycle(runloop* loop)
{
	((runloop_blocktest*)loop)->recycle();
}
#endif

#ifdef ARLIB_TEST
#ifdef _WIN32
#define usleep(n) Sleep((n)/1000)
#endif

static void test_runloop(bool is_global)
{
	runloop* loop = (is_global ? runloop::global() : runloop::create());
	
	//must be before the other one, loop->enter() must be called to ensure it doesn't actually run
	loop->raw_timer_remove(loop->raw_set_timer_once(50, bind_lambda([]() { assert_unreachable(); })));
	
	//don't put this scoped, id is used later
	uintptr_t id = loop->raw_set_timer_repeat(20, bind_lambda([&]()
		{
			assert_ne(id, 0);
			uintptr_t id_copy = id; // the 'id' reference gets freed by loop->remove(), reset it before that and keep a copy
			id = 0;
			loop->raw_timer_remove(id_copy);
		}));
	assert_ne(id, 0); // no thinking -1 is the highest ID so 0 should be used
	
	{
		int64_t start = time_ms_ne();
		int64_t end = start;
		uint64_t id2 = loop->raw_set_timer_once(100, bind_lambda([&]() { end = time_ms_ne(); loop->exit(); }));
		loop->enter();
		
		assert_range(end-start, 75,200);
		loop->raw_timer_remove(id2);
	}
	
	assert_eq(id, 0);
	
#ifdef ARLIB_THREAD
	if(0){
		loop->prepare_submit();
		uint64_t start_ms = time_ms_ne();
		function<void()> threadproc = bind_lambda([loop, start_ms]()
			{
				while (start_ms+100 > time_ms_ne()) usleep(1000);
				loop->submit(bind_lambda([loop]()
					{
						loop->exit();
					}));
			});
		thread_create(std::move(threadproc));
		loop->enter();
		uint64_t end_ms = time_ms_ne();
		assert_range(end_ms-start_ms, 75,200);
	}
#endif
	
	//I could stick in some fd tests, but the sockets test all plausible operations anyways.
	//Okay, they should vary which runloop they use.
	
	if (!is_global) delete loop;
}
test("global runloop", "function,array,set,time", "runloop")
{
	test_runloop(true);
}
class loop_tester {
public:
	autoptr<runloop> loop;
	DECL_TIMER(t1, loop_tester);
	DECL_TIMER(t2, loop_tester);
};
class loop_tester2 {
public:
	runloop* p_loop;
	runloop* loop() { return p_loop; }
	DECL_TIMER(t3, loop_tester2);
	DECL_TIMER(t4, loop_tester2);
};
test("private runloop", "function,array,set,time", "runloop")
{
	test_runloop(false);
	
	loop_tester holder;
	loop_tester2 holder2;
	holder.loop = runloop::create();
	holder2.p_loop = holder.loop;
	
	int n = 0;
#ifdef _WIN32
#define TIME_MUL 3 // Windows time is inaccurate by default
#else
#define TIME_MUL 1
#endif
	holder.t1.set_repeat(20*TIME_MUL, [&](){ n++; });
	holder.t2.set_once(50*TIME_MUL, [&](){ holder.loop->exit(); });
	holder2.t3.set_repeat(10*TIME_MUL, [&](){ n+=10; holder2.t3.reset(); });
	holder2.t4.set_repeat(30*TIME_MUL, [&](){ n+=100; });
	holder.loop->enter();
	assert_eq(n, 112);
}

#ifdef ARLIB_SOCKET
static void test_runloop_2(bool is_global)
{
	runloop* loop = (is_global ? runloop::global() : runloop::create());
	
	socket* sock[2];
	uint8_t buf[2][8];
	size_t n_buf[2] = {0,0};
	for (int i=0;i<2;i++)
	{
		sock[i] = socket::create("muncher.se", 80, loop);
		sock[i]->callback([&, i]()
			{
				if (n_buf[i] < 8)
				{
					int bytes = sock[i]->recv(arrayvieww<uint8_t>(buf[i]).skip(n_buf[i]));
					assert_gte(bytes, 0);
					n_buf[i] += bytes;
				}
				if (n_buf[i] >= 8)
				{
					uint8_t discard[4096];
					if (sock[i]->recv(discard) < 0)
					{
						delete sock[i];
						sock[i] = NULL;
					}
				}
				if (n_buf[0] == 8 && n_buf[1] == 8) loop->exit();
			});
		sock[i]->send(cstring("GET / HTTP/1.1\r\n").bytes());
	}
	uintptr_t t = loop->raw_set_timer_once(500, [&](){ loop->exit(); });
	loop->enter();
	loop->raw_timer_remove(t);
	
	for (int i=0;i<2;i++)
	{
		assert_eq(n_buf[i], 0);
		sock[i]->send(cstring("Host: muncher.se\r\nConnection: close\r\n\r\n").bytes());
	}
	uintptr_t t2 = loop->raw_set_timer_once(100, [&](){ loop->raw_timer_remove(t2); t2 = 0; });
	usleep(200*1000); // ensure everything is ready simultaneously
	runloop_blocktest_recycle(loop);
	t = loop->raw_set_timer_once(1000, [&](){ assert_unreachable(); });
	loop->enter();
	loop->raw_timer_remove(t);
	
	assert_eq(t2, 0);
	assert_eq(cstring(arrayview<uint8_t>(buf[0])), "HTTP/1.1");
	assert_eq(cstring(arrayview<uint8_t>(buf[1])), "HTTP/1.1");
	
	delete sock[0];
	delete sock[1];
	
	if (!is_global) delete loop;
}
test("runloop with sockets", "function,array,set,time", "runloop")
{
	test_skip("too slow");
	testcall(test_runloop_2(false));
	testcall(test_runloop_2(true));
}
#endif
#endif
