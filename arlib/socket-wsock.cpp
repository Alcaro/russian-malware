#if defined(ARLIB_SOCKET) && defined(_WIN32)
#include "socket.h"
#undef socket

#include <winsock2.h>
#include <ws2tcpip.h>
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

static int my_setsockopt(SOCKET sock, int level, int option_name, const void * option_value, socklen_t option_len)
{
	return setsockopt(sock, level, option_name, (char*)/*lol windows*/option_value, option_len);
}
static int my_setsockopt(SOCKET sock, int level, int option_name, int option_value)
{
	return my_setsockopt(sock, level, option_name, &option_value, sizeof(option_value));
}
#define setsockopt my_setsockopt

//MSG_DONTWAIT isn't implemented on windows
static void MAYBE_UNUSED setblock(SOCKET sock, bool newblock)
{
	u_long nonblock = !newblock;
	ioctlsocket(sock, FIONBIO, &nonblock);
}

void configure_sock(SOCKET sock)
{
	// Nagle's algorithm - made sense in the 80s, but these days, the saved bandwidth isn't worth the added latency.
	// Better combine stuff in userspace, syscalls are (relatively) expensive these days.
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, true);
	
	struct tcp_keepalive keepalive = {
		1,       // SO_KEEPALIVE
		30*1000, // TCP_KEEPIDLE in milliseconds
		3*1000,  // TCP_KEEPINTVL
		//On Windows Vista and later, the number of keep-alive probes (data retransmissions) is set to 10 and cannot be changed.
		//https://msdn.microsoft.com/en-us/library/windows/desktop/dd877220(v=vs.85).aspx
		//so no TCP_KEEPCNT; I'll reduce INTVL instead. And a polite server will RST anyways.
	};
	u_long ignore;
	WSAIoctl(sock, SIO_KEEPALIVE_VALS, &keepalive, sizeof(keepalive), NULL, 0, &ignore, NULL, NULL);
}

class socket2_impl : public socket2 {
public:
	// what a mess... why is microsoft like this
	SOCKET fd;
	WSAEVENT ev;
	long ev_active = 0;
	bool send_wouldblock = false;
	
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	
	waiter<void> wait = make_waiter<&socket2_impl::wait, &socket2_impl::complete>();
	producer<void> prod_recv;
	producer<void> prod_send;
	
	void complete()
	{
		WSANETWORKEVENTS ev_out;
		WSAEnumNetworkEvents(fd, ev, &ev_out);
		ev_active &= ~ev_out.lNetworkEvents;
		
		runloop2::await_handle(ev).then(&wait);
		
		if ((ev_out.lNetworkEvents & (FD_READ|FD_CLOSE)) && prod_recv.has_waiter())
			RETURN_IF_CALLBACK_DESTRUCTS(prod_recv.complete());
		if ((ev_out.lNetworkEvents & (FD_WRITE|FD_CLOSE)) && prod_send.has_waiter())
			prod_send.complete();
	}
	
	socket2_impl(SOCKET fd, WSAEVENT ev) : fd(fd), ev(ev)
	{
		runloop2::await_handle(ev).then(&wait);
	}
	
	ssize_t recv_sync(bytesw by) override
	{
		assert(by);
		return fixret(::recv(this->fd, (char*)by.ptr(), by.size(), 0));
	}
	ssize_t send_sync(bytesr by) override
	{
		assert(by);
		ssize_t ret = fixret(::send(this->fd, (char*)by.ptr(), by.size(), 0));
		send_wouldblock = (ret == 0);
		return ret;
	}
	async<void> can_recv() override
	{
		ev_active |= FD_READ|FD_CLOSE;
		WSAEventSelect(fd, ev, ev_active);
		return &prod_recv;
	}
	async<void> can_send() override
	{
		// this one isn't level triggered
		if (!send_wouldblock)
			return prod_send.complete_sync();
		ev_active |= FD_WRITE|FD_CLOSE;
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
	SOCKET sock = mksocket(addr.as_native()->sa_family, SOCK_STREAM, 0);
	if (sock < 0)
		co_return nullptr;
	setblock(sock, false);
	// one would expect WSAEINPROGRESS, but both docs and actual behavior say it's WOULDBLOCK
	if (connect(sock, addr.as_native(), sizeof(addr)) != 0 && WSAGetLastError() != WSAEWOULDBLOCK)
	{
	fail:
		closesocket(sock);
		WSACloseEvent(ev);
		co_return nullptr;
	}
	
	configure_sock(sock);
	
	WSAEventSelect(sock, ev, FD_CONNECT);
	co_await runloop2::await_handle(ev);
	WSAResetEvent(ev);
	
	int error = 0;
	socklen_t len = sizeof(error);
	getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &len);
	if (error != 0)
		goto fail;
	
