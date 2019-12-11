#ifdef __linux__
#include "sandbox.h"
#include "../process.h"

#include <sys/syscall.h>
#ifdef __NR_execveat // force disable on old kernels

#include <sys/user.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <linux/memfd.h> // documented as sys/memfd.h, but that doesn't exist

//#include <linux/ioprio.h> // ioprio_set - header doesn't exist for me, copying the content
//I believe they count as userspace ABI, i.e. this is fine

#define IOPRIO_CLASS_SHIFT	(13)
#define IOPRIO_PRIO_MASK	((1UL << IOPRIO_CLASS_SHIFT) - 1)
#define IOPRIO_PRIO_CLASS(mask)	((mask) >> IOPRIO_CLASS_SHIFT)
#define IOPRIO_PRIO_DATA(mask)	((mask) & IOPRIO_PRIO_MASK)
#define IOPRIO_PRIO_VALUE(class, data)	(((class) << IOPRIO_CLASS_SHIFT) | data)

enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE,
};

enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER,
};


#include <fcntl.h>
#ifndef F_ADD_SEALS
#define F_LINUX_SPECIFIC_BASE 1024
#define F_ADD_SEALS   (F_LINUX_SPECIFIC_BASE + 9)  // and these only exist in linux/fcntl.h - where fcntl() doesn't exist
#define F_GET_SEALS   (F_LINUX_SPECIFIC_BASE + 10) // and I can't include both, duplicate definitions
#define F_SEAL_SEAL   0x0001
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW   0x0004
#define F_SEAL_WRITE  0x0008
#endif
#include <sys/prctl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <linux/audit.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>

#include "internal-linux-sand.h"

/*
//ensure a AF_UNIX SOCK_SEQPACKET socketpair can't specify dest_addr
//(turns out it can, but address is ignored. good enough)
void stest()
{
	//const int type = SOCK_DGRAM;
	const int type = SOCK_SEQPACKET;
	//const int type = SOCK_STREAM;
	
	int socks[2];
	socketpair(AF_UNIX, type, 0, socks);
	
	struct sockaddr_un ga = { AF_UNIX, "socket" };
	int gal = offsetof(sockaddr_un,sun_path) + 7;
	int g = socket(AF_UNIX, type, 0);
	errno=0;
	bind(g, (sockaddr*)&ga, gal);
	perror("bind");
	listen(g, 10);
	perror("listen");
	
	struct sockaddr_un ga2 = { AF_UNIX, "\0socket" };
	int ga2l = offsetof(sockaddr_un,sun_path) + 8;
	int g2 = socket(AF_UNIX, type, 0);
	errno=0;
	bind(g2, (sockaddr*)&ga2, ga2l);
	perror("bind");
	listen(g2, 10);
	perror("listen");
	
	//int g3 = socket(AF_UNIX, type, 0);
	int g3 = socks[1];
	errno=0;
	sendto(g3, "foo",3, 0, (sockaddr*)&ga, gal);
	perror("sendto");
	errno=0;
	sendto(g3, "bar",3, 0, (sockaddr*)&ga2, ga2l);
	perror("sendto");
	
	while (true)
	{
		char out[6];
		int n = recv(socks[0], out, 6, MSG_DONTWAIT);
		if (n<0) break;
		n = write(1, out, n);
	}
	
	close(socks[0]);
	close(socks[1]);
	//close(g);
	//close(g2);
	if (g3!=socks[1]) close(g3);
	unlink("socket");
}
// */


template<typename T> inline T require(T x)
{
	//failures are easiest debugged with strace
	if ((long)x == -1) _exit(1);
	return x;
}
//this could be an overload, but for security-critical code, explicit is good
inline void require_b(bool expected)
{
	if (!expected) _exit(1);
}
template<typename T> inline T require_eq(T actual, T expected)
{
	if (actual != expected) _exit(1);
	return actual;
}


extern const char sandbox_preload_bin[];
extern const unsigned sandbox_preload_len;

int sandproc::preloader_fd()
{
	static int s_fd = 0;
	if (s_fd) return s_fd;
	if (lock_read_loose(&s_fd)) return s_fd;
	
	int fd = syscall(__NR_memfd_create, "arlib-sand-preload", MFD_CLOEXEC|MFD_ALLOW_SEALING);
	if (fd < 0)
		goto fail;
	if (write(fd, sandbox_preload_bin, sandbox_preload_len) != sandbox_preload_len)
		goto fail;
	if (fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE|F_SEAL_SHRINK|F_SEAL_GROW|F_SEAL_SEAL) < 0)
		goto fail;
	int prev;
	prev = lock_cmpxchg(&s_fd, 0, fd);
	if (prev != 0)
	{
		close(fd);
		return prev;
	}
	return fd;
	
fail:
	if (fd >= 0) close(fd);
	return -1;
}

static bool install_seccomp()
{
	static const struct sock_filter filter[] = {
		#include "bpf.inc"
	};
	static_assert(ARRAY_SIZE(filter) < 65536);
	static const struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = (sock_filter*)filter,
	};
	require(prctl(PR_SET_NO_NEW_PRIVS, 1, 0,0,0));
	require(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog));
	return true;
}


// no glibc wrapper
static int execveat(int dirfd, const char * pathname, char * const argv[], char * const envp[], int flags)
{
	return syscall(__NR_execveat, dirfd, pathname, argv, envp, flags);
}


