#ifdef ARLIB_SOCKET
#pragma once
#include "../global.h"
#include "../array.h"
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
	static socket* create_ssl(cstring domain, int port, runloop* loop); // TODO: choosable backend, server
	// To avoid amplification attacks, any homemade protocol must have the first packet longer than the first reply.
	// For example, it can contain a 256 character message explaining that this is a 256 byte message to avoid amplification attacks.
	static socket* create_udp(cstring domain, int port, runloop* loop);
	
	//Acts like create(), but no DNS lookups are done, and write() can fail or return partial success.
	//It is also unsafe to send a 0-byte buffer to such a socket. Wrapped sockets ignore them.
	//Unless documented otherwise, no funtion can accept a raw socket.
	static socket* create_raw(cstring ip, int port, runloop* loop);
	//The wrapper will buffer write() calls, making them succeed even if the actual socket is full.
	//The normal creation functions create wrappers already. The runloop must be same as given to the child.
	//(Can be used with a raw socket, of course.)
	static socket* wrap(socket* inner, runloop* loop);
	
	//Super simple function, just calls create() or create_ssl().
	//Suitable as default implementation of a 'create socket' callback. Said callback should be expected to return a non-raw socket.
	static socket* create_sslmaybe(bool ssl, cstring domain, int port, runloop* loop);
	
	//Wraps the socket in a SSL layer. This function can accept a raw socket.
	static socket* wrap_ssl(socket* inner, cstring domain, runloop* loop);
	//Like the above, but with create_raw's caveats.
	static socket* wrap_ssl_raw(socket* inner, cstring domain, runloop* loop);
	//Like the above, but does not validate the certificate. Generally a bad idea. Does not exist under all SSL backends.
	static socket* wrap_ssl_raw_noverify(socket* inner, runloop* loop);
	
	
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
	//send() buffers everything internally, and always uses every byte.
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
	
	//It is safe to call this multiple times.
	//If there's still data available for reading on the socket, the callbacks will be called again.
	//If both read and write are possible and both callbacks are set, read is called; it's implementation defined whether write is too.
	//False positives are possible. Be prepared for the actual operations returning empty.
	//If the socket is closed or an error occurs, the read callback is called at least once (or, if not set, write callback is called).
	//The socket should be deleted after recv/send returns an error. Not doing so is undefined behavior.
	//cb_write should be NULL most of the time, or it will be called way too often.
	virtual void callback(function<void()> cb_read, function<void()> cb_write = NULL) = 0;
	
	virtual ~socket() {}
	
	//TODO: serialize function, usable for both normal and ssl
	//in addition to the usual copyable-data hierarchy, it needs to
	// store/provide fds
	// be destructive, SSL state is only usable once
	// support erroring out, if serialization isn't implemented
	
	
	// Network byte order.
	static string ip_to_string(arrayview<byte> bytes);
	static array<byte> string_to_ip(cstring str);
	// The buffer must be at least 16 bytes. Returns bytes actually used (4 or 16, or 0 for error).
	static int string_to_ip(arrayvieww<byte> out, cstring str);
	static bool string_to_ip4(arrayvieww<byte> out, cstring str);
	static bool string_to_ip6(arrayvieww<byte> out, cstring str);
};

//SSL feature matrix:
//                      | OpenSSL | SChannel | GnuTLS | BearSSL | TLSe | Comments
//Basic functionality   | Yes     | No       | No     | Yes     | No   | Many are bitrotted (likely easy to fix)
//Nonblocking           | Yes     | ?        | Yes    | Yes     | Yes  | OpenSSL supports nonblocking, but not blocking
//Permissive (expired)  | Yes     | ?        | Yes    | No      | No
//Permissive (bad root) | Yes     | ?        | Yes    | Yes     | No
//Permissive (bad name) | Yes     | ?        | Yes    | No      | No
//Serialize             | No      | No       | No     | Yes*    | No*  | TLSe claims to support it, but seems to be server-only
//                                                                     | BearSSL is homemade and will need rewrites if upstream changes
//Server                | No      | No       | No     | No      | No   | Likely possible on everything, I'm just lazy
//Reputable author      | Yes     | Yes      | Yes    | Yes     | No   | Not saying TLSe is bad, just that I don't know
//Binary size           | 4       | 2.5      | 4      | 80      | 169  | In kilobytes, estimated; DLLs not included
#endif
