#pragma once
#include "../global.h"
#include "../containers.h"
#include "../function.h"
#include "../string.h"
#include "../file.h"
#include "../set.h"
#include <stdint.h>
#include <string.h>

//TODO: fiddle with https://github.com/ckennelly/hole-punch

//TODO: http, websocket and async socket should have pointer to fd monitor, which they auto update
//maybe all sockets should, probably easier. no point saving memory, sockets have fds which are expensive

#define socket socket_t
class socket : nocopy {
protected:
	socket(int fd) : fd(fd) {}
	int fd; // Used by select().
	
	//deallocates the socket, returning its fd, while letting the fd remain valid
	static int decompose(socket* * sock) { int ret = (*sock)->fd; (*sock)->fd=-1; delete *sock; *sock = NULL; return ret; }
	static int decompose(autoptr<socket> * sock) { int ret = (*sock)->fd; (*sock)->fd=-1; *sock = NULL; return ret; }
	
	static void setblock(int fd, bool newblock);
	
public:
	//Returns NULL on connection failure.
	static socket* create(cstring domain, int port);
	//Always succeeds. If the server can't be contacted (including DNS failure), returns failure on first write or read.
	//WARNING: DNS lookup is currently synchronous.
	static socket* create_async(cstring domain, int port);
	//Always succeeds. If the server can't be contacted, may return e_broken at some point, or may just discard everything.
	static socket* create_udp(cstring domain, int port);
	
	enum {
		e_lazy_dev = -1, // Whoever implemented this socket handler was lazy and just returned -1. Treat it as e_broken or an unknown error.
		e_closed = -2, // Remote host chose to gracefully close the connection.
		e_broken = -3, // Connection was forcibly torn down.
		e_udp_too_big = -4, // Attempted to process an unacceptably large UDP packet.
		e_ssl_failure = -5, // Certificate validation failed, no algorithms in common, or other SSL error.
		e_not_supported = -6, // Attempted to read or write on a listening socket, or other unsupported operation.
	};
	
	//WARNING: Most socket APIs treat read/write of zero bytes as EOF. Not this one! 0 is EWOULDBLOCK; EOF is an error.
	// This reduces the number of special cases; EOF is usually treated the same way as unknown errors, and EWOULDBLOCK is usually not an error.
	//The first two functions will process at least one byte, or if block is false, at least zero. send() sends all bytes before returning.
	//For UDP sockets, partial reads or writes aren't possible; you always get one or zero packets.
	virtual int recv(arrayvieww<byte> data, bool block = false) = 0;
	int recv(array<byte>& data, bool block = false)
	{
		if (data.size()==0)
		{
			data.resize(4096);
			int bytes = recv((arrayvieww<byte>)data, block);
			if (bytes >= 0) data.resize(bytes);
			else data.resize(0);
			return bytes;
		}
		else return recv((arrayvieww<byte>)data, block);
	}
	virtual int sendp(arrayview<byte> data, bool block = true) = 0;
	
	int send(arrayview<byte> bytes)
	{
		const byte * data = bytes.ptr();
		unsigned int len = bytes.size();
		unsigned int sent = 0;
		while (sent < len)
		{
			int here = this->sendp(arrayview<byte>(data+sent, len-sent));
			if (here<0) return here;
			sent += here;
		}
		return len;
	}
	
	//Convenience functions for handling textual data.
	//maybe<string> recvstr(bool block = false)
	//{
	//	maybe<array<byte>> ret = this->recv(block);
	//	if (!ret) return maybe<string>(NULL, ret.error);
	//	return maybe<string>((string)ret.value);
	//}
	int sendp(cstring data, bool block = true) { return this->sendp(data.bytes(), block); }
	int send(cstring data) { return this->send(data.bytes()); }
	
	virtual ~socket() {}
	
	//Can be used to keep a socket alive across exec(). Don't use for an SSL socket, use serialize() instead.
	static socket* create_from_fd(int fd);
	int get_fd() { return fd; }
	
