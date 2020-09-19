#pragma once
#ifdef ARLIB_SOCKET
#include "socket.h"
#include "bytepipe.h"

class WebSocket : nocopy {
	runloop* loop;
	autoptr<socket> sock;
	bytepipe tosend; // used to avoid sending stuff before the handshake finishes, server throws Protocol Error if I try
	
	enum {
		t_text = 1,
		t_binary = 2,
		
		t_close = 8,
		t_ping = 9,
		t_pong = 10
	};
	
	array<uint8_t> msg;
	
	bool inHandshake;
	bool gotFirstLine; // HTTP/1.1 101 Switching Protocols
	//TODO: Sec-WebSocket-Accept https://developer.mozilla.org/en-US/docs/Web/API/WebSockets_API/Writing_WebSocket_servers
	
	bool break_callback;
	
	function<void(cstring)> cb_str;
	function<void(arrayview<uint8_t>)> cb_bin;
	function<void()> cb_error;
	
#ifdef ARLIB_SSL
	function<socket*(bool ssl, cstring domain, int port, runloop* loop)> cb_mksock = socket::create_sslmaybe;
#else
	function<socket*(cstring domain, int port, runloop* loop)> cb_mksock = socket::create;
#endif
	
	
	void activity();
	void send(arrayview<uint8_t> message, int type);
	void cancel() { sock = NULL; cb_error(); }
	
public:
	WebSocket(runloop* loop) : loop(loop) {}
	void connect(cstring target, arrayview<string> headers = NULL);
	
	//A custom socket creation function, if you want proxy support.
#ifdef ARLIB_SSL
	void wrap_socks(function<socket*(bool ssl, cstring domain, int port, runloop* loop)> cb) { cb_mksock = cb; }
#else
	void wrap_socks(function<socket*(cstring domain, int port, runloop* loop)> cb) { cb_mksock = cb; }
#endif
	
	void send(arrayview<uint8_t> message) { send(message, t_binary); }
	void send(cstring message) { send(message.bytes(), t_text); }
	
	//It's fine to call both of those if you need to keep text/binary data apart. If you don't, both will go to the same one.
	//Do this before connect(), or you may miss events, for example if the target is unparseable.
	void callback(function<void(cstring           )> cb_str, function<void()> cb_error) { this->cb_str = cb_str; this->cb_error = cb_error; }
	void callback(function<void(arrayview<uint8_t>)> cb_bin, function<void()> cb_error) { this->cb_bin = cb_bin; this->cb_error = cb_error; }
	
	bool isOpen() { return sock; }
	void reset() { sock = NULL; tosend.reset(); msg.reset(); break_callback = true; }
	
	operator bool() { return isOpen(); }
};

#endif
