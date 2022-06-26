#pragma once
#include "runloop2.h"
#include "string.h"
#include "time.h"
#include "bytepipe.h"
#include "endian.h"
#include <unistd.h>

#ifdef _WIN32
#include <winsock2.h>
#else
typedef int SOCKET; // it's a windows name, but making up my own name would have no advantages other than not being a windows name
#endif

class socket2 {
public:
	// This struct represents a sockaddr_in6.
	struct address {
		alignas(uint32_t) char _space[28];
		
		address() { memset(_space, 0, sizeof(_space)); }
		address(bytesr by, uint16_t port = 0); // Input must be 0, 4 or 16 bytes.
		address(cstring str, uint16_t port = 0); // If port is not zero, it may be parsed from the input.
		address(const address&) = default;
		
		operator bool();
		
		// These two will return the address without port.
		bytesr as_bytes(); // 4 or 16 bytes, or 0 if empty.
		string as_str();
		
		struct sockaddr * as_native() { return (struct sockaddr*)_space; }
		
		uint16_t port();
		void set_port(uint16_t newport);
		address& with_port(uint16_t newport) { set_port(newport); return *this; }
	};
	
	virtual ~socket2() {}
	
	// Like the recv syscall, can return fewer bytes than requested, including zero. If so, call the function again.
	// Two different parts of the program may await recv() and send() simultaneously, but only one of each.
	// Asking for zero bytes is undefined behavior. However, it is a quite possible return value.
	// The buffers must remain valid until the async operation completes.
	// Every static member can be used concurrently by any number of callers, on any thread and on any coroutine.
	virtual async<ssize_t> recv(bytesw by) = 0;
	virtual async<ssize_t> send(bytesr by) = 0;
	
	// These three are low level devices. You probably want the below two instead.
	// Arlib DNS does not recognize the concept of search domains; it will treat every domain as fully qualified.
	// Arlib DNS does also not recognize the FQDN marker (trailing period); it's a concept no longer attached to reality.
	// If the port is nonzero, the port can be overridden by the domain. If port is zero, domain must be domain only.
	static async<address> dns(cstring domain, uint16_t port = 0);
	static async<autoptr<socket2>> create(address ip);
#ifdef ARLIB_SSL
	// Whether setup fails or succeeds, the inner socket is consumed and can't be extracted.
	static async<autoptr<socket2>> wrap_ssl(autoptr<socket2> inner, cstrnul domain);
#endif
	
	// These two will accept a port from the domain name.
	static inline async<autoptr<socket2>> create(cstring domain, uint16_t port)
	{
		address ip = co_await socket2::dns(domain, port);
		co_return co_await socket2::create(ip);
	}
#ifdef ARLIB_SSL
	static inline async<autoptr<socket2>> create_ssl(cstrnul domain, uint16_t port)
	{
		address ip = co_await socket2::dns(domain, port);
		autoptr<socket2> sock = co_await socket2::create(ip);
		if (!sock) co_return nullptr;
		co_return co_await socket2::wrap_ssl(std::move(sock), domain);
	}
#endif
	static inline async<autoptr<socket2>> create_sslmaybe(bool ssl, cstrnul domain, uint16_t port)
	{
#ifndef ARLIB_SSL
		if (ssl)
			co_return nullptr;
#endif
		address ip = co_await socket2::dns(domain, port);
		autoptr<socket2> sock = co_await socket2::create(ip);
		if (!sock) co_return nullptr;
#ifdef ARLIB_SSL
		if (ssl)
			sock = co_await socket2::wrap_ssl(std::move(sock), domain);
#endif
		co_return sock;
	}
	
	// Implementation detail of dns(), to be called by the runloop only.
	static void* dns_create();
	static void dns_destroy(void* dns);
};

// Differences from the normal TCP sockets:
// - UDP is a different protocol, of course.
// - Can't be wrapped into SSL or socks5 or whatever.
// - UDP is connectionless, so creating a socket2_udp is asynchronous.
// - recv and send will either return error or a full packet. They will not split packets, nor return zero bytes.
// - send is synchronous. If the socket is full, the packet is lost.
#ifdef __unix__
class socket2_udp {
	SOCKET fd;
	socket2::address addr;
	
