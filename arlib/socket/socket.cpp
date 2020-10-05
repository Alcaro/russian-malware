#include "socket.h"
#include "../bytepipe.h"
#include "../dns.h"
#include "../thread.h"
#include "../stringconv.h"

#undef socket
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <mstcpip.h>
	#define MSG_NOSIGNAL 0
	#define MSG_DONTWAIT 0
	#define SOCK_CLOEXEC 0
	#define SOCK_NONBLOCK 0
	#define EINPROGRESS WSAEINPROGRESS
	#define close closesocket
	#ifdef _MSC_VER
		#pragma comment(lib, "ws2_32.lib")
	#endif
	typedef intptr_t socketint_t;
#else
	#include <netdb.h>
	#include <errno.h>
	#include <unistd.h>
	#include <fcntl.h>
	
	#include <netinet/tcp.h>
	#include <arpa/inet.h>
	typedef int socketint_t;
#endif

//wrapper because 'socket' is a type in this code, so socket(2) needs another name
static socketint_t mksocket(int domain, int type, int protocol) { return socket(domain, type|SOCK_CLOEXEC, protocol); }
#define socket socket_t

namespace {

#ifdef _WIN32
RUN_ONCE_FN(initialize)
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData); // why
}
#else
static void initialize() {}
#endif

static int fixret(int ret)
{
	if (ret > 0) return ret;
	if (ret == 0) return socket::e_closed;
#ifdef __unix__
	if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
#else
	if (ret < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return 0;
#endif
	return socket::e_broken;
}

static int setsockopt(socketint_t socket, int level, int option_name, const void * option_value, socklen_t option_len)
{
	return ::setsockopt(socket, level, option_name, (char*)/*lol windows*/option_value, option_len);
}

static int setsockopt(socketint_t socket, int level, int option_name, int option_value)
{
	return setsockopt(socket, level, option_name, &option_value, sizeof(option_value));
}

//MSG_DONTWAIT is usually better, but accept() and connect() don't take that argument
static void MAYBE_UNUSED setblock(socketint_t fd, bool newblock)
{
#ifdef _WIN32
	u_long nonblock = !newblock;
	ioctlsocket(fd, FIONBIO, &nonblock);
#else
	int flags = fcntl(fd, F_GETFL, 0);
	flags &= ~O_NONBLOCK;
	if (!newblock) flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
#endif
}

static addrinfo * parse_hostname(cstring domain, uint16_t port, bool udp)
{
	addrinfo hints;
	memset(&hints, 0, sizeof(addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = udp ? SOCK_DGRAM : SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST;
	
	addrinfo * addr = NULL;
	getaddrinfo(domain.c_str(), tostring(port), &hints, &addr); // why does getaddrinfo take port as a string
	
	return addr;
}

static int connect(cstring domain, int port)
{
	initialize();
	
	addrinfo * addr = parse_hostname(domain, port, false);
	if (!addr) return -1;
	
	socketint_t fd = mksocket(addr->ai_family, addr->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, addr->ai_protocol);
	if (fd < 0) return -1;
#ifndef _WIN32
	//because 30 second pauses are unequivocally detestable
	timeval timeout;
	timeout.tv_sec = 4;
	timeout.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
#endif
	if (connect(fd, addr->ai_addr, addr->ai_addrlen) != 0 && errno != EINPROGRESS)
	{
		freeaddrinfo(addr);
		close(fd);
		return -1;
	}
	freeaddrinfo(addr);
	
#ifndef _WIN32
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, 1); // enable
	setsockopt(fd, SOL_TCP, TCP_KEEPCNT, 3); // ping count before the kernel gives up
	setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, 30); // seconds idle until it starts pinging
	setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, 10); // seconds per ping once the pings start
#else
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
#endif
	
	return fd;
}


class socket_raw : public socket {
public:
	socket_raw(socketint_t fd, runloop* loop) : fd(fd), loop(loop) {}
	
	socketint_t fd;
#ifdef _WIN32
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	HANDLE waiter = CreateEvent(NULL, true, false, NULL);
#endif
	
	runloop* loop = NULL;
	function<void()> cb_read;
	function<void()> cb_write;
	
	static socket* create(socketint_t fd, runloop* loop)
	{
		if (fd<0) return NULL;
		return new socket_raw(fd, loop);
	}
	
