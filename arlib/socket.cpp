#ifdef ARLIB_SOCKET
#include "socket.h"

#undef socket
#ifdef __unix__
	#include <sys/socket.h>
	#include <netinet/tcp.h>
	#include <arpa/inet.h>
	#include <fcntl.h>
	#include <errno.h>
#else
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#define MSG_NOSIGNAL 0
	#define MSG_DONTWAIT 0
	#define SOCK_CLOEXEC 0
	#define SOCK_NONBLOCK 0
	#define close closesocket
	#ifdef _MSC_VER
		#pragma comment(lib, "ws2_32.lib")
	#endif
#endif

static SOCKET mksocket(int domain, int type, int protocol)
{
#ifndef _WIN32
	return socket(domain, type, protocol);
#else
	return WSASocket(domain, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
#endif
}

namespace {

#ifdef _WIN32
oninit_static()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData); // why
}
#endif

#ifndef _WIN32 // unneeded with WSAfoobar() overlapped functions
static int fixret(int ret)
{
	if (ret > 0) return ret;
	if (ret == 0) return -1;
#ifdef __unix__
	if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
#else
	if (ret < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return 0;
#endif
	return -1;
}
#endif

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

#ifdef __unix__
class socket2_impl : public socket2 {
public:
	SOCKET fd;
	
	socket2_impl(SOCKET fd) : fd(fd) {}
	
	bytesw recv_by;
	async<ssize_t> recv(bytesw by) override
	{
		recv_by = by;
		runloop2::await_read(fd).then(&recv_w);
		return &recv_p;
	}
	struct recv_pt : public producer_fn<ssize_t, recv_pt> {
		void cancel() { container_of<&socket2_impl::recv_p>(this)->cancel_recv(); }
	} recv_p;
	struct recv_wt : public waiter_fn<void, recv_wt> {
		void complete() { container_of<&socket2_impl::recv_w>(this)->complete_recv(); }
	} recv_w;
	void complete_recv()
	{
		recv_p.complete(fixret(::recv(fd, (char*)recv_by.ptr(), recv_by.size(), MSG_DONTWAIT|MSG_NOSIGNAL)));
	}
	void cancel_recv() { recv_w.cancel(); }
	
	bytesr send_by;
	async<ssize_t> send(bytesr by) override
	{
		int n = fixret(::send(fd, (char*)by.ptr(), by.size(), MSG_DONTWAIT|MSG_NOSIGNAL));
		if (n != 0)
		{
			send_p.complete(n);
			return &send_p;
		}
		else
		{
			send_by = by;
			runloop2::await_write(fd).then(&send_w);
			return &send_p;
		}
	}
	struct send_pt : public producer_fn<ssize_t, send_pt> {
		void cancel() { container_of<&socket2_impl::send_p>(this)->cancel_send(); }
	} send_p;
	struct send_wt : public waiter_fn<void, send_wt> {
		void complete() { container_of<&socket2_impl::send_w>(this)->complete_send(); }
	} send_w;
	void complete_send()
	{
		send_p.complete(fixret(::send(fd, (char*)send_by.ptr(), send_by.size(), MSG_DONTWAIT|MSG_NOSIGNAL)));
	}
	void cancel_send() { send_w.cancel(); }
};
#endif
#ifdef _WIN32
class socket2_impl : public socket2 {
public:
	SOCKET fd;
	
	socket2_impl(SOCKET fd) : fd(fd) {}
	
	DWORD flags;
	WSAOVERLAPPED ov_rd = {};
	async<ssize_t> recv(bytesw by) override
	{
		WSABUF buf = { (ULONG)by.size(), (char*)by.ptr() }; // why does this guy have length first
		flags = 0;
		WSARecv(fd, &buf, 1, nullptr, &flags, &ov_rd, [](DWORD dwError, DWORD cbTransferred, WSAOVERLAPPED* lpOverlapped, DWORD dwFlags) {
			if (dwError == ERROR_OPERATION_ABORTED)
				return;
			socket2_impl* self = container_of<&socket2_impl::ov_rd>(lpOverlapped);
			self->recv_p.complete(cbTransferred);
		});
		return &recv_p;
	}
	struct recv_pt : public producer_fn<ssize_t, recv_pt> {
		void cancel() { container_of<&socket2_impl::recv_p>(this)->cancel_recv(); }
	} recv_p;
	void cancel_recv() { CancelIoEx((HANDLE)fd, &ov_rd); }
	
	WSAOVERLAPPED ov_wr = {};
	async<ssize_t> send(bytesr by) override
	{
		WSABUF buf = { (ULONG)by.size(), (char*)by.ptr() };
		WSASend(fd, &buf, 1, nullptr, 0, &ov_wr, [](DWORD dwError, DWORD cbTransferred, WSAOVERLAPPED* lpOverlapped, DWORD dwFlags) {
			socket2_impl* self = container_of<&socket2_impl::ov_wr>(lpOverlapped);
			self->send_p.complete(cbTransferred);
		});
		return &send_p;
	}
	struct send_pt : public producer_fn<ssize_t, send_pt> {
		void cancel() { container_of<&socket2_impl::send_p>(this)->cancel_send(); }
	} send_p;
	void cancel_send() { CancelIoEx((HANDLE)fd, &ov_wr); }
};
#endif
}


