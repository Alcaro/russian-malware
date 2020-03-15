#pragma once
//this file requires <sys/socket.h> included
//this is not done here, to allow recvmsg/sendmsg to be redefined
//#include <sys/socket.h>

//kernel features that aren't used yet, but may be useful here, or the underlying process module:
//[5.0 / march 2019] SECCOMP_RET_USER_NOTIF - instead of SIGSYS, another process gets the syscall arguments
//  worth investigating, though the target process can't follow any pointers, so probably won't do much
//[5.1 / may 2019] pidfd_send_signal
//[5.2 / july 2019] clone(CLONE_PIDFD)
//[5.3 / september 2019] select() on pidfd, and reading exit status
//  these three would allow deleting the SIGCHLD handler, killing some nasty code and likely fixing a few corner cases
//[unmerged?] close_range() / close_from() / nextfd()
//  simplifies my closefrom(), current one is pretty ugly
//[unmerged?] CLONE_WAIT_PID
//  makes waitpid(-1) not care about that child, allowing use of GSubprocess
//[root-only] eBPF
//  moving some policy from broker to BPF would be an improvement
//  but to my knowledge, non-classic BPF is still true-root only (CLONE_NEWUSER isn't enough)
//  due to the 99999 Spectre variants, CLONE_NEWUSER or lower will likely never have access to eBPF
//[no patch exists] make execveat accept NULL as a blank string
//  that last mappable page hack is terrible
//[no patch exists] RESOLVE_BENEATH with seccomp-bpf
//  RESOLVE_BENEATH will improve performance by not involving broker for the vast majority of open()s
//  it's still open/openat/sigreturn, but it's way better than open/sendto/recvfrom/openat/sendmsg/recvmsg/sigreturn
//  can only be used for readonly open, max_write is mandatory
//  it was added to kernel 5.6 (unreleased) via openat2 - but its flags are in a struct, so I can't do anything
//[no patch exists] deep argument inspection for seccomp-bpf https://lwn.net/Articles/799557/
//  this would allow using RESOLVE_BENEATH, purging the last mappable page hack, and fix various other ugliness
//[unmerged] Landlock; an alternative to seccomp-bpf, somewhat higher level
//  seccomp-bpf is quite limited, and needs some weird tricks; perhaps a completely different approach would be better

//minimum kernel version policy is similar to minimum C++ version: it must work on latest Debian stable and Ubuntu LTS
//however, syscalls can be runtime tested with fallbacks; any released kernel is acceptable
//(but optional implifications don't really simplify anything, and mean one path won't be tested, so this should generally be avoided)
//currently, the minimum kernel is 4.6 (may 2016), to use CLONE_NEWCGROUP
//the maximum kernel feature used is also 4.6, nothing optional is used

//allowing openat() is currently risk-free, if used with AT_BENEATH
//pidfd is also safe, since openat() is blocked
//however, openat(pidfd, ...) could give access to various strange things, which must be prohibited
// RESOLVE_NO_MAGICLINKS and RESOLVE_BENEATH would most likely be sufficient
// alternatively, check if pidfd is still a /proc entry; if not, it's safe

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
