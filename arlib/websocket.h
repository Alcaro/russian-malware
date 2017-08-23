#ifdef ARLIB_SOCKET
#pragma once
#include "socket.h"

class WebSocket : nocopy {
	autoptr<socket> sock;
	array<byte> msg;
	
	void fetch(bool block);
	bool poll(bool block, array<byte>* ret);
	
	void send(arrayview<byte> message, bool binary);
	
public:
	bool connect(cstring target, arrayview<string> headers = NULL);
	
	bool ready() { return poll(false, NULL); }
	void await() { while (!poll(true, NULL)) {} }
	array<byte> recv(bool block = false);
	string recvstr(bool block = false) { return (string)recv(block); }
	
	void send(arrayview<byte> message) { send(message, true); }
	void send(cstring message) { send(message.bytes(), false); }
	
	bool isOpen() { return sock; }
	void close() { sock = NULL; }
	void reset() { close(); msg.reset(); }
	
	operator bool() { return isOpen(); }
	
	//If this key is returned, call .recv(). May not necessarily return anything.
	void monitor(socket::monitor& mon, void* key) { if (sock) mon.add(sock, key, true, false); }
};

#endif
