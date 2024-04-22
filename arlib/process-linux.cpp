#include "process.h"

#ifdef __linux__
#include "os.h"
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/sched.h>
#include <linux/wait.h>
//#include <signal.h>
//#include <errno.h>
//#include <unistd.h>

//Sets the file descriptor table to 'fds', closing all other fds.
//If an entry is -1, the corresponding fd is closed. Duplicates in the input are allowed.
//Returns false on failure, but keeps doing its best anyways.
//Will mangle the input array. While suboptimal, it's the only way to avoid a post-fork malloc.
//The CLOEXEC flag is set to 'cloexec' on all remaining fds.
static bool set_fds(arrayvieww<int> fds, bool cloexec = false)
{
	if (fds.size() > INT_MAX) return false;
	
	bool ok = true;
	
	//probably doable with fewer dups, but yawn, don't care.
	for (size_t i=0;i<fds.size();i++)
	{
		while ((unsigned)fds[i] < i && fds[i] >= 0)
		{
			fds[i] = fcntl(fds[i], F_DUPFD_CLOEXEC);
			if (fds[i] < 0) ok = false;
		}
	}
	
	for (size_t i=0;i<fds.size();i++)
	{
		if (fds[i] >= 0)
		{
			if (fds[i] != (int)i)
			{
				if (dup3(fds[i], i, O_CLOEXEC) < 0)
				{
					ok = false;
					close(i);
				}
			}
			fcntl(i, F_SETFD, cloexec ? FD_CLOEXEC : 0);
		}
		if (fds[i] < 0) close(i);
	}
	
	return (close_range(fds.size(), UINT_MAX, 0) == 0);
}

static int process_wait_sync(fd_t& fd);

int process::create(raw_params& param)
{
	// exec resets the child termination signal to SIGCHLD,
	// and kernel pretends to not know what the pidfd points to if the signal was zero
	// I could also use the vfork flags, but I can't quite determine what exactly is legal after vfork
	// I'd probably need to rewrite this function in assembly to make it safe (though I could put the pid==0 case in a C++ function)
#ifdef __x86_64__
	pid_t pid = syscall(__NR_clone, CLONE_PIDFD|SIGCHLD, NULL, (fd_raw_t*)&this->fd, NULL, NULL);
#endif
	// on FreeBSD, the equivalent is pdfork
	if (pid < 0)
		return 0;
	if (pid == 0)
	{
		// in child process
		
		if (param.detach)
		{
			if (fork() != 0)
				_exit(0);
		}
		
		//WARNING:
		//fork(), POSIX.1-2008, http://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html
		//  If a multi-threaded process calls fork(), the new process shall contain a replica of the
		//  calling thread and its entire address space, possibly including the states of mutexes and
		//  other resources. Consequently, to avoid errors, the child process may only execute
		//  async-signal-safe operations until such time as one of the exec functions is called.
		//In particular, malloc must be avoided.
		
		if (!set_fds(param.fds))
			_exit(EXIT_FAILURE);
		const char * const * envp = param.envp ? param.envp : __environ;
		for (const char * prog : param.progs)
			execve(prog, (char**)param.argv, (char**)envp); // why are these not properly const declared
		while (true)
			_exit(EXIT_FAILURE);
	}
	if (param.detach)
	{
		process_wait_sync(this->fd);
		return 0;
	}
	return pid;
}

static bool process_try_wait(fd_t& fd, int& ret, bool async)
{
	siginfo_t si;
	// si.si_pid = 0; // unnecessary on Linux (needed on other Unix)
	// todo: delete cast, and include of <linux/wait.h>, when dropping ubuntu 22.04
	waitid((idtype_t)P_PIDFD, (int)fd, &si, WEXITED|WSTOPPED|WCONTINUED|(async?WNOHANG:0));
	if (si.si_pid != 0 && (si.si_code == CLD_EXITED || si.si_code == CLD_DUMPED))
	{
		fd.close();
		ret = si.si_status;
		return true;
	}
	return false;
}

static int process_wait_sync(fd_t& fd)
{
	while (true)
	{
		int ret;
		if (process_try_wait(fd, ret, false))
			return ret;
	}
}

async<int> process::wait()
{
	if (!fd.valid())
		co_return -1;
	while (true)
	{
		co_await runloop2::await_read(fd);
		int ret;
		if (process_try_wait(fd, ret, true))
			co_return ret;
	}
}

void process::kill()
{
	if (!fd.valid())
		return;
	syscall(SYS_pidfd_send_signal, (int)fd, SIGKILL, nullptr, 0);
	process_wait_sync(fd);
}

process::pipe process::pipe::create()
{
	int fds[2] = { -1, -1 };
	if (pipe2(fds, O_CLOEXEC) != 0)
		debug_fatal("pipe2() failed\n");
	return { fds[0], fds[1] };
}

#ifndef __GLIBC__
static char* strchrnul(const char * s, int c)
{
	const char * ret = strchr(s, c);
	if (!ret) ret = s + strlen(s);
	return (char*)s;
}
#endif

int process::create(params& param)
{
	array<const char *> argv;
	for (const string& s : param.argv)
		argv.append(s);
	if (!param.argv)
		argv.append(param.prog);
	argv.append(NULL);
	
	array<const char *> envp;
	for (const string& s : param.envp)
		envp.append(s);
	envp.append(NULL);
	
	array<string> progs;
	
	if (param.prog.contains("/"))
		progs.append(param.prog);
	else
	{
		const char * path = getenv("PATH");
		if (!path) path = "/bin"; // just hardcode something, PATH being absent is crazy anyways
		while (true)
		{
			const char * end = strchrnul(path, ':');
			progs.append(cstring(bytesr((uint8_t*)path, end-path))+"/"+param.prog);
			if (*end)
				path = end+1;
			else
				break;
		}
	}
	
	if (param.fds.size() < 3)
	{
		if (param.fds.size() == 0)
			param.fds.append(0);
		if (param.fds.size() == 1)
			param.fds.append(1);
		param.fds.append(2);
	}
	
	array<const char *> progs_ptrs;
	for (const char * prog : progs)
		progs_ptrs.append(prog);
	
	struct raw_params rparam;
	rparam.progs = progs_ptrs;
	rparam.argv = argv.ptr();
	if (param.envp)
		rparam.envp = envp.ptr();
	rparam.fds = param.fds;
	rparam.detach = param.detach;
	
	return create(rparam);
}

co_test("process", "array,string", "process")
{
	{
		process p;
		assert(p.create({ "true", { "/bin/true" } }));
		assert_eq(co_await p.wait(), 0);
	}
	{
		process p;
		assert(p.create({ "false", { "/bin/false" } }));
		assert_eq(co_await p.wait(), 1);
	}
	{
		process p;
		assert(p.create({ "program that doesn't exist", { "a" } }));
		assert_eq(co_await p.wait(), EXIT_FAILURE);
	}
	{
		process().create({ .prog="program that doesn't exist", .argv={ "a" }, .detach=true });
	}
}
#endif
