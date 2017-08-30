#include "socket.h"
#include "../test.h"

//TODO:
//- fetch howsmyssl, ensure the only failure is the session cache

#ifdef ARLIB_TEST
//#undef test_skip
//#define test_skip(x)

//not in socket.h because this shouldn't really be used for anything, blocking is evil
static array<byte> recvall(socket* sock, unsigned int len)
{
	array<byte> ret;
	ret.resize(len);
	
	size_t pos = 0;
	while (pos < len)
	{
		int part = sock->recv(ret.slice(pos, (pos==0)?2:1), true); // funny slicing to ensure partial reads are processed sensibly
		assert_ret(part >= 0, NULL);
		assert_ret(part > 0, NULL); // this is a blocking recv, returning zero is forbidden
		pos += part;
	}
	return ret;
}

static void clienttest(socket* rs)
{
	//returns whether the socket peer speaks HTTP
	//discards the actual response, and since the Host: header is silly, it's most likely some variant of 404 not found
	//also closes the socket
	
	autoptr<socket> s = rs;
	assert(s);
	
	//in HTTP, client talks first, ensure this doesn't return anything
	byte discard[1];
	assert(s->recv(discard) == 0);
	
	const char http_get[] =
		"GET / HTTP/1.1\r\n"
		"Host: example.com\r\n" // wrong host, but we don't care, all we care about is server returning a HTTP response
		"Connection: close\r\n" // 400 Bad Request is the easiest to summon, so let's do that
		"\r\n";
	assert_eq(s->send(http_get), (int)strlen(http_get));
	
	array<byte> ret = recvall(s, 4);
	assert(ret.size() == 4);
	assert(!memcmp(ret.ptr(), "HTTP", 4));
}

test("plaintext client") { test_skip("too slow"); clienttest(socket::create("google.com", 80)); }
test("SSL client") { test_skip("too slow"); clienttest(socketssl::create("google.com", 443)); }
test("SSL SNI") { test_skip("too slow"); clienttest(socketssl::create("git.io", 443)); }
test("SSL permissiveness (bad root)")
{
	test_skip("too slow");
	autoptr<socket> s;
	assert(!(s=socketssl::create("superfish.badssl.com", 443)));
	assert( (s=socketssl::create("superfish.badssl.com", 443, true)));
}
test("SSL permissiveness (bad name)")
{
	test_skip("too slow");
	autoptr<socket> s;
	assert(!(s=socketssl::create("wrong.host.badssl.com", 443)));
	assert( (s=socketssl::create("wrong.host.badssl.com", 443, true)));
}
test("SSL permissiveness (expired)")
{
	test_skip("too slow");
	autoptr<socket> s;
	assert(!(s=socketssl::create("expired.badssl.com", 443)));
	assert( (s=socketssl::create("expired.badssl.com", 443, true)));
}

#ifdef ARLIB_SSL_BEARSSL
static void ser_test(autoptr<socketssl>& s)
{
//int fd;
//s->serialize(&fd);
	
	socketssl* sp = s.release();
	assert(sp);
	assert(!s);
	int fd;
	array<byte> data = sp->serialize(&fd);
	assert(data);
	s = socketssl::deserialize(fd, data);
	assert(s);
}
test("SSL serialization")
{
	test_skip("too slow");
	autoptr<socketssl> s = socketssl::create("google.com", 443);
	testcall(ser_test(s));
	s->send("GET / HTTP/1.1\n");
	testcall(ser_test(s));
	s->send("Host: google.com\nConnection: close\n\n");
	testcall(ser_test(s));
	array<byte> bytes = recvall(s, 4);
	assert_eq(string(bytes), "HTTP");
	testcall(ser_test(s));
	bytes = recvall(s, 4);
	assert_eq(string(bytes), "/1.1");
}
#endif

void listentest(const char * localhost, int port)
{
#ifdef __linux__
	test_skip("spurious failures due to TIME_WAIT (add SO_REUSEADDR?)");
#endif
	
	autoptr<socketlisten> l = socketlisten::create(port);
	assert(l);
	autoptr<socket> c1 = socket::create(localhost, port);
	assert(c1);
	
#ifdef _WIN32
	//apparently the connection takes a while to make it through the kernel, at least on windows
	//socket* lr = l; // can't select &l because autoptr<socketlisten>* isn't socket**
	//assert(socket::select(&lr, 1, 100) == 0); // TODO: enable select()
	Sleep(50);
#endif
	autoptr<socket> c2 = l->accept();
	assert(c2);
	
	l = NULL;
	
	c1->send("foo");
	c2->send("bar");
	
	array<byte> ret;
	ret = recvall(c1, 3);
	assert(ret.size() == 3);
	assert(!memcmp(ret.ptr(), "bar", 3));
	
	ret = recvall(c2, 3);
	assert(ret.size() == 3);
	assert(!memcmp(ret.ptr(), "foo", 3));
}

test("listen on localhost") { listentest("localhost", 7777); }
test("listen on 127.0.0.1") { listentest("127.0.0.1", 7778); }
test("listen on ::1")       { listentest("::1", 7779); }
#endif