async<autoptr<socket2>> socket2::create(address addr)
{
	if (!addr)
		co_return nullptr;
	
	SOCKET fd = mksocket(addr.as_native()->sa_family, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0);
	if (fd < 0)
		co_return nullptr;
#ifndef _WIN32
	//because 30 second pauses are unequivocally detestable
	timeval timeout;
	timeout.tv_sec = 4;
	timeout.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
	if (connect(fd, addr.as_native(), sizeof(addr)) != 0 && errno != EINPROGRESS)
#else
	setblock(fd, false);
	if (connect(fd, addr.as_native(), sizeof(addr)) != 0 && WSAGetLastError() != WSAEWOULDBLOCK)
#endif
	{
	fail:
		close(fd);
		co_return nullptr;
	}
	
	// Nagle's algorithm - made sense in the 80s, but these days, the saved bandwidth isn't worth the added latency.
	// Better combine stuff in userspace, syscalls are (relatively) expensive these days.
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, true);
	
#ifndef _WIN32
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, 1); // enable
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, 3); // ping count before the kernel gives up
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, 30); // seconds idle until it starts pinging
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, 10); // seconds per ping once the pings start
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
	
#ifdef __unix__
	co_await runloop2::await_write(fd);
#endif
#ifdef _WIN32
	WSAEVENT ev;
	contextmanager(ev = WSACreateEvent(), WSACloseEvent(ev))
	{
		WSAEventSelect(fd, ev, FD_CONNECT);
		co_await runloop2::await_handle(ev);
	}
#endif
	
	int error = 0;
	socklen_t len = sizeof(error);
	getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&error, &len);
	if (error != 0)
		goto fail;
	
	co_return new socket2_impl(fd);
}

autoptr<socket2_udp> socket2_udp::create(socket2::address ip)
{
	SOCKET fd = mksocket(ip.as_native()->sa_family, SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0);
	if (fd < 0)
		return nullptr;
	return new socket2_udp(fd, ip);
}

