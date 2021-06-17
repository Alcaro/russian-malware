#pragma once
//this file requires <sys/socket.h> included
//this is not done here, to allow recvmsg/sendmsg to be redefined
//#include <sys/socket.h>

//kernel features that aren't used yet, but may be useful here, or the underlying process module:
//[4.17 / june 2018] MAP_FIXED_NOREPLACE, so the last page can be mapped early, ensuring it's not used during execve
//  (can't unconditionally replace that page; if ASLR put the bottom of the stack there, shredding that would be inadvisable)
//[5.1 / may 2019] pidfd_send_signal
//[5.2 / july 2019] clone(CLONE_PIDFD)
//[5.3 / september 2019] select() on pidfd, and reading exit status
//  these three would allow deleting the SIGCHLD handler, killing some nasty code and likely fixing a few corner cases
//[5.9 / october 2020] close_range()
//  simplifies my closefrom(), current one is pretty ugly
//[5.10 / december 2020] nonblocking pidfd (maybe? not sure)
//[5.13 / june 2021?] Landlock, another sandboxing mechanism
//  Landlock is not a complete sandbox in 5.13, but it's a good start, and it seems to complement seccomp-bpf quite well
//  more specifically, banning FS_READ_DIR on / should restrict the filesystem well enough that I don't need the last mappable page hack,
//    and it may also offer a variant of RESOLVE_BENEATH (not sure)
//[not usable in its current state] RESOLVE_BENEATH
//  RESOLVE_BENEATH will improve performance by not involving broker for the vast majority of open()s
//  it's still open/openat/sigreturn, but it's way better than open/sendto/recvfrom/openat/sendmsg/sigreturn
//  can only be used for readonly open, max_write is mandatory
//  it was added to kernel 5.6 (march 2020) via openat2 - but its flags are in a struct, so I can't do anything
//[no patch exists] a way to block all filesystem access (syscall or prctl, or chroot to a directory guaranteed to exist and be empty)
//  so I can delete that last mappable page hack in execveat, and stop worrying about other filesystem operations
//  (openat would still exist, but that's easy to seccomp off)
//[no patch exists] deep argument inspection for seccomp-bpf https://lwn.net/Articles/799557/
//  this would allow using RESOLVE_BENEATH, as well as purging the last mappable page hack, and probably fix some other ugliness
//[no patch exists] make execveat treat NULL as a blank string
//  yet another way to delete that last mappable page hack
//[no patch exists] pidfd for SECCOMP_RET_USER_NOTIF and process_vm_readv
//  would change open/sendto/recvfrom/openat/sendmsg/sigreturn to open/usernotif/openat/vm_readv/usernotif-return
//  the current pid-based mechanism allows leaking the address space of unsandboxed processes, if the pid is reused
//  could also be solved with an unsandboxed broker in the child's pid namespace,
//    but the extra process may cost more performance than the removed syscalls save
//[not usable in its current state] eBPF
//  moving some policy from broker to BPF would be an improvement
//  but due to the 99999 Spectre variants, CLONE_NEWUSER or lower will likely never have access to eBPF; needs true root
//[unmerged] CLONE_WAIT_PID
//  makes waitpid(-1) not care about that child, allowing use of GSubprocess
//some of the above are multiple ways to accomplish the same goal; duplicates will be kept until any solution is implemented

//currently, the minimum kernel is 4.6 (may 2016), to use CLONE_NEWCGROUP
//the maximum kernel feature used is also 4.6, nothing optional is used

//allowing openat() is currently risk-free, if used with RESOLVE_BENEATH
//giving child access to pidfd is also safe, since openat() is blocked
//however, openat(pidfd, ...) could give access to various strange things, which must be prohibited
//  probably harmless with RESOLVE_NO_MAGICLINKS
//  alternatively, check if pidfd is still a /proc entry; if not, it's safe