	co_return new socket2_impl(sock, ev);
}

autoptr<socket2_udp> socket2_udp::create(socket2::address ip)
{
	if (!ip)
		return nullptr;
	SOCKET sock = mksocket(ip.as_native()->sa_family, SOCK_DGRAM, 0);
	// TODO: check if ioctl SIO_UDP_CONNRESET does anything useful
	if (sock < 0)
		return nullptr;
	return new socket2_udp(sock, ip);
}

ssize_t socket2_udp::recv_sync(bytesw by, socket2::address* sender)
{
	socket2::address sender_backup;
	if (!sender)
		sender = &sender_backup; // windows doesn't accept null address
	socklen_t addrsize = sizeof(*sender);
	return fixret(::recvfrom(sock, (char*)by.ptr(), by.size(), 0, sender->as_native(), &addrsize));
}

void socket2_udp::send(bytesr by)
{
	::sendto(sock, (char*)by.ptr(), by.size(), 0, addr.as_native(), sizeof(addr));
}


static MAYBE_UNUSED SOCKET socketlisten_create_ip4(u_long ip, int port)
{
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = ip;
	sa.sin_port = htons(port);
	
	SOCKET sock = mksocket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET) return INVALID_SOCKET;
	
	if (bind(sock, (struct sockaddr*)&sa, sizeof(sa)) < 0) goto fail;
	if (listen(sock, 10) < 0) goto fail;
	return sock;
	
fail:
	closesocket(sock);
	return -1;
}

static SOCKET socketlisten_create_ip6(const struct in6_addr& ip, int port)
{
	struct sockaddr_in6 sa; // IN6ADDR_ANY_INIT should work, but doesn't.
	memset(&sa, 0, sizeof(sa));
	sa.sin6_family = AF_INET6;
	sa.sin6_addr = ip;
	sa.sin6_port = htons(port);
	
	SOCKET sock = mksocket(AF_INET6, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET) return INVALID_SOCKET;
	
	if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, false) < 0) goto fail;
	if (bind(sock, (struct sockaddr*)&sa, sizeof(sa)) < 0) goto fail;
	if (listen(sock, 10) < 0) goto fail;
	return sock;
	
fail:
	closesocket(sock);
	return -1;
}

autoptr<socketlisten> socketlisten::create(uint16_t port, function<void(autoptr<socket2>)> cb)
{
	SOCKET sock = INVALID_SOCKET;
	if (sock == INVALID_SOCKET) sock = socketlisten_create_ip6(in6addr_any, port);
#if _WIN32_WINNT < _WIN32_WINNT_LONGHORN
	// XP can't dualstack the v6 addresses
	if (sock == INVALID_SOCKET) sock = socketlisten_create_ip4(htonl(INADDR_ANY), port);
#endif
	if (sock == INVALID_SOCKET) return NULL;
	
	return new socketlisten(sock, cb);
}

socketlisten::socketlisten(SOCKET sock, function<void(autoptr<socket2>)> cb) : cb(cb), sock(sock)
{
	setblock(sock, false);
	ev = CreateEvent(NULL, true, false, NULL);
	WSAEventSelect(sock, ev, FD_ACCEPT);
	runloop2::await_handle(ev).then(&wait);
}

void socketlisten::on_incoming()
{
	ResetEvent(this->ev);
	SOCKET newsock = accept(this->sock, NULL, NULL);
	if (newsock < 0) return;
	configure_sock(sock);
	this->cb(new socket2_impl(newsock, WSACreateEvent()));
	runloop2::await_handle(ev).then(&wait);
}

socketlisten::~socketlisten()
{
	wait.cancel(); // must do this before closing ev/sock
	CloseHandle(ev);
	closesocket(sock);
}
#endif