	int recv(arrayvieww<uint8_t> data)
	{
		return fixret(::recv(this->fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL));
	}
	
	int send(arrayview<uint8_t> data)
	{
		return fixret(::send(this->fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL));
	}
	
	void callback(function<void()> cb_read, function<void()> cb_write)
	{
		this->cb_read = cb_read;
		this->cb_write = cb_write;
#ifdef __unix__
		loop->set_fd(fd,
		             cb_read  ? bind_lambda([this](uintptr_t) { this->cb_read();  }) : NULL,
		             cb_write ? bind_lambda([this](uintptr_t) { this->cb_write(); }) : NULL);
#else
		WSAEventSelect(fd, waiter, (cb_read ? FD_READ : 0) | (cb_write ? FD_WRITE : 0) | FD_CLOSE);
		// don't know whether readable or writable; call both, they'll deal with zero bytes
		loop->set_object(waiter, (cb_read || cb_write) ? bind_lambda([this](HANDLE)
			{
				ResetEvent(this->waiter);
				RETURN_IF_CALLBACK_DESTRUCTS(this->cb_write());
				RETURN_IF_CALLBACK_DESTRUCTS(this->cb_read());
			}) : NULL);
#endif
	}
	
	~socket_raw()
	{
#ifndef _WIN32
		loop->set_fd(fd, NULL, NULL);
#endif
		close(fd);
#ifdef _WIN32
		loop->set_object(waiter, NULL);
		CloseHandle(waiter);
#endif
	}
};

class socket_raw_udp : public socket {
public:
	socket_raw_udp(socketint_t fd, sockaddr * addr, socklen_t addrlen, runloop* loop)
		: fd(fd), loop(loop), peeraddr((uint8_t*)addr, addrlen)
	{
		peeraddr_cmp.resize(addrlen);
	}
	
	socketint_t fd;
#ifdef _WIN32
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	HANDLE waiter = CreateEvent(NULL, true, false, NULL);
#endif
	
	runloop* loop;
	function<void()> cb_read;
	function<void()> cb_write;
	
	array<uint8_t> peeraddr;
	array<uint8_t> peeraddr_cmp;
	
	int recv(arrayvieww<uint8_t> data)
	{
		socklen_t len = peeraddr_cmp.size();
		int ret = fixret(::recvfrom(fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL, (sockaddr*)peeraddr_cmp.ptr(), &len));
		//discard data from unexpected sources. source IPs can be forged under UDP, but probably helps a little
		//TODO: may be better to implement recvfrom as an actual function on those sockets
		if (len != (socklen_t)peeraddr.size() || peeraddr != peeraddr_cmp) return 0;
		return ret;
	}
	
	int send(arrayview<uint8_t> data)
	{
		return fixret(::sendto(fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL, (sockaddr*)peeraddr.ptr(), peeraddr.size()));
	}
	
	/*private*/ void on_readable(uintptr_t) { cb_read(); }
	/*private*/ void on_writable(uintptr_t) { cb_write(); }
	void callback(function<void()> cb_read, function<void()> cb_write)
	{
		this->cb_read = cb_read;
		this->cb_write = cb_write;
#ifdef __unix__
		loop->set_fd(fd,
		             cb_read  ? bind_this(&socket_raw_udp::on_readable) : NULL,
		             cb_write ? bind_this(&socket_raw_udp::on_writable) : NULL);
#else
		WSAEventSelect(fd, waiter, (cb_read ? FD_READ : 0) | (cb_write ? FD_WRITE : 0) | FD_CLOSE);
		// don't know whether readable or writable; call both, they'll deal with zero bytes
		loop->set_object(waiter, (cb_read || cb_write) ? bind_lambda([this](HANDLE)
			{
				ResetEvent(this->waiter);
				RETURN_IF_CALLBACK_DESTRUCTS(this->cb_write());
				RETURN_IF_CALLBACK_DESTRUCTS(this->cb_read());
			}) : NULL);
#endif
	}
	
	~socket_raw_udp()
	{
#ifdef __unix__
		loop->set_fd(fd, NULL, NULL);
#endif
		close(fd);
#ifdef _WIN32
		loop->set_object(waiter, NULL);
		CloseHandle(waiter);
#endif
	}
};

