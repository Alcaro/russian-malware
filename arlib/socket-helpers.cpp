#ifdef ARLIB_SOCKET
#include "socket.h"
#include <errno.h>

// some of this should be deduplicated into calling each other, but not until compilers are better at inlining coroutines
async<socket2::address> socket2::dns_port(cstring host, uint16_t port)
{
	cstring domain = socket2::address::split_port(host, &port);
	co_return (co_await socket2::dns(domain)).with_port(port);
}
async<autoptr<socket2>> socket2::create(cstring host, uint16_t port)
{
	cstring domain = socket2::address::split_port(host, &port);
	address ip = (co_await socket2::dns(domain)).with_port(port);
	if (!ip) { errno = ENOENT; co_return nullptr; }
	co_return co_await socket2::create(ip);
}
#ifdef ARLIB_SSL
async<autoptr<socket2>> socket2::create_ssl(cstring host, uint16_t port)
{
	cstring domain = socket2::address::split_port(host, &port);
	address ip = (co_await socket2::dns(domain)).with_port(port);
	autoptr<socket2> sock = co_await socket2::create(ip);
	co_return co_await socket2::wrap_ssl(std::move(sock), domain);
}
#endif
async<autoptr<socket2>> socket2::wrap_sslmaybe(bool ssl, autoptr<socket2> sock, cstring domain)
{
	if (!ssl)
		co_return sock;
#ifdef ARLIB_SSL
	co_return co_await socket2::wrap_ssl(std::move(sock), domain);
#else
	co_return nullptr;
#endif
}

async<autoptr<socket2>> socket2::create_sslmaybe(bool ssl, cstring host, uint16_t port)
{
	// mostly same as the above, but let's copypaste until compilers inline coroutines properly
#ifndef ARLIB_SSL
	if (ssl)
		co_return nullptr;
#endif
	cstring domain = socket2::address::split_port(host, &port);
	address ip = (co_await socket2::dns(domain)).with_port(port);
	if (!ip) co_return nullptr;
	autoptr<socket2> sock = co_await socket2::create(ip);
	if (!ssl)
		co_return sock;
#ifdef ARLIB_SSL
	co_return co_await socket2::wrap_ssl(std::move(sock), domain);
#else
	co_return nullptr;
#endif
}



void socketbuf::socket_failed()
{
	send_wait.cancel();
	recv_wait.cancel();
	sock = nullptr;
	if (send_prod.has_waiter())
		RETURN_IF_CALLBACK_DESTRUCTS(send_prod.complete(false));
	if (recv_op != op_none)
	{
		op_t op = recv_op;
		recv_op = op_none;
		// the numeric branches yield identical machine code and should be merged
		// (they aren't)
		if (op == op_u8)
			recv_prod.get<producer_t<uint8_t>>().inner.complete(0);
		else if (op <= op_u16l)
			recv_prod.get<producer_t<uint16_t>>().inner.complete(0);
		else if (op <= op_u32l)
			recv_prod.get<producer_t<uint32_t>>().inner.complete(0);
		else if (op <= op_u64l)
			recv_prod.get<producer_t<uint64_t>>().inner.complete(0);
		else if (op == op_bytesr)
			recv_prod.get<producer_t<bytesr>>().inner.complete(nullptr);
		else if (op == op_line)
			recv_prod.get<producer_t<cstring>>().inner.complete("");
		else
			__builtin_unreachable();
	}
}
void socketbuf::recv_ready()
{
	ssize_t n = sock->recv_sync(recv_by.push_begin(1024));
	if (n < 0)
		socket_failed();
	if (n < 0 || !sock) // don't read sock after calling socket_failed, it could've deleted this
		return;
	size_t prev_size = recv_by.size();
	recv_by.push_finish(n);
	recv_complete(prev_size);
}
void socketbuf::recv_complete(size_t prev_size)
{
	if (!sock)
	{
		socket_failed();
		return;
	}
	if (recv_op == op_line)
	{
		bytesr line = recv_by.pull_line(prev_size);
		if (line || recv_by.size() >= recv_bytes)
		{
			this->recv_op = op_none;
			recv_prod.get<producer_t<cstring>>().inner.complete(line);
		}
		else
			sock->can_recv().then(&recv_wait);
	}
	else if (recv_op == op_bytesr_partial && recv_by.size() >= 1)
	{
		this->recv_op = op_none;
		recv_prod.get<producer_t<bytesr>>().inner.complete(recv_by.pull(min(recv_bytes, recv_by.size())));
	}
	else if (recv_by.size() >= recv_bytes)
	{
		op_t op = this->recv_op;
		this->recv_op = op_none;
		// don't bother destructing these guys, their dtor is empty anyways (other than an assert)
		if (op == op_u8) recv_prod.get<producer_t<uint8_t>>().inner.complete(readu_le8(recv_by.pull(1).ptr()));
		else if (op == op_u16b) recv_prod.get<producer_t<uint16_t>>().inner.complete(readu_be16(recv_by.pull(2).ptr()));
		else if (op == op_u16l) recv_prod.get<producer_t<uint16_t>>().inner.complete(readu_le16(recv_by.pull(2).ptr()));
		else if (op == op_u32b) recv_prod.get<producer_t<uint32_t>>().inner.complete(readu_be32(recv_by.pull(4).ptr()));
		else if (op == op_u32l) recv_prod.get<producer_t<uint32_t>>().inner.complete(readu_le32(recv_by.pull(4).ptr()));
		else if (op == op_u64b) recv_prod.get<producer_t<uint64_t>>().inner.complete(readu_be64(recv_by.pull(8).ptr()));
		else if (op == op_u64l) recv_prod.get<producer_t<uint64_t>>().inner.complete(readu_le64(recv_by.pull(8).ptr()));
		else if (op == op_bytesr) recv_prod.get<producer_t<bytesr>>().inner.complete(recv_by.pull(recv_bytes));
		else __builtin_unreachable();
	}
	else
	{
		sock->can_recv().then(&recv_wait);
	}
}

