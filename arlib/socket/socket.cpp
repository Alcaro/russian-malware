#include "socket.h"
#include "../bytepipe.h"
#include "../dns.h"
#include"../stringconv.h"

#undef socket
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <mstcpip.h>
	#define MSG_NOSIGNAL 0
	#define MSG_DONTWAIT 0
	#define SOCK_CLOEXEC 0
	#define close closesocket
	#ifdef _MSC_VER
		#pragma comment(lib, "ws2_32.lib")
	#endif
#else
	#include <netdb.h>
	#include <errno.h>
	#include <unistd.h>
	#include <fcntl.h>
	
	#include <netinet/tcp.h>
	#include <arpa/inet.h>
#endif

//wrapper because 'socket' is a type in this code, so socket(2) needs another name
static int mksocket(int domain, int type, int protocol) { return socket(domain, type|SOCK_CLOEXEC, protocol); }
#define socket socket_t

namespace {

static void initialize()
{
#ifdef _WIN32 // lol
	static bool initialized = false;
	if (initialized) return;
	initialized = true;
	
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

static int setsockopt(int socket, int level, int option_name, const void * option_value, socklen_t option_len)
{
	return ::setsockopt(socket, level, option_name, (char*)/*lol windows*/option_value, option_len);
}

static int setsockopt(int socket, int level, int option_name, int option_value)
{
	return setsockopt(socket, level, option_name, &option_value, sizeof(option_value));
}

//MSG_DONTWAIT is usually better, but accept() and connect() don't take that argument
static void MAYBE_UNUSED setblock(int fd, bool newblock)
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

static addrinfo * parse_hostname(cstring domain, int port, bool udp)
{
	char portstr[16];
	sprintf(portstr, "%i", port);
	
	addrinfo hints;
	memset(&hints, 0, sizeof(addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = udp ? SOCK_DGRAM : SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST;
	
	addrinfo * addr = NULL;
	getaddrinfo(domain.c_str(), portstr, &hints, &addr);
	
	return addr;
}

static int connect(cstring domain, int port)
{
	initialize();
	
	addrinfo * addr = parse_hostname(domain, port, false);
	if (!addr) return -1;
	
	//TODO: this probably fails on windows...
	int fd = mksocket(addr->ai_family, addr->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, addr->ai_protocol);
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

} // close namespace

namespace {

class socket_raw : public socket {
public:
	socket_raw(int fd) : fd(fd) {}
	
	int fd;
	runloop* loop = NULL;
	function<void(socket*)> cb_read;
	function<void(socket*)> cb_write;
	
	static socket* create(int fd)
	{
		if (fd<0) return NULL;
		return new socket_raw(fd);
	}
	
	/*private*/ int fixret(int ret)
	{
		if (ret > 0) return ret;
		if (ret == 0) return e_closed;
#ifdef __unix__
		if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
#endif
#ifdef _WIN32
		if (ret < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return 0;
#endif
		return e_broken;
	}
	
	int recv(arrayvieww<byte> data)
	{
		return fixret(::recv(this->fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL));
	}
	
	int send(arrayview<byte> data)
	{
		return fixret(::send(fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL));
	}
	
	/*private*/ void on_readable(uintptr_t) { cb_read(this); }
	/*private*/ void on_writable(uintptr_t) { cb_write(this); }
	void callback(runloop* loop, function<void(socket*)> cb_read, function<void(socket*)> cb_write)
	{
		this->loop = loop;
		this->cb_read = cb_read;
		this->cb_write = cb_write;
		loop->set_fd(fd,
		             cb_read  ? bind_this(&socket_raw::on_readable) : NULL,
		             cb_write ? bind_this(&socket_raw::on_writable) : NULL);
	}
	
	~socket_raw()
	{
		if (loop) loop->set_fd(fd, NULL, NULL);
		if (fd>=0) close(fd);
	}
};

class socket_raw_udp : public socket {
public:
	socket_raw_udp(int fd, sockaddr * addr, socklen_t addrlen) : fd(fd), peeraddr((uint8_t*)addr, addrlen)
	{
		peeraddr_cmp.resize(addrlen);
	}
	
	int fd;
	
	runloop* loop = NULL;
	function<void(socket*)> cb_read;
	function<void(socket*)> cb_write;
	
	array<byte> peeraddr;
	array<byte> peeraddr_cmp;
	
	/*private*/ int fixret(int ret)
	{
		if (ret > 0) return ret;
		if (ret == 0) return e_closed;
#ifdef __unix__
		if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
#endif
#ifdef _WIN32
		if (ret < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return 0;
#endif
		return e_broken;
	}
	
	int recv(arrayvieww<byte> data)
	{
		socklen_t len = peeraddr_cmp.size();
		int ret = fixret(::recvfrom(fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL, (sockaddr*)peeraddr_cmp.ptr(), &len));
		//discard data from unexpected sources. source IPs can be forged under UDP, but probably helps a little
		//TODO: may be better to implement recvfrom as an actual function on those sockets
		if (len != peeraddr.size() || peeraddr != peeraddr_cmp) return 0;
		return ret;
	}
	
	int send(arrayview<byte> data)
	{
		return fixret(::sendto(fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL, (sockaddr*)peeraddr.ptr(), peeraddr.size()));
	}
	
	/*private*/ void on_readable(uintptr_t) { cb_read(this); }
	/*private*/ void on_writable(uintptr_t) { cb_write(this); }
	void callback(runloop* loop, function<void(socket*)> cb_read, function<void(socket*)> cb_write)
	{
		this->loop = loop;
		this->cb_read = cb_read;
		this->cb_write = cb_write;
		loop->set_fd(fd,
		             cb_read  ? bind_this(&socket_raw_udp::on_readable) : NULL,
		             cb_write ? bind_this(&socket_raw_udp::on_writable) : NULL);
	}
	
	~socket_raw_udp()
	{
		if (loop) loop->set_fd(fd, NULL, NULL);
		if (fd>=0) close(fd);
	}
};

class socketwrap : public socket {
public:
	socket* i_connect(cstring domain, cstring ip, int port)
	{
		socket* ret = socket_raw::create(connect(ip, port));
		if (ret && this->ssl) ret = socket::wrap_ssl(ret, domain, loop);
		return ret;
	}
	
	socketwrap(cstring domain, int port, runloop* loop, bool ssl)
	{
		this->loop = loop;
		this->ssl = ssl;
		child = i_connect(domain, domain, port);
		if (!child)
		{
			this->port = port;
			dns = new DNS(loop);
			dns->resolve(domain, bind_this(&socketwrap::dns_cb));
		}
		set_loop();
	}
	
	autoptr<DNS> dns;
	uint16_t port;
	bool ssl;
	
	void dns_cb(string domain, string ip)
	{
		child = i_connect(domain, ip, port);
		dns = NULL;
		set_loop();
	}
	
	autoptr<socket> child;
	
	bytepipe tosend;
	runloop* loop = NULL;
	uintptr_t idle_id = 0;
	function<void(socket*)> cb_read;
	function<void(socket*)> cb_write; // call once when connection is done, or forever if connection is broken
	
	/*private*/ void cancel()
	{
		child = NULL;
	}
	
	/*private*/ void set_loop()
	{
		if (!child)
		{
			if (dns) return;
			if (!idle_id) idle_id = loop->set_idle(bind_this(&socketwrap::on_idle));
			return;
		}
		child->callback(loop,
		                cb_read            ? bind_this(&socketwrap::on_readable) : NULL,
		                tosend.remaining() ? bind_this(&socketwrap::on_writable) : NULL);
		if (cb_write && !idle_id) idle_id = loop->set_idle(bind_this(&socketwrap::on_idle));
	}
	/*private*/ void trysend()
	{
		if (!child) return set_loop();
		
		arrayview<byte> bytes = tosend.pull_buf();
		int nbytes = child->send(bytes);
		if (nbytes < 0) return cancel();
		tosend.pull_done(nbytes);
		
		set_loop();
	}
	
	int recv(arrayvieww<byte> data)
	{
		if (!child)
		{
			if (dns) return 0;
			return e_broken;
		}
		return child->recv(data);
	}
	
	int send(arrayview<byte> data)
	{
		if (!data) return 0;
		if (!child && !dns) return e_broken;
		tosend.push(data);
		trysend();
		return data.size();
	}
	
	/*private*/ void on_readable(socket*) { cb_read(this); }
	/*private*/ void on_writable(socket*) { trysend(); }
	/*private*/ bool on_idle()
	{
		if (!child && cb_read) cb_read(this);
		else if (cb_write) cb_write(this);
		else { idle_id = 0; return false; }
		return true;
	}
	void callback(runloop* loop, function<void(socket*)> cb_read, function<void(socket*)> cb_write)
	{
		this->loop = loop;
		this->cb_read = cb_read;
		this->cb_write = cb_write;
		set_loop();
	}
	~socketwrap()
	{
		loop->remove(idle_id);
	}
};

} // close namespace

socket* socket::create(cstring domain, int port, runloop* loop)
{
	return new socketwrap(domain, port, loop, false);
}

socket* socket::create_ssl(cstring domain, int port, runloop* loop)
{
	return new socketwrap(domain, port, loop, true);
}

socket* socket::create_udp(cstring domain, int port, runloop* loop)
{
	initialize();
	
	addrinfo * addr = parse_hostname(domain, port, true);
	if (!addr) return NULL;
	
	int fd = mksocket(addr->ai_family, addr->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, addr->ai_protocol);
	socket* ret = new socket_raw_udp(fd, addr->ai_addr, addr->ai_addrlen);
	freeaddrinfo(addr);
	//TODO: add the wrapper
	
	return ret;
}

#if 0
static MAYBE_UNUSED int socketlisten_create_ip4(int port)
{
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	
	int fd = mksocket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) goto fail;
	
	if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) goto fail;
	if (listen(fd, 10) < 0) goto fail;
	return fd;
	
fail:
	close(fd);
	return -1;
}

static int socketlisten_create_ip6(int port)
{
	struct sockaddr_in6 sa; // IN6ADDR_ANY_INIT should work, but doesn't.
	memset(&sa, 0, sizeof(sa));
	sa.sin6_family = AF_INET6;
	sa.sin6_addr = in6addr_any;
	sa.sin6_port = htons(port);
	
	int fd = mksocket(AF_INET6, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, false) < 0) goto fail;
	if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) goto fail;
	if (listen(fd, 10) < 0) goto fail;
	return fd;
	
fail:
	close(fd);
	return -1;
}

socketlisten* socketlisten::create(int port)
{
	initialize();
	
	int fd = -1;
	if (fd<0) fd = socketlisten_create_ip6(port);
#if defined(_WIN32) && _WIN32_WINNT < 0x0600
	//Windows XP can't dualstack the v6 addresses, so let's keep the fallback
	if (fd<0) fd = socketlisten_create_ip4(port);
#endif
	if (fd<0) return NULL;
	
	setblock(fd, false);
	return new socketlisten(fd);
}

socket* socketlisten::accept()
{
	return socket_wrap(::accept(this->fd, NULL,NULL));
}

socketlisten::~socketlisten() { if (loop) loop->set_fd(fd, NULL, NULL); close(this->fd); }
#endif