//A flexible socket sends a DNS request, then seamlessly opens a TCP or SSL connection to the returned IP.
class socket_flex : public socket {
public:
	socket* i_connect(cstring domain, cstring ip, int port)
	{
		socket* ret = socket_raw::create(connect(ip, port), this->loop);
#ifdef ARLIB_SSL
		if (ret && this->ssl) ret = socket::wrap_ssl_raw(ret, domain, this->loop);
#endif
		return ret;
	}
	
	socket_flex(cstring domain, int port, runloop* loop
#ifdef ARLIB_SSL
, bool ssl = false
#endif
)
	{
		this->loop = loop;
#ifdef ARLIB_SSL
		this->ssl = ssl;
#endif
		child = i_connect(domain, domain, port);
		if (!child)
		{
			this->port = port;
			dns = new DNS(loop);
			dns->resolve(domain, bind_this(&socket_flex::dns_cb));
		}
		set_loop();
	}
	
	autoptr<DNS> dns;
	uint16_t port;
#ifdef ARLIB_SSL
	bool ssl;
#endif
	
	runloop* loop = NULL;
	autoptr<socket> child;
	
	function<void()> cb_read;
	function<void()> cb_write; // call once when connection is ready, or forever if connection is broken
	
	/*private*/ void dns_cb(string domain, string ip)
	{
		child = i_connect(domain, ip, port);
		dns = NULL;
		set_loop();
		cb_write(); // TODO: do this later
	}
	
	/*private*/ void set_loop()
	{
		if (child) child->callback(cb_read, cb_write);
	}
	
	int recv(arrayvieww<uint8_t> data)
	{
		if (!child)
		{
			if (dns) return 0;
			else return e_broken;
		}
		return child->recv(data);
	}
	
	int send(arrayview<uint8_t> data)
	{
		if (!child)
		{
			if (dns) return 0;
			else return e_broken;
		}
		return child->send(data);
	}
	
	void callback(function<void()> cb_read, function<void()> cb_write)
	{
		this->cb_read = cb_read;
		this->cb_write = cb_write;
		
		set_loop();
	}
};

//Makes writes always succeed. If they fail, they're buffered in memory.
class socketbuf : public socket {
public:
	socketbuf(socket* child, runloop* loop) : loop(loop), child(child) {}
	
	runloop* loop;
	autoptr<socket> child;
	
	bytepipe tosend;
	DECL_TIMER(idle, socketbuf);
	function<void()> cb_read;
	function<void()> cb_write;
	
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	
	/*private*/ void cancel()
	{
		child = NULL;
		idle.reset();
	}
	
	/*private*/ void call_cb_immed()
	{
		if (cb_read)  RETURN_IF_CALLBACK_DESTRUCTS(cb_read( ));
		if (cb_write) RETURN_IF_CALLBACK_DESTRUCTS(cb_write());
		
		set_loop();
	}
	
	/*private*/ void set_loop()
	{
		if (!child) return;
		child->callback(cb_read, tosend ? bind_this(&socketbuf::trysend_void) : NULL);
		
		if (cb_write || (cb_read && !child))
		{
			idle.set_idle(bind_this(&socketbuf::call_cb_immed));
		}
		else
		{
			idle.reset();
		}
	}
	/*private*/ bool trysend(bool in_runloop)
	{
		arrayview<uint8_t> bytes = tosend.pull_buf();
		if (!bytes.size()) return true;
		int nbytes = child->send(bytes);
		if (nbytes < 0)
		{
			if (in_runloop) cancel();
			return false;
		}
		tosend.pull_done(nbytes);
		
		set_loop();
		return true;
	}
	/*private*/ void trysend_void()
	{
		trysend(true);
	}
	
	int recv(arrayvieww<uint8_t> data)
	{
		if (!child) return e_broken;
		return child->recv(data);
	}
	
	int send(arrayview<uint8_t> data)
	{
		if (!child) return e_broken;
		tosend.push(data);
		if (!trysend(false)) return -1;
		return data.size();
	}
	
	void callback(function<void()> cb_read, function<void()> cb_write)
	{
		this->cb_read = cb_read;
		this->cb_write = cb_write;
		set_loop();
	}
};

} // close namespace

