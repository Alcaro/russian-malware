#pragma once
//this file requires <sys/socket.h> included
//this is not done here, to allow recvmsg/sendmsg to be redefined
//#include <sys/socket.h>

//kernel features that aren't used yet, but may be useful:
//[5.1 / april 2019] pidfd_send_signal
//[5.2 / unreleased] clone(CLONE_PIDFD)
//[no patch?] select() on pidfd, and reading exit status
//  these three would allow deleting the SIGCHLD handler, killing some nasty code and likely fixing a few corner cases
//[5.3? / unmerged] close_range() / close_from() / nextfd()
//  simplifies my closefrom(), current one is pretty ugly
//[unmerged] AT_BENEATH
//  can only be used for readonly open, max_write is mandatory
//  this will improve performance by not involving broker for the vast majority of open()s
//  it's still open/openat/sigreturn, but it's way better than open/sendto/recvfrom/openat/sendmsg/recvmsg/sigreturn
//[unavailable] various BPF thingies
//  moving some policy from broker to BPF would be an improvement
//  but to my knowledge, non-classic BPF is still root only (CLONE_NEWUSER isn't enough)

//minimum kernel version policy is similar to minimum C++ version: it must work on latest Debian stable and Ubuntu LTS
//however, syscalls can be runtime tested with fallbacks; any released kernel is acceptable
//currently, the minimum kernel is 4.6 (may 2016), to use CLONE_NEWCGROUP
//the maximum kernel feature used is 4.6, there's nothing optional

//openat() is currently risk-free, if used with AT_BENEATH
//however, only one of openat() and pidfd is acceptable

enum broker_req_t {
	br_nop,       // [req only] does nothing, doesn't respond (unused)
	br_ping,      // does nothing, sends an empty response (used by launcher to check if parent is alive)
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
//so I'll just copypaste it
#undef CMSG_NXTHDR
static inline struct cmsghdr * CMSG_NXTHDR(struct msghdr * msgh, struct cmsghdr * cmsg)
{
	if (cmsg->cmsg_len < sizeof(struct cmsghdr))
		/* The kernel header does this so there may be a reason.  */
		return NULL;
	
	cmsg = (cmsghdr*)((uint8_t*)cmsg + CMSG_ALIGN(cmsg->cmsg_len));
	if ((uint8_t*)(cmsg+1) > ((uint8_t*)msgh->msg_control + msgh->msg_controllen) ||
		((uint8_t*)cmsg + CMSG_ALIGN(cmsg->cmsg_len) > ((uint8_t*)msgh->msg_control + msgh->msg_controllen)))
		/* No more entries.  */
		return NULL;
	
	return cmsg;
}

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
