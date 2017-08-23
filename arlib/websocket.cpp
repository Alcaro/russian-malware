#ifdef ARLIB_SOCKET
#include "websocket.h"
#include "http.h"
#include "endian.h"
#include "stringconv.h"

bool WebSocket::connect(cstring target, arrayview<string> headers)
{
	HTTP::location loc;
	if (!HTTP::parseUrl(target, false, loc)) return false;
	if (loc.proto == "wss") sock = socketssl::create(loc.domain, loc.port ? loc.port : 443);
	if (loc.proto == "ws")  sock =    socket::create(loc.domain, loc.port ? loc.port : 80);
	if (!sock) return false;
	
	sock->send("GET "+loc.path+" HTTP/1.1\r\n"
	           "Host: "+loc.domain+"\r\n"
	           "Connection: upgrade\r\n"
	           "Upgrade: websocket\r\n"
	           //"Origin: "+loc.domain+"\r\n"
	           "Sec-WebSocket-Version: 13\r\n"
	           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n" // TODO: de-hardcode this
	           );
	
	for (cstring s : headers)
	{
		sock->send(s+"\r\n");
	}
	sock->send("\r\n");
	
	msg.resize(4096);
	int bytesdone = 0;
	while (true) // TODO: make nonblocking
	{
		int bytes = sock->recv(msg.skip(bytesdone), true);
		if (bytes<0) return false;
		
	again: ;
		size_t n = msg.slice(bytesdone, bytes).find('\n');
		if (n != (size_t)-1)
		{
			n += bytesdone;
			bytesdone += bytes;
			
			string line = msg.slice(0, n);
			if (line.endswith("\r")) line = line.substr(0, ~1);
			msg = msg.slice(n+1, msg.size()-n-1);
			msg.resize(4096);
			bytesdone -= n+1;
			if (line.startswith("HTTP") && !line.startswith("HTTP/1.1 101 ")) return false;
			if (line == "")
			{
				msg.resize(bytesdone);
				return true;
			}
			bytes = bytesdone;
			bytesdone = 0;
			goto again;
		}
		else bytesdone += bytes;
	}
	return true;
}

void WebSocket::fetch(bool block)
{
again: ;
	uint8_t bytes[4096];
	int nbyte = sock->recv(bytes, block);
	if (nbyte < 0)
	{
puts("SOCKDEAD");
		sock = NULL;
	}
	if (nbyte > 0)
	{
		msg += arrayview<byte>(bytes, nbyte);
		block = false;
		goto again;
	}
}

bool WebSocket::poll(bool block, array<byte>* ret)
{
again:
	if (!sock) return false;
	
	if (msg.size() < 2) { fetch(block); block=false; }
	if (msg.size() < 2) return false;
	
	uint8_t headsizespec = msg[1]&0x7F;
	uint8_t headsize = 2;
	if (msg[1] & 0x80) headsize += 4;
	if (headsizespec == 126) headsize += 2;
	if (headsizespec == 127) headsize += 8;
	
	if (msg.size() < headsize) { fetch(block); block=false; }
	if (msg.size() < headsize) return false;
	
	size_t msgsize = headsize + headsizespec;
	if (headsizespec == 126) msgsize = headsize + bigend<uint16_t>(msg.slice(2, 2));
	if (headsizespec == 127) msgsize = headsize + bigend<uint64_t>(msg.slice(2, 8));
	
	if (msg.size() < msgsize) { fetch(block); block=false; }
	if (msg.size() < msgsize) return false;
	
	size_t bodysize = msgsize-headsize;
	
	if (msg[0]&0x08) // throw away control messages
	{
puts("RETURNSKIP:"+tostringhex(msg.slice(0,headsize))+" "+tostringhex(msg.skip(headsize)));
		msg = msg.skip(msgsize);
		goto again;
	}
	
	if (ret)
	{
		array<byte>& out = *ret;
		out = msg.slice(headsize, bodysize);
//puts("RETURN:"+tostringhex(msg.slice(0,headsize))+" "+tostringhex(out));
		
		if (msg[1] & 0x80)
		{
			uint8_t key[4];
			key[0] = msg[headsize-4+0];
			key[1] = msg[headsize-4+1];
			key[2] = msg[headsize-4+2];
			key[3] = msg[headsize-4+3];
			for (size_t i=0;i<bodysize;i++)
			{
				out[i] ^= key[i&3];
			}
		}
		
		msg = msg.skip(msgsize);
	}
	return true;
}

array<byte> WebSocket::recv(bool block)
{
	array<byte> b;
	bool received;
	do {
		received = poll(block, &b);
	} while (sock && block && !received);
	return b;
}

void WebSocket::send(arrayview<byte> message, bool binary)
{
	if (!sock) return;
	
	array<byte> header;
	header.append(0x80 | (binary ? 0x2 : 0x1)); // frame-FIN, opcode
	if (message.size() <= 125)
	{
		//some websocket servers require masking, despite that being completely useless over https, and despite not doing masking themselves
		//hypocrites
		//TODO: actually mask, rather than doing nop encryption
		header.append(message.size() | 0x80);
	}
	else if (message.size() <= 65535)
	{
		header.append(126 | 0x80);
		header += bigend<uint16_t>(message.size()).bytes();
	}
	else
	{
		header.append(127 | 0x80);
		header += bigend<uint64_t>(message.size()).bytes();
	}
	header.append(0);
	header.append(0);
	header.append(0);
	header.append(0);
//puts("SEND:"+tostringhex(header)+" "+tostringhex(message));
	sock->send(header);
	sock->send(message);
}

#include "test.h"
#ifdef ARLIB_TEST
test()
{
	test_skip("kinda slow");
	
	{
		WebSocket ws;
		assert(ws.connect("wss://echo.websocket.org"));
		ws.send("hello");
		assert_eq(ws.recvstr(true), "hello");
		ws.send("hello");
		assert_eq(ws.recvstr(true), "hello");
		ws.send("hello");
		ws.send("hello");
		assert_eq(ws.recvstr(true), "hello");
		assert_eq(ws.recvstr(true), "hello");
		
#define msg128 "128bytes128bytes128bytes128bytes128bytes128bytes128bytes128bytes" \
               "128bytes128bytes128bytes128bytes128bytes128bytes128bytes128bytes"
		ws.send(msg128);
		assert_eq(ws.recvstr(true), msg128);
	}
	
	{
		//this server requires masking for whatever strange reason
		WebSocket ws;
		assert(ws.connect("wss://wss.websocketstest.com/service"));
		assert_eq(ws.recvstr(true), "connected,");
		ws.send("version,");
		assert_eq(ws.recvstr(true), "version,hybi-draft-13");
		ws.send("echo,test message");
		assert_eq(ws.recvstr(true), "echo,test message");
	}
}
#endif
#endif
