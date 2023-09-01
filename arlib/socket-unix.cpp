#if defined(ARLIB_SOCKET) && defined(__unix__)
#include "socket.h"
#undef socket

#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

static int mksocket(int domain, int type, int protocol)
{
	return socket(domain, type, protocol);
}

namespace {

static int fixret(int ret)
{
	if (ret > 0) return ret;
	if (ret == 0) { errno = ESHUTDOWN; return -1; }
	if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
	return -1;
}

static int my_setsockopt(int fd, int level, int option_name, const void * option_value, socklen_t option_len)
{
	return setsockopt(fd, level, option_name, option_value, option_len);
}
static int my_setsockopt(int fd, int level, int option_name, int option_value)
{
	return my_setsockopt(fd, level, option_name, &option_value, sizeof(option_value));
}
#define setsockopt my_setsockopt

static void configure_sock(int fd)
{
	//because 30 second pauses are unequivocally detestable
	timeval timeout;
	timeout.tv_sec = 4;
	timeout.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout)); // strace calls this SO_SNDTIMEO_OLD, can't find any docs
	// TODO: do I need the above at all? It made sense when connect() was blocking, but it no longer is.
	
	// Nagle's algorithm - made sense in the 80s, but these days, the saved bandwidth isn't worth the added latency.
	// Better combine stuff in userspace, syscalls are (relatively) expensive these days.
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, true);
	
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, 1); // enable
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, 3); // ping count before the kernel gives up
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, 30); // seconds idle until it starts pinging
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, 10); // seconds per ping once the pings start
}

//MSG_DONTWAIT is usually better, but accept() and connect() don't take that argument
static void MAYBE_UNUSED setblock(int fd, bool newblock)
{
	int flags = fcntl(fd, F_GETFL, 0);
	flags &= ~O_NONBLOCK;
	if (!newblock) flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
}

class socket2_impl : public socket2 {
public:
	int fd;
	socket2_impl(int fd) : fd(fd) {}
	ssize_t recv_sync(bytesw by) override { assert(by); return fixret(::recv(fd, (char*)by.ptr(), by.size(), MSG_DONTWAIT|MSG_NOSIGNAL)); }
	ssize_t send_sync(bytesr by) override { assert(by); return fixret(::send(fd, (char*)by.ptr(), by.size(), MSG_DONTWAIT|MSG_NOSIGNAL)); }
	async<void> can_recv() override { return runloop2::await_read(fd); }
	async<void> can_send() override { return runloop2::await_write(fd); }
	int get_fd() override { return fd; }
	~socket2_impl() { close(fd); }
};
}

async<autoptr<socket2>> socket2::create(address addr)
{
	if (!addr)
		co_return nullptr;
	
	int fd = mksocket(addr.as_native()->sa_family, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0);
	if (fd < 0)
		co_return nullptr;
	
	configure_sock(fd);
	
	if (connect(fd, addr.as_native(), sizeof(addr)) != 0 && errno != EINPROGRESS)
	{
	fail:
		close(fd);
		co_return nullptr;
	}
	
	co_await runloop2::await_write(fd);
	
	int error = 0;
	socklen_t len = sizeof(error);
	getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&error, &len);
	if (error != 0)
	{
		errno = error;
		goto fail;
	}
	
	co_return new socket2_impl(fd);
}


autoptr<socket2_udp> socket2_udp::create(socket2::address ip)
{
	if (!ip)
		return nullptr;
	int fd = mksocket(ip.as_native()->sa_family, SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0);
	if (fd < 0)
		return nullptr;
	return new socket2_udp(fd, ip);
}

ssize_t socket2_udp::recv_sync(bytesw by, socket2::address* sender)
{
	socklen_t addrsize = sender ? sizeof(*sender) : 0;
	return fixret(::recvfrom(fd, by.ptr(), by.size(), MSG_DONTWAIT|MSG_NOSIGNAL, socket2::address::as_native(sender), &addrsize));
}

void socket2_udp::send(bytesr by)
{
	::sendto(fd, by.ptr(), by.size(), MSG_DONTWAIT|MSG_NOSIGNAL, addr.as_native(), sizeof(addr));
}


autoptr<socketlisten> socketlisten::create(const socket2::address & addr, function<void(autoptr<socket2>)> cb)
{
	int fd = mksocket(AF_INET6, SOCK_STREAM, 0);
	if (fd < 0) return nullptr;
	
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, false) < 0) goto fail;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, true) < 0) goto fail;
	if (bind(fd, addr.as_native(), sizeof(addr)) < 0) goto fail;
	if (listen(fd, 10) < 0) goto fail;
	return new socketlisten(fd, std::move(cb));
	
fail:
	close(fd);
	return nullptr;
}

autoptr<socketlisten> socketlisten::create(uint16_t port, function<void(autoptr<socket2>)> cb)
{
	static const uint8_t localhost[16] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 };
	return create(socket2::address(localhost, port), cb);
}

socketlisten::socketlisten(int fd, function<void(autoptr<socket2>)> cb) : fd(fd), cb(std::move(cb))
{
	setblock(fd, false);
	runloop2::await_read(fd).then(&wait);
}

void socketlisten::on_incoming()
{
#ifdef __linux__
	int nfd = accept4(this->fd, NULL, NULL, SOCK_CLOEXEC);
#else
	int nfd = accept(this->fd, NULL, NULL);
#endif
	if (nfd >= 0)
	{
		configure_sock(nfd);
		this->cb(new socket2_impl(nfd));
	}
	runloop2::await_read(fd).then(&wait);
}
#endif
