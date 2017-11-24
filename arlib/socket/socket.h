#pragma once
#include "../global.h"
#include "../containers.h"
#include "../function.h"
#include "../string.h"
#include "../file.h"
#include "../set.h"
#include "../runloop.h"
#include <stdint.h>
#include <string.h>

//TODO: fiddle with https://github.com/ckennelly/hole-punch

#define socket socket_t
class socket : nocopy {
public:
	//Always succeeds immediately, doing the real work asynchronously.
	//If the server doesn't exist or doesn't respond, reports a broken connection.
	//Once the connection is established, it reports writability to its runloop. However, writes before that will succeed.
	static socket* create(cstring domain, int port, runloop* loop);
	static socket* create_ssl(cstring domain, int port, runloop* loop); // TODO: choosable backend, permissiveness, SSL server
	static socket* create_udp(cstring domain, int port, runloop* loop);
	
	//Doesn't return until the connection is established. Not recommended.
	static socket* create_sync(cstring domain, int port);
	
	//The wrapper will buffer write() calls, making them succeed even if the actual socket is full.
	//The normal creation functions create wrappers already. The runloop must be same as given to the child.
	static socket* wrap(socket* inner, runloop* loop);
	//Acts like create(), but no DNS lookups are done, and write() can fail or return partial success.
	//It is also unsafe to send a 0-byte buffer to such a socket. Wrapped sockets ignore them.
	static socket* create_raw(cstring ip, int port, runloop* loop);
	static socket* wrap_ssl(socket* inner, cstring domain, runloop* loop);
	
	
	enum {
		e_lazy_dev = -1, // Whoever implemented this socket handler was lazy. Treat it as e_broken or an unknown error.
		e_closed = -2, // Remote host chose to gracefully close the connection.
		e_broken = -3, // Connection was forcibly torn down.
		e_udp_too_big = -4, // Attempted to process an unacceptably large UDP packet.
		e_ssl_failure = -5, // Certificate validation failed, no algorithms in common, or other SSL error.
		e_not_supported = -6, // Attempted to read or write on a listening socket, or other unsupported operation.
	};
	
	//WARNING: Most socket APIs treat read/write of zero bytes as EOF. Not this one! 0 is EWOULDBLOCK; EOF is an error.
	// This reduces the number of special cases; EOF is usually treated the same way as unknown errors,
	// and EWOULDBLOCK is usually not an error.
	//The first two functions will process at least one byte, or if block is false, at least zero.
	// send() buffers everything internally, and always uses every byte.
	//For UDP sockets, partial reads or writes aren't possible; you always get one or zero packets.
	virtual int recv(arrayvieww<byte> data) = 0;
	int recv(array<byte>& data)
	{
		if (data.size()==0)
		{
			data.resize(4096);
			int bytes = recv((arrayvieww<byte>)data);
			if (bytes >= 0) data.resize(bytes);
			else data.resize(0);
			return bytes;
		}
		else return recv((arrayvieww<byte>)data);
	}
	virtual int send(arrayview<byte> data) = 0;
	
	//The socket will remove its callbacks when destroyed.
	//It is safe to call this multiple times; however, 'loop' must be same for each call.
	//If there's still data available for reading on the socket, the callbacks will be called again.
	//If both read and write are possible and both callbacks are set, read is called; it's implementation defined whether write is too.
	//False positives are possible. Use nonblocking operations.
	//If the socket is closed, it's considered both readable and writable.
	virtual void callback(function<void(socket*)> cb_read, function<void(socket*)> cb_write = NULL) = 0;
	
	virtual ~socket() {}
	
	//TODO: serialize function, usable for both normal and ssl
	//in addition to the usual copyable-data hierarchy, it needs to
	// store/provide fds
	// be destructive, SSL state is only usable once
	// support erroring out, if serialization isn't implemented
};

//SSL feature matrix:
//                      | OpenSSL | SChannel | GnuTLS | BearSSL | TLSe | Comments
//Basic functionality   | Yes     | ?        | Yes    | Yes     | Yes* | TLSe doesn't support SNI
//Nonblocking           | Yes     | ?        | Yes    | Yes     | Yes  | OpenSSL supports nonblocking, but not blocking
//Permissive (expired)  | Yes     | ?        | Yes    | No      | No
//Permissive (bad root) | Yes     | ?        | Yes    | Yes     | No
//Permissive (bad name) | Yes     | ?        | Yes    | No      | No
//Serialize             | No      | No       | No     | Yes*    | No   | TLSe claims to support it, but I can't get it working
//                                                                     | BearSSL is homemade and will need rewrites if upstream changes
//Server                | No      | No       | No     | No      | No   | Likely possible on everything, I'm just lazy
//Reputable author      | Yes     | Yes      | Yes    | Yes     | No
//Binary size           | 4       | 2.5      | 4      | 80      | 169  | In kilobytes, estimated; DLLs not included
