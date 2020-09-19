#ifdef _WIN32
#include "runloop.h"
#include "set.h"
#include "os.h"
#include "test.h"
#include <windows.h>

#if defined(ARLIB_GAME) || !defined(ARGUI_NONE)
#define ENABLE_MSGPUMP
void _window_process_events();
#endif

namespace {
class runloop_windows : public runloop {
public:
	bool exited = false;
#ifdef ENABLE_MSGPUMP
	bool is_global = false;
	runloop_windows(bool global = false) : is_global(global) {}
#else
	static const bool is_global = false;
#endif
	
	map<uintptr_t, function<void(HANDLE)>> events;
	
#ifdef ARLIB_THREAD
	HANDLE submit_event = NULL;
	HANDLE submit_read = NULL;
	HANDLE submit_write = NULL;
#endif
	
	struct timer_cb {
		unsigned id; // -1 if marked for removal
		unsigned ms;
		bool repeat;
		bool finished = false;
		uint64_t next;
		function<void()> cb;
	};
	array<timer_cb> timerinfo;
	
	void set_object(HANDLE h, function<void(HANDLE)> cb) override
	{
		if (cb) events.insert((uintptr_t)h, cb);
		else events.remove((uintptr_t)h);
		
		if (events.size() >= MAXIMUM_WAIT_OBJECTS-1) abort(); // TODO
		// MS recommendation is spawn threads whose only job is wait, or use thread pool
	}
	
	
	uintptr_t raw_set_timer_rel(unsigned ms, bool repeat, function<void()> callback) override
	{
		unsigned timer_id = 1;
		for (size_t i=0;i<timerinfo.size();i++)
		{
			if (timerinfo[i].id == (unsigned)-1) continue;
			if (timerinfo[i].id >= timer_id)
			{
				timer_id = timerinfo[i].id+1;
			}
		}
		
		timer_cb& timer = timerinfo.append();
		timer.repeat = repeat;
		timer.id = timer_id;
		timer.ms = ms;
		timer.cb = callback;
		timer.next = time_ms_ne() + ms;
		return timer_id;
	}
	
	void raw_timer_remove(uintptr_t id) override
	{
		if (id == 0) return;
		
		for (size_t i=0;i<timerinfo.size();i++)
		{
			if (timerinfo[i].id == id)
			{
				timerinfo[i].id = -1;
				return;
			}
		}
		abort(); // happens if that timer doesn't exist
	}
	
	void step(bool wait) override
	{
		uint64_t t_now = time_ms_ne();
		uint64_t t_next = (uint64_t)-1;
		
		for (size_t i=0;i<timerinfo.size();i++)
		{
		again: ;
			timer_cb& timer = timerinfo[i];
			
			if (timer.id == (unsigned)-1)
			{
				timerinfo.remove(i);
				
				//funny code to avoid (size_t)-1 being greater than timerinfo.size()
				if (i == timerinfo.size()) break;
				goto again;
			}
			
			if (timer.finished) continue;
			
			if (timer.next <= t_now)
			{
				timer.next = t_now + timer.ms;
				
				timer.cb(); // WARNING: May invalidate 'timer'. timerinfo[i] remains valid.
				wait = false; // ensure it returns quickly, since it did something
				if (!timerinfo[i].repeat) timerinfo[i].finished = true;
			}
			
			if (timerinfo[i].next < t_next) t_next = timerinfo[i].next;
		}
		
		uint32_t delay;
		if (!wait) delay = 0;
		else if (t_next == (uint64_t)-1) delay = INFINITE;
		else delay = t_next-t_now;
		
#ifndef ARLIB_OPT
		if (wait && events.size() == 0 && delay == INFINITE && !is_global)
		{
#ifdef ARLIB_TESTRUNNER
			assert(!"runloop is empty and will block forever");
#else
			abort();
#endif
		}
#endif
		
		unsigned n = 0;
		HANDLE handles[MAXIMUM_WAIT_OBJECTS];
		for (auto& pair : events)
			handles[n++] = (HANDLE)pair.key;
		
		uint32_t wait_ret;
#ifdef ENABLE_MSGPUMP
		if (is_global)
		{
			wait_ret = MsgWaitForMultipleObjectsEx(n, handles, delay, QS_ALLEVENTS, MWMO_INPUTAVAILABLE);
		}
		else
#endif
		{
			if (n == 0) { wait_ret = WAIT_TIMEOUT; Sleep(delay); }
			else wait_ret = WaitForMultipleObjects(n, handles, false, delay);
		}
		
		if (wait_ret != WAIT_TIMEOUT)
		{
#ifdef ENABLE_MSGPUMP
			if (wait_ret == n)
				_window_process_events();
			else
#endif
				events[(uintptr_t)handles[wait_ret]](handles[wait_ret]);
		}
	}
	
#ifdef ARLIB_THREAD
	void prepare_submit() override
	{
		if (submit_event) return;
		submit_event = CreateEvent(NULL, false, false, NULL);
		if (!CreatePipe(&submit_read, &submit_write, NULL, 0)) abort();
		this->set_object(submit_event, [this](HANDLE) {
				function<void()> cb;
				DWORD bytes;
				while (PeekNamedPipe(submit_read, NULL, 0, NULL, &bytes, NULL) && bytes != 0)
				{
					if (!ReadFile(submit_read, &cb, sizeof(cb), &bytes, NULL) || bytes != sizeof(cb)) abort();
					cb();
				}
			});
	}
	void submit(function<void()>&& cb) override
	{
		DWORD bytes;
		if (!WriteFile(submit_write, &cb, sizeof(cb), &bytes, NULL) || bytes != sizeof(cb)) abort();
		memset(&cb, 0, sizeof(cb));
		SetEvent(submit_event);
	}
#endif
	
	void enter() override
	{
		exited = false;
		while (!exited) step(true);
	}
	
	void exit() override
	{
		exited = true;
	}
	
	~runloop_windows()
	{
#ifdef ARLIB_TESTRUNNER
		assert_empty();
#endif
#ifdef ARLIB_THREAD
		if (submit_event)
		{
			CloseHandle(submit_event);
			CloseHandle(submit_read);
			CloseHandle(submit_write);
		}
#endif
	}
	
#ifdef ARLIB_TESTRUNNER
	void assert_empty() override
	{
	again:
		for (auto& pair : events)
		{
#ifdef ARLIB_THREAD
			if ((HANDLE)pair.key == submit_event) continue;
#endif
			test_nothrow {
				test_fail("object left in runloop");
			}
			set_object((HANDLE)pair.key, nullptr);
			goto again;
		}
		while (timerinfo.size())
		{
			if (timerinfo[0].id != (unsigned)-1)
			{
				test_nothrow {
					test_fail("timer left in runloop");
				}
			}
			timerinfo.remove(0);
		}
	}
#endif
};
}

runloop* runloop::create()
{
	return runloop_wrap_blocktest(new runloop_windows());
}

runloop* runloop::global()
{
	static runloop* ret;
	if (!ret) ret = runloop_wrap_blocktest(new runloop_windows(
#ifdef ENABLE_MSGPUMP
		true
#endif
	));
	return ret;
}
#endif