bytesr socketbuf::bytes_sync(size_t n)
{
	if (!sock)
		return {};
	bytesr by1 = recv_by.pull_begin(1);
	if (by1)
	{
		n = min(n, by1.size());
		recv_by.pull_finish(n);
		return by1.slice(0, n);
	}
	bytesw by2 = recv_by.push_begin(1);
	if (by2.size() > n)
		by2 = by2.slice(0, n);
	ssize_t amt = sock->recv_sync(by2);
	if (amt < 0)
	{
		socket_failed();
		return {};
	}
	return by2.slice(0, amt);
}

void socketbuf::send_ready()
{
	if (!sock)
		return;
	ssize_t n = sock->send_sync(send_by.pull_begin());
	if (n < 0)
		socket_failed();
	if (!sock)
		return;
	send_by.pull_finish(n);
	send_prepare();
}
void socketbuf::send_prepare()
{
	if (send_by.size())
	{
		if (!send_wait.is_waiting())
			sock->can_send().then(&send_wait);
	}
	else
	{
		if (send_prod.has_waiter())
			send_prod.complete(true);
	}
}
void socketbuf::send(bytesr by)
{
	if (!sock)
		return;
	if (LIKELY(send_by.size() == 0))
	{
		ssize_t n = sock->send_sync(by);
		if (n < 0)
		{
			socket_failed();
			return;
		}
		by = by.skip(n);
		if (LIKELY(!by))
			return;
	}
	send_by.push(by);
	send_prepare();
}

void socketbuf::moved()
{
	recv_wait.moved();
	if (recv_op)
	{
		if (recv_op == op_u8) recv_prod.get<producer_t<uint8_t>>().inner.moved();
		else if (recv_op == op_u16b) recv_prod.get<producer_t<uint16_t>>().inner.moved();
		else if (recv_op == op_u16l) recv_prod.get<producer_t<uint16_t>>().inner.moved();
		else if (recv_op == op_u32b) recv_prod.get<producer_t<uint32_t>>().inner.moved();
		else if (recv_op == op_u32l) recv_prod.get<producer_t<uint32_t>>().inner.moved();
		else if (recv_op == op_u64b) recv_prod.get<producer_t<uint64_t>>().inner.moved();
		else if (recv_op == op_u64l) recv_prod.get<producer_t<uint64_t>>().inner.moved();
		else if (recv_op == op_bytesr) recv_prod.get<producer_t<bytesr>>().inner.moved();
		else __builtin_unreachable();
	}
	send_wait.moved();
	send_prod.moved();
}
#endif