socket* socket::create(cstring domain, int port, runloop* loop)
{
	return new socketbuf(new socket_flex(domain, port, loop), loop);
}

socket* socket::create_raw(cstring domain, int port, runloop* loop)
{
	return new socket_flex(domain, port, loop);
}

#ifdef ARLIB_SSL
socket* socket::create_ssl(cstring domain, int port, runloop* loop)
{
	return new socketbuf(new socket_flex(domain, port, loop, true), loop);
}
#endif

socket* socket::create_udp(cstring domain, int port, runloop* loop)
{
	initialize();
	
	addrinfo * addr = parse_hostname(domain, port, true);
	if (!addr) return NULL;
	
	socketint_t fd = mksocket(addr->ai_family, addr->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, addr->ai_protocol);
	socket* ret = new socket_raw_udp(fd, addr->ai_addr, addr->ai_addrlen, loop);
	freeaddrinfo(addr);
	//TODO: teach the wrapper about UDP, then add it
	
	return ret;
}

socket* socket::wrap(socket* inner, runloop* loop)
{
	return new socketbuf(inner, loop);
}

#ifdef ARLIB_SSL
socket* socket::wrap_ssl(socket* inner, cstring domain, runloop* loop)
{
	return wrap(wrap_ssl_raw(inner, domain, loop), loop);
}

socket* socket::create_sslmaybe(bool ssl, cstring domain, int port, runloop* loop)
{
	return (ssl ? socket::create_ssl : socket::create)(domain, port, loop);
}
#endif


static MAYBE_UNUSED socketint_t socketlisten_create_ip4(int port)
{
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	
	socketint_t fd = mksocket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) goto fail;
	
	if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) goto fail;
	if (listen(fd, 10) < 0) goto fail;
	return fd;
	
fail:
	close(fd);
	return -1;
}

static socketint_t socketlisten_create_ip6(int port)
{
	struct sockaddr_in6 sa; // IN6ADDR_ANY_INIT should work, but doesn't.
	memset(&sa, 0, sizeof(sa));
	sa.sin6_family = AF_INET6;
	sa.sin6_addr = in6addr_any;
	sa.sin6_port = htons(port);
	
	socketint_t fd = mksocket(AF_INET6, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, false) < 0) goto fail;
	if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) goto fail;
	if (listen(fd, 10) < 0) goto fail;
	return fd;
	
fail:
	close(fd);
	return -1;
}

socketlisten* socketlisten::create(int port, runloop* loop, function<void(autoptr<socket>)> callback)
{
	initialize();
	
	socketint_t fd = -1;
	if (fd<0) fd = socketlisten_create_ip6(port);
#if defined(_WIN32) && _WIN32_WINNT < _WIN32_WINNT_LONGHORN
	// XP can't dualstack the v6 addresses, so let's keep the fallback
	if (fd<0) fd = socketlisten_create_ip4(port);
#endif
	if (fd<0) return NULL;
	
	return new socketlisten(fd, loop, callback);
}

socketlisten::socketlisten(socketint_t fd, runloop* loop, function<void(autoptr<socket>)> callback)
	: fd(fd), loop(loop), callback(callback)
{
	setblock(fd, false);
#ifdef __unix__
	loop->set_fd(fd, [this](uintptr_t n) {
#ifdef __linux__
		int nfd = accept4(this->fd, NULL, NULL, SOCK_CLOEXEC);
#else
		int nfd = accept(this->fd, NULL, NULL);
#endif
		if (nfd < 0) return;
		this->callback(new socketbuf(socket_raw::create(nfd, this->loop), this->loop));
	});
#else
	waiter = CreateEvent(NULL, true, false, NULL);
	WSAEventSelect(fd, waiter, FD_ACCEPT);
	loop->set_object(waiter, [this](HANDLE h) {
		ResetEvent(this->waiter);
		socketint_t nfd = accept(this->fd, NULL, NULL);
		if (nfd < 0) return;
		this->callback(new socketbuf(socket_raw::create(nfd, this->loop), this->loop));
	});
#endif
}

socketlisten::~socketlisten()
{
#ifdef __unix__
	loop->set_fd(fd, NULL, NULL);
#else
	loop->set_object(waiter, NULL);
	CloseHandle(waiter);
#endif
	close(fd);
}
