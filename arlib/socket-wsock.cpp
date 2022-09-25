#if defined(ARLIB_SOCKET) && defined(_WIN32)
#include "socket.h"

#undef socket
#include <winsock2.h>
#include <ws2tcpip.h>
#define MSG_NOSIGNAL 0
#define MSG_DONTWAIT 0
#define SOCK_CLOEXEC 0
#define SOCK_NONBLOCK 0
#ifdef _MSC_VER
	#pragma comment(lib, "ws2_32.lib")
#endif

static SOCKET mksocket(int domain, int type, int protocol)
{
	//return WSASocket(domain, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
	return WSASocket(domain, type, protocol, nullptr, 0, 0);
}

namespace {

oninit_static()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData); // why
}

static int fixret(int ret)
{
	if (ret > 0) return ret;
	if (ret == 0) return -1;
	if (ret < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return 0;
	return -1;
}

static int my_setsockopt(SOCKET fd, int level, int option_name, const void * option_value, socklen_t option_len)
{
	return ::setsockopt(fd, level, option_name, (char*)/*lol windows*/option_value, option_len);
}
static int my_setsockopt(SOCKET fd, int level, int option_name, int option_value)
{
	return my_setsockopt(fd, level, option_name, &option_value, sizeof(option_value));
}
#define setsockopt my_setsockopt

//MSG_DONTWAIT is usually better, but accept() and connect() don't take that argument
static void MAYBE_UNUSED setblock(SOCKET fd, bool newblock)
{
	u_long nonblock = !newblock;
	ioctlsocket(fd, FIONBIO, &nonblock);
}

class socket2_impl : public socket2 {
public:
	// what a mess... why is microsoft like this
	SOCKET fd;
	WSAEVENT ev;
	long ev_active = 0;
	
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	
	struct waiter_t : public waiter<void, waiter_t> {
		void complete() { container_of<&socket2_impl::wait>(this)->complete(); }
	} wait;
	producer<void> prod_recv;
	producer<void> prod_send;
	
	void complete()
	{
		runloop2::await_handle(ev).then(&wait);
		
		WSANETWORKEVENTS ev_out;
		WSAEnumNetworkEvents(fd, ev, &ev_out);
		ev_active &= ~ev_out.lNetworkEvents;
		
		if ((ev_out.lNetworkEvents & FD_READ) && prod_recv.has_waiter())
			RETURN_IF_CALLBACK_DESTRUCTS(prod_recv.complete());
		if ((ev_out.lNetworkEvents & FD_WRITE) && prod_send.has_waiter())
			prod_send.complete();
	}
	
	socket2_impl(SOCKET fd, WSAEVENT ev) : fd(fd), ev(ev)
	{
		runloop2::await_handle(ev).then(&wait);
	}
	ssize_t recv_sync(bytesw by) override
	{
		return fixret(::recv(this->fd, (char*)by.ptr(), by.size(), MSG_NOSIGNAL));
	}
	ssize_t send_sync(bytesr by) override
	{
		return fixret(::send(this->fd, (char*)by.ptr(), by.size(), MSG_NOSIGNAL));
	}
	async<void> can_recv() override
	{
		ev_active |= FD_READ;
		WSAEventSelect(fd, ev, ev_active);
		return &prod_recv;
	}
	async<void> can_send() override
	{
		// this one claims to be not level triggered, but it works for me
		ev_active |= FD_WRITE;
		WSAEventSelect(fd, ev, ev_active);
		return &prod_send;
	}
	
	~socket2_impl()
	{
		WSACloseEvent(ev);
		closesocket(fd);
	}
};
}


async<autoptr<socket2>> socket2::create(address addr)
{
	if (!addr)
		co_return nullptr;
	
	WSAEVENT ev = WSACreateEvent();
	if (ev == WSA_INVALID_EVENT)
		abort();
	SOCKET fd = mksocket(addr.as_native()->sa_family, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0);
	if (fd < 0)
		co_return nullptr;
	setblock(fd, false);
	if (connect(fd, addr.as_native(), sizeof(addr)) != 0 && WSAGetLastError() != WSAEWOULDBLOCK)
	{
	fail:
		closesocket(fd);
		WSACloseEvent(ev);
		co_return nullptr;
	}
	
	// Nagle's algorithm - made sense in the 80s, but these days, the saved bandwidth isn't worth the added latency.
	// Better combine stuff in userspace, syscalls are (relatively) expensive these days.
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, true);
	
	struct tcp_keepalive keepalive = {
		1,       // SO_KEEPALIVE
		30*1000, // TCP_KEEPIDLE in milliseconds
		3*1000,  // TCP_KEEPINTVL
		//On Windows Vista and later, the number of keep-alive probes (data retransmissions) is set to 10 and cannot be changed.
		//https://msdn.microsoft.com/en-us/library/windows/desktop/dd877220(v=vs.85).aspx
		//so no TCP_KEEPCNT; I'll reduce INTVL instead. And a polite server will RST anyways.
	};
	u_long ignore;
	WSAIoctl(fd, SIO_KEEPALIVE_VALS, &keepalive, sizeof(keepalive), NULL, 0, &ignore, NULL, NULL);
	
	WSAEventSelect(fd, ev, FD_CONNECT);
	co_await runloop2::await_handle(ev);
	WSAResetEvent(ev);
	
	int error = 0;
	socklen_t len = sizeof(error);
	getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&error, &len);
	if (error != 0)
		goto fail;
	
	co_return new socket2_impl(fd, ev);
}

autoptr<socket2_udp> socket2_udp::create(socket2::address ip)
{
	if (!ip)
		return nullptr;
	SOCKET fd = mksocket(ip.as_native()->sa_family, SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0);
	if (fd < 0)
		return nullptr;
	return new socket2_udp(fd, ip);
}

ssize_t socket2_udp::recv_sync(bytesw by, socket2::address* sender)
{
	socklen_t addrsize = sender ? sizeof(*sender) : 0;
	return fixret(::recvfrom(fd, (char*)by.ptr(), by.size(), MSG_NOSIGNAL, sender->as_native(), &addrsize));
}

void socket2_udp::send(bytesr by)
{
	::sendto(fd, (char*)by.ptr(), by.size(), MSG_DONTWAIT|MSG_NOSIGNAL, addr.as_native(), sizeof(addr));
}
#endif
