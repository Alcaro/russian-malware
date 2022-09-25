#ifdef ARLIB_SOCKET
#include "socks5.h"
#include "bytestream.h"

async<autoptr<socket2>> socks5::create_inner(cstring host, uint16_t port)
{
	autoptr<socket2> sock = co_await socket2::create(m_host, m_port);
	if (!sock)
		co_return nullptr;
	
	uint8_t buf[3 + 4+253+2];
	bytestreamw send = buf;
	
	send.u8s(/*version*/ 5, /*nmethods*/ 1, /*anonymous*/ 0); // spec claims GSSAPI support is mandatory, but anon only works in practice
	
	send.u8s(/*version*/ 5, /*cmd=connect*/ 1, /*reserved*/ 0); // spec also says wait between auth and connect, but, again, not necessary
	uint8_t ip_buf[16];
	
	if (cstring domain = socket2::address::parse_domain(host, &port))
	{
		send.u8s(3, domain.length()); // domain
		send.text(domain);
	}
	else if (socket2::address::parse_ipv4(host, ip_buf, &port))
	{
		send.u8(1); // ipv4
		send.bytes(bytesr(ip_buf, 4));
	}
	else if (socket2::address::parse_ipv6(host, ip_buf, &port))
	{
		send.u8(6); // ipv6
		send.bytes(bytesr(ip_buf, 16));
	}
	else co_return nullptr;
	send.u16b(port);
	
	bytesr remaining = send.finish();
	while (remaining) {
		co_await sock->can_send();
		ssize_t n = sock->send_sync(remaining);
		if (n < 0)
			co_return nullptr;
		remaining = remaining.skip(n);
	}
	
	size_t n_needed = 6;
	size_t n_recv = 0;
again:
	while (n_recv < n_needed)
	{
		co_await sock->can_recv();
		ssize_t n_this = sock->recv_sync(bytesw(buf, n_needed).skip(n_recv));
		if (n_this < 0)
			co_return nullptr;
		n_recv += n_this;
	}
	
	static const uint8_t buf_exp[] = {
		/*ver*/5, /*auth*/0,
		/*ver*/5, /*reply=success*/0, /*reserved*/0, /*addrtype (not checked here)*/
	};
	if (!memeq(buf, buf_exp, sizeof(buf_exp)))
		co_return nullptr;
	
	if (buf[5] == 1) // ipv4
	{
		n_needed = 6+4+2;
		if (n_recv < n_needed)
			goto again;
	}
	else if (buf[5] == 4) // ipv6
	{
		n_needed = 6+16+2;
		if (n_recv < n_needed)
			goto again;
	}
	else // spec says server is allowed to claim to bind to a domain name, but I've never seen it and the operation doesn't make sense
		co_return nullptr;
	
	co_return sock;
}

#include "test.h"

co_test("SOCKS5", "tcp", "socks5")
{
	test_skip("too slow");
	test_skip_force("only works on my computer");
	
	socks5 proxy;
	proxy.configure("localhost");
	socketbuf sock;
	
	sock = co_await proxy.create("1.1.1.1", 80);
	assert(sock);
	sock.send("GET / HTTP/1.1\r\nHost: wrong-host.com\r\nConnection: close\r\n\r\n");
	assert_eq(cstring(co_await sock.bytes(8)), "HTTP/1.1");
	
	sock = co_await proxy.create("google.com", 80);
	assert(sock);
	sock.send("GET / HTTP/1.1\r\nHost: wrong-host.com\r\nConnection: close\r\n\r\n");
	assert_eq(cstring(co_await sock.bytes(8)), "HTTP/1.1");
}
#endif