	//Returns whether the object has buffers such that recv() or send() will return immediately, even if the fd doesn't claim it will.
	//If select(2) will return this one, this function isn't needed.
	virtual bool active(bool want_recv, bool want_send) { return false; }
	
	//Note that these may return false positives. Use nonblocking operations.
	//Resets after select().
	class monitor {
		struct item { void* key; bool read; bool write; };
		map<uintptr_t,item> m_items;
	public:
		void add(socket* sock, void* key, bool read = true, bool write = false)
		{
			item i = { key, read, write };
			m_items.insert((uintptr_t)sock, i);
		}
		void* select(int timeout_ms = -1);
	};
	
	static size_t select(arrayview<socket*> socks, bool* can_read, bool* can_write, int timeout_ms = -1);
	static size_t select(arrayview<socket*> socks, int timeout_ms = -1) { bool x; return select(socks, &x, NULL, timeout_ms); }
};


//SSL feature matrix:
//                      | OpenSSL | SChannel | GnuTLS | BearSSL | TLSe | Comments
//Basic functionality   | Yes     | ?        | Yes    | Yes     | Yes* | TLSe doesn't support SNI
//Nonblocking           | Yes     | ?        | Yes    | Yes     | Yes  | OpenSSL supports nonblocking, but not blocking
//Permissive (expired)  | Yes     | ?        | Yes    | No      | No
//Permissive (bad root) | Yes     | ?        | Yes    | Yes     | No
//Permissive (bad name) | Yes     | ?        | Yes    | No      | No   | Bad names are very rare outside testing
//Serialize             | No      | No       | No     | Yes*    | No   | TLSe claims to support it, but I can't get it working
//                                                                     | BearSSL is homemade and will need rewrites if upstream changes
//Server                | No      | No       | No     | No      | No   | Likely possible on everything, I'm just lazy
//Reputable author      | Yes     | Yes      | Yes    | Yes     | No
//Binary size           | 4       | 2.5      | 4      | 80      | 169  | In kilobytes, estimated; DLLs not included
class socketssl : public socket {
protected:
	socketssl(int fd) : socket(fd) {}
public:
	//If 'permissive' is false and the cert is untrusted (expired, bad root, wrong domain, etc), returns NULL.
	static socketssl* create(cstring domain, int port, bool permissive=false)
	{
		return socketssl::create(socket::create(domain, port), domain, permissive);
	}
	//On entry, this takes ownership of the socket. Even if connection fails, the socket may not be used anymore.
	//The socket must be a normal TCP socket; UDP and nested SSL is not supported.
	//socket::create_async counts as normal. However, this may return success immediately, even if the server certificate is bad.
	// In this case, recv() will never succeed, and send() will never send anything to the server (but may buffer data internally).
	static socketssl* create(socket* parent, cstring domain, bool permissive=false);
	
	//set_cert or set_cert_cb must be called before read or write.
	static socketssl* create_server(socket* parent);
	//Only usable on server sockets.
	void set_cert(array<byte> data); // Must be called exactly once.
	void set_cert_cb(function<void(socketssl* sock, cstring hostname)> cb); // Used for SNI. The callback must call set_cert.
	
	//Can be used to keep a socket alive across exec().
	//If successful, serialize() returns the the file descriptor needed to unserialize, and the socket is deleted.
	//On failure, returns empty and nothing happens.
	//Only available under some implementation. Non-virtual, to fail in linker rather than runtime.
	array<byte> serialize(int* fd);
	static socketssl* deserialize(int fd, arrayview<byte> data);
};

//socket::select() works on these, but recv/send will fail
class socketlisten : public socket {
	socketlisten(int fd) : socket(fd) { this->fd = fd; }
public:
	static socketlisten* create(int port);
	socket* accept();
	~socketlisten();
	
	int recv(arrayvieww<byte> data, bool block) { return e_not_supported; }
	int sendp(arrayview<byte> data, bool block) { return e_not_supported; }
};