	bytesw recv_by;
	socket2::address* recv_sender;
	struct recv_pt : public producer_fn<ssize_t, recv_pt> {
		void cancel() { container_of<&socket2_udp::recv_p>(this)->cancel_recv(); }
	} recv_p;
	struct recv_wt : public waiter_fn<void, recv_wt> {
		void complete() { container_of<&socket2_udp::recv_w>(this)->complete_recv(); }
	} recv_w;
	void complete_recv();
	void cancel_recv() { recv_w.cancel(); }
	
	socket2_udp(SOCKET fd, socket2::address addr) : fd(fd), addr(addr) {}
public:
	static autoptr<socket2_udp> create(socket2::address ip);
	
	async<ssize_t> recv(bytesw by, socket2::address* sender = nullptr)
	{
		recv_by = by;
		recv_sender = sender;
		runloop2::await_read(fd).then(&recv_w);
		return &recv_p;
	}
	void send(bytesr by);
	
	~socket2_udp() { close(fd); }
};
#endif
#ifdef _WIN32
class socket2_udp {
	SOCKET fd;
	socket2::address addr;
	
	INT sock_len;
	DWORD flags;
	
	WSAOVERLAPPED ov_rd = {};
	struct recv_pt : public producer_fn<ssize_t, recv_pt> {
		void cancel() { container_of<&socket2_udp::recv_p>(this)->cancel_recv(); }
	} recv_p;
	void cancel_recv();
	
	socket2_udp(SOCKET fd, socket2::address addr) : fd(fd), addr(addr) {}
public:
	static autoptr<socket2_udp> create(socket2::address ip);
	
	async<ssize_t> recv(bytesw by, socket2::address* sender = nullptr);
	void send(bytesr by);
	
	~socket2_udp() { close(fd); }
};
#endif

// A socketbuf is a convenience wrapper to read structured data from the socket. Ask for N bytes and you get N bytes, no need for loops.
// On the send side, it converts send to memcpy, making it act as if it's synchronous and allowing multiple concurrent writers.
class socketbuf {
	autoptr<socket2> sock;
	bytepipe by;
	
	enum op_t {
		op_none, op_u8,
		op_u16b, op_u16l,
		op_u32b, op_u32l,
		op_u64b, op_u64l,
		op_bytesr, op_bytesr_partial,
		op_line,
	} op;
	size_t need_bytes; // or max length for op_line
	
	template<typename T>
	struct producer_t : public producer_fn<T, producer_t<T>> {
		// extra declval to work around https://github.com/llvm/llvm-project/issues/55812
		void cancel() { container_of<&socketbuf::prod>((decltype(std::declval<socketbuf>().prod)*)this)->cancel(); }
	};
	variant_raw<producer_t<uint8_t>, producer_t<uint16_t>, producer_t<uint32_t>, producer_t<uint64_t>,
	            producer_t<bytesr>, producer_t<cstring>> prod;
	void cancel()
	{
		op = op_none;
		wait.cancel();
	}
	
	struct waiter_t : public waiter_fn<ssize_t, waiter_t> {
		void complete(ssize_t val) { container_of<&socketbuf::wait>(this)->complete(val); }
	} wait;
	void complete(ssize_t n);
	void complete2(size_t prev_size);
	void complete_null();
	
	template<typename T>
	async<T> get_async()
	{
		producer_t<T>* ret = prod.construct<producer_t<T>>();
		complete2(0);
		return ret;
	}
	template<typename T, op_t op>
	async<T> ret_int()
	{
		this->op = op;
		need_bytes = sizeof(T);
		return get_async<T>();
	}
public:
	socketbuf() { by.bufsize(4096); }
	socketbuf(autoptr<socket2> sock) : sock(std::move(sock)) { by.bufsize(4096); }
	socketbuf& operator=(autoptr<socket2> sock)
	{
		this->sock = std::move(sock);
		by.reset(4096);
		return *this;
	}
	