enum broker_req_t {
	br_nop,       // [req only] does nothing, doesn't respond (unused)
	br_ping,      // does nothing, sends an empty response (used by launcher to check if parent is alive, after setting PDEATHSIG)
	br_open,      // open(req.path, req.flags[0], req.flags[1])
	br_unlink,    // flags unused
	br_access,    // access(req.path, req.flags[0])
	br_get_emul,  // returns the fd of the emulator, path and flags unused
	br_fork,      // returns a new fd equivalent to the existing broker fd, to be used in fork()
	br_shmem,     // for sandbox-aware children: returns a memfd, for sharing memory
};

//static_assert(O_RDONLY==0);
//static_assert(O_WRONLY==1);
//static_assert(O_RDWR==2);
//static_assert(O_ACCMODE==3);

#define SAND_PATHLEN 260 // same as Windows MAX_PATH, anything longer than this probably isn't useful

struct broker_req {
	enum broker_req_t type;
	uint32_t flags[3];
	char path[SAND_PATHLEN];
};
struct broker_rsp {
	enum broker_req_t type;
	int err;
};

//CMSG_NXTHDR is a function in glibc, we can't do that
//there's an inline version in the headers, but I can't find a reliable way to enforce its use
//copying from musl instead
#undef CMSG_NXTHDR
#define __MHDR_END(mhdr) ((unsigned char *)(mhdr)->msg_control + (mhdr)->msg_controllen)
#define __CMSG_LEN(cmsg) (((cmsg)->cmsg_len + sizeof(long) - 1) & ~(long)(sizeof(long) - 1))
#define __CMSG_NEXT(cmsg) ((unsigned char *)(cmsg) + __CMSG_LEN(cmsg))
#define CMSG_NXTHDR(mhdr, cmsg) ((cmsg)->cmsg_len < sizeof (struct cmsghdr) ? (struct cmsghdr *)0 : \
        (__CMSG_NEXT(cmsg) + sizeof (struct cmsghdr) >= __MHDR_END(mhdr) ? (struct cmsghdr *)0 : \
        ((struct cmsghdr *)__CMSG_NEXT(cmsg))))

//from http://blog.varunajayasiri.com/passing-file-descriptors-between-processes-using-sendmsg-and-recvmsg
//somewhat reformatted
static inline ssize_t send_fd(int sockfd, const void * buf, size_t len, int flags, int fd)
{
	//need at least one byte of data, otherwise recvmsg gets angry
	struct iovec iov = { (void*)buf, len };
	char ctrl_buf[CMSG_SPACE(sizeof(int))] = {};
	struct msghdr message = {
		.msg_name = NULL, .msg_namelen = 0,
		.msg_iov = &iov, .msg_iovlen = 1,
		.msg_control = ctrl_buf, .msg_controllen = (fd>=0 ? sizeof(ctrl_buf) : 0),
		.msg_flags = 0,
	};
	
	if (fd >= 0)
	{
		cmsghdr* ctrl_msg = CMSG_FIRSTHDR(&message);
		ctrl_msg->cmsg_level = SOL_SOCKET;
		ctrl_msg->cmsg_type = SCM_RIGHTS;
		ctrl_msg->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(ctrl_msg), &fd, sizeof(int));
	}
	
	return sendmsg(sockfd, &message, flags);
}

static inline ssize_t recv_fd(int sockfd, void * buf, size_t len, int flags, int* fd)
{
	struct iovec iov = { buf, len };
	char ctrl_buf[CMSG_SPACE(sizeof(int))] = {};
	struct msghdr message = {
		.msg_name = NULL, .msg_namelen = 0,
		.msg_iov = &iov, .msg_iovlen = 1,
		.msg_control = ctrl_buf, .msg_controllen = sizeof(ctrl_buf),
		.msg_flags = 0,
	};
	
	ssize_t ret = recvmsg(sockfd, &message, flags);
	
	*fd = -1;
	for (cmsghdr* ctrl_msg=CMSG_FIRSTHDR(&message); ctrl_msg!=NULL; ctrl_msg=CMSG_NXTHDR(&message, ctrl_msg))
	{
		if (ctrl_msg->cmsg_level == SOL_SOCKET && ctrl_msg->cmsg_type == SCM_RIGHTS)
		{
			memcpy(fd, CMSG_DATA(ctrl_msg), sizeof(int));
		}
	}
	
	return ret;
}
