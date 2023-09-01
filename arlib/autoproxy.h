#pragma once
#include "socket.h"

class autoproxy {
	autoproxy() = delete;
public:
	static async<autoptr<socket2>> socket_create(cstring domain, uint16_t port);
#ifdef ARLIB_SSL
	static inline async<autoptr<socket2>> socket_create_ssl(cstring host, uint16_t port)
	{
		cstring domain = socket2::address::split_port(host, &port);
		co_return co_await socket2::wrap_ssl(co_await socket_create(domain, port), domain);
	}
#endif
	static inline async<autoptr<socket2>> socket_create_sslmaybe(bool ssl, cstring host, uint16_t port)
	{
		cstring domain = socket2::address::split_port(host, &port);
		co_return co_await socket2::wrap_sslmaybe(ssl, co_await socket_create(domain, port), domain);
	}
};