	void reset() { sock = nullptr; }
	
	operator bool() { return sock != nullptr; }
	
	// If the socket breaks, these will return the applicable zero value until the object is deleted.
	async<uint8_t> u8() { return ret_int<uint8_t, op_u8>(); }
	async<uint8_t> u8b() { return u8(); }
	async<uint8_t> u8l() { return u8(); }
	async<uint16_t> u16b() { return ret_int<uint16_t, op_u16b>(); }
	async<uint16_t> u16l() { return ret_int<uint16_t, op_u16l>(); }
	async<uint32_t> u32b() { return ret_int<uint32_t, op_u32b>(); }
	async<uint32_t> u32l() { return ret_int<uint32_t, op_u32l>(); }
	async<uint64_t> u64b() { return ret_int<uint64_t, op_u64b>(); }
	async<uint64_t> u64l() { return ret_int<uint64_t, op_u64l>(); }
	
	// The max length is slightly approximate. The object will not read another packet once at least 8192 bytes have been collected,
	//  but if the line is 8200 bytes and the last packet completed it, it will be returned.
	// Length exceeded can be distinguished from socket failure via operator bool, but there's not much you can do with that information.
	// Empty line can, like bytepipe, be distinguished from failure by whether it contains \n.
	async<cstring> line(size_t maxlen = 8192)
	{
		op = op_line;
		need_bytes = maxlen;
		return get_async<cstring>();
	}
	async<bytesr> bytes(size_t n)
	{
		op = op_bytesr;
		need_bytes = n;
		return get_async<bytesr>();
	}
	// Will return anywhere between 1 and n bytes.
	async<bytesr> bytes_partial(size_t n)
	{
		op = op_bytesr_partial;
		need_bytes = n;
		return get_async<bytesr>();
	}
	
private:
	bytepipe send_by;
	ssize_t send_sync = 0;
	
	struct send_waiter_t : public waiter_fn<ssize_t, send_waiter_t> {
		void complete(ssize_t val) { container_of<&socketbuf::send_wait>(this)->send_complete(val); }
	} send_wait;
	struct send_producer_t : public producer_fn<bool, send_producer_t> {
		void cancel() { container_of<&socketbuf::send_prod>(this)->send_wait.cancel(); }
	} send_prod;
	void send_complete(ssize_t n)
	{
		if (n < 0)
			sock = nullptr;
		if (send_sync != 0)
		{
			send_sync = n;
			return;
		}
		if (!sock)
		{
			if (send_prod.has_waiter())
				send_prod.complete(false);
			return;
		}
		send_by.pull_finish(n);
		if (send_by.size())
		{
			sock->send(send_by.pull_begin()).then(&send_wait);
		}
		else
		{
			if (send_prod.has_waiter())
				send_prod.complete(true);
		}
	}
public:
	/*
	void send(bytesr by)
	{
		if (!sock)
			return;
		if (send_by.size() == 0)
		{
			send_sync = 1;
			sock->send(by).then(&send_wait);
			if (!send_wait.is_waiting())
			{
				if (!sock)
					return;
				by = by.skip(send_sync); // can't be negative, negatives make it reset sock
				if (!by)
					return;
			}
		}
		send_sync = 0;
		send_by.push(by);
		send_complete(0);
	}
	void send(cstring str) { return send(str.bytes()); }
	void send(const char * str) { return send(bytesr((uint8_t*)str, strlen(str))); }
	*/
	
	// send_buf collects incoming bytes, and does not send them 
	void send_buf(bytesr by) { send_by.push(by); }
	template<typename... Ts> void send_buf(Ts... args) { send_by.push_text(args...); }
	void send_flush() { send_complete(0); }
	
	// Completes when the object has nothing left to send.
	// This may be immediately, or may take longer than expected if another coroutine sends something while you're waiting.
	// Only one coroutine may await sends; concurrency here is not allowed.
	async<bool> await_send()
	{
		if (!sock)
			return &send_prod.complete(false);
		else if (send_by.size() == 0)
			return &send_prod.complete(true);
		else
			return &send_prod;
	}
};
