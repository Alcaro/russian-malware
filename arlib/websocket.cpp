#ifdef ARLIB_SOCKET
#include "websocket.h"
#include "http.h"
#include "bytestream.h"

async<bool> websocket::connect(cstring target, arrayview<string> headers)
{
	sock = nullptr;
	
	http_t::location loc;
	if (!loc.parse(target))
	{
	fail:
		sock = nullptr;
		co_return false;
	}
	
	if (loc.scheme == "ws")
		sock = co_await cb_mksock(false, loc.domain, 80);
#ifdef ARLIB_SSL
	else if (loc.scheme == "wss")
		sock = co_await cb_mksock(true, loc.domain, 443);
#endif
	else
		goto fail;
	
	sock.send_buf("GET ",loc.path," HTTP/1.1\r\n"
				  "Host: ",loc.domain,"\r\n"
				  "Connection: upgrade\r\n"
				  "Upgrade: websocket\r\n"
				  //"Origin: ",loc.domain,"\r\n"
				  "Sec-WebSocket-Version: 13\r\n"
				  "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"); // TODO: de-hardcode this
	for (cstring s : headers)
	{
		sock.send_buf(s, "\r\n");
	}
	sock.send_buf("\r\n");
	sock.send_flush();
	
	if (!co_await sock.await_send())
		goto fail;
	
	cstring line = co_await sock.line();
	if (!line.startswith("HTTP/1.1 101 "))
		goto fail;
	while (true)
	{
		cstring line = co_await sock.line();
		if (!line)
			goto fail;
		if (!bytepipe::trim_line(line))
			break;
	}
	
	co_return true;
}

// The returned bytesr is valid until next function call on this object.
async<bytesr> websocket::msg(int* type)
{
	if (type)
		*type = 0;
	
	uint8_t type_raw = co_await sock.u8();
	size_t size = co_await sock.u8();
	if (size & 0x80) // the mask bit; https://datatracker.ietf.org/doc/html/rfc6455#section-5.1 says only client can set it
	{
	fail:
		sock = nullptr;
		co_return nullptr;
	}
	
	if (size == 126)
	{
		size = co_await sock.u16b();
		if (size < 126) goto fail; // https://datatracker.ietf.org/doc/html/rfc6455#section-5.2 says overlong encodings are banned
	}
	else if (size == 127)
	{
		uint64_t size64 = co_await sock.u64b();
		if (size64 <= 0xFFFF) goto fail; // overlong
		if (size64 > SIZE_MAX) goto fail; // can't represent that
		if (size64 > 0x7FFFFFFFFFFFFFFF) goto fail; // spec says size can't be this big, unclear why
		size = size64;
	}
	// else nothing
	
	bytesr by = co_await sock.bytes(size);
	if (!sock) co_return nullptr;
	
	// spec says server may send ping and pong, as well as fragmented messages
	// I've never seen em
	
	if ((type_raw&0x0F) == t_close)
		sock = nullptr;
	if (type)
		*type = (type_raw&0x0F);
	co_return by;
}

void websocket::send(bytesr by, int type)
{
	uint8_t head_buf[2+8+4];
	bytestreamw head = head_buf;
	
	head.u8(0x80 | type);
	if (by.size() < 126)
	{
		head.u8(0x80 | by.size());
	}
	else if (by.size() <= 0xFFFF)
	{
		head.u8(0x80 | 126);
		head.u16b(by.size());
	}
	else
	{
		head.u8(0x80 | 127);
		head.u64b(by.size());
	}
	head.u32b(0); // mask key (spec says must be random, but screw that, it doesn't protect against any plausible threat)
	
	sock.send_buf(head.finish());
	sock.send_buf(by);
	sock.send_flush();
}

#include "test.h"
//co_test("websocket", "tcp,ssl,bytepipe", "websocket")
co_test("websocket", "", "websocket")
{
	test_skip("takes two seconds");
	
	websocket ws;
	assert(co_await ws.connect("wss://ws.ifelse.io"));
	
	cstring hq = co_await ws.msg();
	assert(cstring(hq).startswith("Request served by"));
	ws.send("hello");
	assert_eq(cstring(co_await ws.msg()), "hello");
	ws.send("hello");
	assert_eq(cstring(co_await ws.msg()), "hello");
	ws.send("hello");
	ws.send("hello");
	assert_eq(cstring(co_await ws.msg()), "hello");
	assert_eq(cstring(co_await ws.msg()), "hello");
	
	cstring msg128 = "128bytes128bytes128bytes128bytes128bytes128bytes128bytes128bytes"
	                 "128bytes128bytes128bytes128bytes128bytes128bytes128bytes128bytes";
	ws.send(msg128);
	assert_eq(cstring(co_await ws.msg()), msg128);
}
#endif
