#include "socket.h"
#include <stdio.h>

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

static int connect(cstring domain, int port)
{
	initialize();
	
	char portstr[16];
	sprintf(portstr, "%i", port);
	
	addrinfo hints;
	memset(&hints, 0, sizeof(addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	
	addrinfo * addr = NULL;
	getaddrinfo(domain.c_str(), portstr, &hints, &addr);
	if (!addr) return -1;
	
	int fd = mksocket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
#ifndef _WIN32
	//because 30 second pauses are unequivocally detestable
	timeval timeout;
	timeout.tv_sec = 4;
	timeout.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
#endif
	if (connect(fd, addr->ai_addr, addr->ai_addrlen) != 0)
	{
		freeaddrinfo(addr);
		close(fd);
		return -1;
	}
	
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
	
	freeaddrinfo(addr);
	return fd;
}

} // close namespace

//MSG_DONTWAIT is usually better, but accept() and connect() don't take that argument
void socket::setblock(int fd, bool newblock)
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

namespace {

class socket_impl : public socket {
public:
	socket_impl(int fd) : socket(fd), loop(NULL) {}
	
	runloop* loop;
	function<void(socket*)> cb_read;
	function<void(socket*)> cb_write;
	
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
	
	int recv(arrayvieww<byte> data, bool block = false)
	{
#ifdef _WIN32 // for Linux, MSG_DONTWAIT is enough
		socket::setblock(this->fd, block);
#endif
		return fixret(::recv(this->fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL | (block ? 0 : MSG_DONTWAIT)));
	}
	
	int sendp(arrayview<byte> data, bool block = true)
	{
#ifdef _WIN32
		socket::setblock(this->fd, block);
#endif
		return fixret(::send(fd, (char*)data.ptr(), data.size(), MSG_NOSIGNAL | (block ? 0 : MSG_DONTWAIT)));
	}
	
	/*private*/ void on_readable(uintptr_t) { cb_read(this); }
	/*private*/ void on_writable(uintptr_t) { cb_write(this); }
	void callback(runloop* loop, function<void(socket*)> cb_read, function<void(socket*)> cb_write)
	{
		this->loop = loop;
		this->cb_read = cb_read;
		this->cb_write = cb_write;
		loop->set_fd(fd, cb_read ? bind_this(&socket_impl::on_readable) : NULL, cb_write ? bind_this(&socket_impl::on_writable) : NULL);
	}
	
	~socket_impl()
	{
		if (loop) loop->set_fd(fd, NULL, NULL);
		if (fd>=0) close(fd);
	}
};

static socket* socket_wrap(int fd)
{
	if (fd<0) return NULL;
	return new socket_impl(fd);
}

}

socket* socket::create_from_fd(int fd)
{
	return socket_wrap(fd);
}

socket* socket::create(cstring domain, int port)
{
	return socket_wrap(connect(domain, port));
}

//static socket* socket::create_udp(const char * domain, int port);

#ifdef __unix__
#include <poll.h>
#define RD_EV (POLLIN |POLLRDHUP|POLLHUP|POLLERR)
#define WR_EV (POLLOUT|POLLRDHUP|POLLHUP|POLLERR)

void* socket::monitor::select(int timeout_ms)
{
	map<uintptr_t,item> items = std::move(m_items);
	array<pollfd> pfds;
	array<void*> pfds_keys;
	
	for (auto& pair : items)
	{
		socket* sock = (socket*)pair.key;
		bool read = pair.value.read;
		bool write = pair.value.write;
		if (sock->active(read, write)) return pair.value.key;
		
		pollfd& pfd = pfds.append();
		pfd.fd = sock->fd;
		pfd.events = (read ? POLLIN : 0) | (write ? POLLOUT : 0);
		pfd.revents = 0;
		pfds_keys.append(pair.value.key);
	}
	
	poll(pfds.ptr(), pfds.size(), timeout_ms);
	
	for (size_t i=0;i<pfds.size();i++)
	{
		if (pfds[i].revents != 0) return pfds_keys[i];
	}
	
	return NULL;
}
#endif


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