pid_t sandproc::launch_impl(const char * program, array<const char*> argv, array<int> stdio_fd)
{
	// can't override argv[0] in sandbox, ld-linux won't understand that
	// if you need a fake argv[0], redirect the chosen executable via sandbox policy
	if (program != argv[0])
		return -1;
	
	argv.insert(0, "[arlib-sandbox]"); // ld-linux thinks it's argv[0] and discards the real one
	
	//fcntl is banned by seccomp, so this goes early
	//putting it before clone() allows sharing it between sandbox children
	int preld_fd = preloader_fd();
	if (preld_fd < 0)
		return -1;
	
	int socks[2];
	if (socketpair(AF_UNIX, SOCK_SEQPACKET|SOCK_CLOEXEC, 0, socks)<0)
		return -1;
	
	stdio_fd.append(socks[1]);
	stdio_fd.append(preld_fd); // we could request preld from parent, but this is easier
	
	int clone_flags = CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWCGROUP | CLONE_NEWIPC | CLONE_NEWNS | CLONE_NEWUTS;
	clone_flags |= SIGCHLD; // termination signal, must be SIGCHLD for waitpid to work properly
	pid_t pid = syscall(__NR_clone, clone_flags, NULL, NULL, NULL, NULL);
	if (pid < 0) return -1;
	if (pid > 0)
	{
		mainsock = socks[0];
		watch_add(socks[0]);
		close(socks[1]);
		return pid;
	}
	
	//WARNING:
	//fork(), POSIX.1-2008, http://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html
	//  If a multi-threaded process calls fork(), the new process shall contain a replica of the
	//  calling thread and its entire address space, possibly including the states of mutexes and
	//  other resources. Consequently, to avoid errors, the child process may only execute
	//  async-signal-safe operations until such time as one of the exec functions is called.
	//This applies to clone() too. In particular, malloc must be avoided. Syscalls and stack is fine.
	
	//we're the child
	
	//some of these steps depend on each other, don't swap them randomly
	
	require_b(set_fds(stdio_fd));
	
	struct rlimit rlim_fsize = { 8*1024*1024, 8*1024*1024 };
	require(setrlimit(RLIMIT_FSIZE, &rlim_fsize));
	
	//CLONE_NEWUSER doesn't seem to grant access to cgroups
	//once (if) it does, set these:
	// memory.memsw.limit_in_bytes = 100*1024*1024
	// cpu.cfs_period_us = 100*1000, cpu.cfs_quota_us = 50*1000
	// pids.max = 30
	//for now, just stick up some rlimit rules, to disable the most naive forkbombs or memory wastes
	
	struct rlimit rlim_as = { 1*1024*1024*1024, 1*1024*1024*1024 }; // this is the only one that affects mmap
	require(setrlimit(RLIMIT_AS, &rlim_as)); // keep the value synced with sysinfo() in sysemu
	
	//why so many? because the rest of the root-namespace user is also included, which is often a few hundred
	//http://elixir.free-electrons.com/linux/latest/source/kernel/fork.c#L1564
	struct rlimit rlim_nproc = { 500, 500 };
	require(setrlimit(RLIMIT_NPROC, &rlim_nproc));
	
	//nice value
	//PRIO_PROCESS,0 - calling thread
	//19 - lowest possible priority
	require(setpriority(PRIO_PROCESS,0, 19));
	//ionice
	//IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0) - lowest priority; the 0 is unused as of kernel 4.16
	require(syscall(__NR_ioprio_set, IOPRIO_WHO_PROCESS,0, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0)));
	
	//die on parent death
	//('parent' is parent thread, not the entire process, but Arlib process objects have thread affinity anyways)
	require(prctl(PR_SET_PDEATHSIG, SIGKILL));
	
	//ensure parent is still alive
	//have to check for a response, it's possible that parent died before the prctl but its socket isn't deleted yet
	struct broker_req req = { br_ping };
	require_eq(send(3, &req, sizeof(req), MSG_NOSIGNAL|MSG_EOR), (ssize_t)sizeof(req));
	struct broker_rsp rsp;
	require_eq(recv(3, &rsp, sizeof(rsp), MSG_NOSIGNAL), (ssize_t)sizeof(rsp));
	//discard the response, we know it's { br_ping, {0,0,0}, "" }, we only care whether we got one at all
	
	//revoke filesystem
	require(chroot("/proc/sys/debug/"));
	require(chdir("/"));
	
	require_eq(install_seccomp(), true);
	
	static const char * const new_envp[] = {
		"TERM=xterm", // some programs check this to know whether they can color, some check ioctl(TCGETS), some check both
		"PATH=/usr/bin:/bin",
		"TMPDIR=/tmp",
		"LANG=en_US.UTF-8",
		NULL
	};
	
	//0x00007FFF'FFFFF000 isn't mappable, apparently sticking SYSCALL (0F 05) at 0x00007FFF'FFFFFFFE
	// will return to an invalid address and blow up
	//http://elixir.free-electrons.com/linux/v4.11/source/arch/x86/include/asm/processor.h#L832
	//doesn't matter what the last page is, as long as there is one
	//make sure to edit the BPF rules if changing this
	//(fair chance it's gonna break once five-level paging shows up)
	char* final_page = (char*)0x00007FFFFFFFE000;
	require_eq(mmap(final_page+0x1000, 0x1000, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0), MAP_FAILED);
	require_eq(mmap(final_page,        0x1000, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0), (void*)final_page);
	require(execveat(4, final_page+0xFFF, (char**)argv.ptr(), (char**)new_envp, AT_EMPTY_PATH));
	
	_exit(1); // execve never returns nonnegative, and require never returns from negative, but gcc knows neither
}
#endif
#endif
