#if defined(__unix__)
#include "thread.h"
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <new>

//list of synchronization points: http://pubs.opengroup.org/onlinepubs/009695399/basedefs/xbd_chap04.html#tag_04_10

#ifdef __linux__disabled // I'm pretty sure putting the parent thread to sleep is more expensive than a cross-thread malloc/free
struct threaddata_linux {
	function<void()> func;
	int futex;
};
static void * threadproc(void * userdata)
{
	threaddata_linux* thdat = (threaddata_linux*)userdata;
	function<void()> func = std::move(thdat->func);
	futex_set_and_wake(&thdat->futex, 1);
	func();
	return NULL;
}

static void thread_create_inner(function<void()>&& start, pthread_attr_t* attr)
{
	threaddata_linux thdat { std::move(start), 0 };
	pthread_t thread;
	if (pthread_create(&thread, attr, threadproc, &thdat) != 0) abort();
	pthread_detach(thread);
	futex_wait_while_eq(&thdat.futex, 0);
}

#else

struct threaddata_pthread {
	function<void()> func;
};
static void * threadproc(void * userdata)
{
	threaddata_pthread* thdat = (threaddata_pthread*)userdata;
	function<void()> func = std::move(thdat->func);
	free(thdat);
	func();
	return NULL;
}

static void thread_create_inner(function<void()>&& start, pthread_attr_t* attr)
{
	threaddata_pthread* thdat = xmalloc(sizeof(threaddata_pthread));
	new(&thdat->func) function<void()>(std::move(start));
	
	pthread_t thread;
	if (pthread_create(&thread, attr, threadproc, thdat) != 0) abort();
	pthread_detach(thread);
}
#endif

void thread_create(function<void()>&& start, priority_t pri)
{
	pthread_attr_t attr;
	if (pthread_attr_init(&attr) < 0) abort();
	
	sched_param sched;
	if (pthread_attr_getschedparam(&attr, &sched) >= 0)
	{
		static const int8_t prios[] = { 0, -10, 10, 19 };
		sched.sched_priority = prios[pri];
		pthread_attr_setschedparam(&attr, &sched);
	}
	
	thread_create_inner(std::move(start), &attr);
	pthread_attr_destroy(&attr);
}

mutex_rec::mutex_rec() : mutex(noinit())
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&mut, &attr);
	pthread_mutexattr_destroy(&attr);
}

unsigned int thread_num_cores()
{
	//for more OSes: https://code.woboq.org/qt5/qtbase/src/corelib/thread/qthread_unix.cpp.html#_ZN7QThread16idealThreadCountEv
	//or https://stackoverflow.com/questions/150355/programmatically-find-the-number-of-cores-on-a-machine
	return sysconf(_SC_NPROCESSORS_ONLN);
}

unsigned int thread_num_cores_idle()
{
	//this function should return physical core count, or cores minus 1 if no hyperthreading,
	// but there doesn't seem to be an easy way to get number of real cores
	//so this is good enough
	unsigned int cores = thread_num_cores();
	return (cores+1)/2;
}
#endif
