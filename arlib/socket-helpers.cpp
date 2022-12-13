#ifdef ARLIB_SOCKET
#include "socket.h"
#include <errno.h>

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
#endif