#ifdef __unix__
void socket2_udp::complete_recv()
{
	socklen_t addrsize = recv_sender ? sizeof(*recv_sender) : 0;
	int n = fixret(::recvfrom(fd, (char*)recv_by.ptr(), recv_by.size(), MSG_DONTWAIT|MSG_NOSIGNAL, recv_sender->as_native(), &addrsize));
	if (n != 0)
		recv_p.complete(n);
	else
		runloop2::await_read(fd).then(&recv_w);
}
#endif
#ifdef _WIN32
async<ssize_t> socket2_udp::recv(bytesw by, socket2::address* sender)
{
	WSABUF buf = { (ULONG)by.size(), (char*)by.ptr() }; // why does this guy have length first
	sock_len = sizeof(*sender);
	flags = 0;
	WSARecvFrom(fd, &buf, 1, nullptr, &flags, sender->as_native(), &sock_len, &ov_rd,
		[](DWORD dwError, DWORD cbTransferred, WSAOVERLAPPED* lpOverlapped, DWORD dwFlags) {
			if (dwError == ERROR_OPERATION_ABORTED)
				return;
			socket2_udp* self = container_of<&socket2_udp::ov_rd>(lpOverlapped);
			self->recv_p.complete(cbTransferred);
		});
	return &recv_p;
}
void socket2_udp::cancel_recv() { CancelIoEx((HANDLE)fd, &ov_rd); }
#endif

void socket2_udp::send(bytesr by)
{
	::sendto(fd, (char*)by.ptr(), by.size(), MSG_DONTWAIT|MSG_NOSIGNAL, addr.as_native(), sizeof(addr));
}


void socketbuf::complete(ssize_t n)
{
	if (n < 0)
		sock = nullptr;
	if (!sock)
		return complete_null();
	size_t prev_size = by.size();
	by.push_finish(n);
	complete2(prev_size);
}
void socketbuf::complete2(size_t prev_size)
{
	if (!sock)
		return complete_null();
	if (op == op_line)
	{
		bytesr line = by.pull_line(prev_size);
		if (line || by.size() >= need_bytes)
			prod.get<producer_t<cstring>>().complete(line);
		else
			sock->recv(by.push_begin(1)).then(&wait);
	}
	else if (op == op_bytesr_partial && by.size() >= 1)
	{
		prod.get<producer_t<bytesr>>().complete(by.pull(min(need_bytes, by.size())));
	}
	else if (by.size() >= need_bytes)
	{
		// don't bother destructing these guys, their dtor is empty anyways
		if (op == op_u8) prod.get<producer_t<uint8_t>>().complete(readu_le8(by.pull(1).ptr()));
		else if (op == op_u16b) prod.get<producer_t<uint16_t>>().complete(readu_be16(by.pull(2).ptr()));
		else if (op == op_u16l) prod.get<producer_t<uint16_t>>().complete(readu_le16(by.pull(2).ptr()));
		else if (op == op_u32b) prod.get<producer_t<uint32_t>>().complete(readu_be32(by.pull(4).ptr()));
		else if (op == op_u32l) prod.get<producer_t<uint32_t>>().complete(readu_le32(by.pull(4).ptr()));
		else if (op == op_u64b) prod.get<producer_t<uint64_t>>().complete(readu_be64(by.pull(8).ptr()));
		else if (op == op_u64l) prod.get<producer_t<uint64_t>>().complete(readu_le64(by.pull(8).ptr()));
		else if (op == op_bytesr) prod.get<producer_t<bytesr>>().complete(by.pull(need_bytes));
		else __builtin_unreachable();
	}
	else
	{
		sock->recv(by.push_begin(need_bytes - by.size())).then(&wait);
	}
}
void socketbuf::complete_null()
{
	// these branches yield identical machine code and should be merged
	// (they won't)
	if (op == op_u8)
		prod.get<producer_t<uint8_t>>().complete(0);
	else if (op <= op_u16l)
		prod.get<producer_t<uint16_t>>().complete(0);
	else if (op <= op_u32l)
		prod.get<producer_t<uint32_t>>().complete(0);
	else if (op <= op_u64l)
		prod.get<producer_t<uint64_t>>().complete(0);
	else if (op == op_bytesr)
		prod.get<producer_t<bytesr>>().complete(nullptr);
	else if (op == op_line)
		prod.get<producer_t<cstring>>().complete("");
	else
		__builtin_unreachable();
}
#endif
