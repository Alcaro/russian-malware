#pragma once
#include "socket.h"
#include "bytestream.h"
#include "stringconv.h"

struct socks5_par {
	runloop* loop;
	socket* to_proxy;
	string target;
	uint16_t port;
};
socket* wrap_socks5(const socks5_par& param);

class socks5 {
	string m_host;
	uint16_t m_port;
	
public:
	void configure(cstring host, int port, runloop* loop) {
		//ignore runloop, but demand it because everything else does
		m_host = host;
		m_port = port;
	}
	bool configure(cstring hostport, runloop* loop) {
		array<cstring> parts = hostport.csplit<1>(":");
		m_host = parts[0];
		if (parts.size() == 1) { m_port = 1080; return true; }
		if (parts.size() != 2) return false;
		if (!fromstring(parts[1], m_port)) return false;
		return true;
	}
	
	socket* connect(bool ssl, cstring domain, int port, runloop* loop);
};
