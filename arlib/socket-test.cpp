#if defined(ARLIB_SOCKET) && defined(ARLIB_TEST)
#include "socket.h"
#include "test.h"

#include "http.h"
#include "json.h"

//TODO: fetch howsmyssl, ensure the only failure is the session cache
//TODO: some of these tests could be made cleaner with runloop::step()

static async<void> socket_test_http(socket2* sock)
{
	const char * http_get =
		"GET / HTTP/1.1\r\n"
		"Host: example.com\r\n" // this is wrong host, so we'll get a 400 or 404 or whatever
		"Connection: close\r\n" // but the response is valid HTTP and that's all we care about
		"\r\n";
	while (*http_get)
	{
		co_await sock->can_send();
		ssize_t n = sock->send_sync(bytesr((uint8_t*)http_get, strlen(http_get)));
		assert_gte(n, 0);
		http_get += n;
	}
	
	uint8_t buf[8];
	size_t n_buf = 0;
	while (n_buf < ARRAY_SIZE(buf))
	{
		co_await sock->can_recv();
		ssize_t n = sock->recv_sync(bytesw(buf).skip(n_buf));
		assert_gte(n, 0);
		n_buf += n;
	}
	
	assert_eq(string(arrayview<uint8_t>(buf)), "HTTP/1.1");
}

co_test("TCP client",  "dns", "tcp")
{
	test_skip("too slow");
	
	// this IP is www.nic.ad.jp, both lookup and ping time for that one are 300ms (helps shake out failure to return to runloop)
	co_await socket_test_http(co_await socket2::create("192.41.192.145", 80));
	co_await socket_test_http(co_await socket2::create("www.nic.ad.jp",  80));
}

static async<void> ssltest(function<async<autoptr<socket2>>(autoptr<socket2> inner, cstrnul domain)> wrap)
{
	test_skip("too slow");
	
	auto dotest = [wrap](cstring desc, cstrnul domain, int port, bool should_succeed = true) -> async<void> {
		testctx(desc) {
			autoptr<socket2> sock_raw = co_await socket2::create(domain, port);
			assert(sock_raw);
			autoptr<socket2> sock_ssl = co_await wrap(std::move(sock_raw), domain);
			if (should_succeed)
			{
				assert(sock_ssl);
				co_await socket_test_http(sock_ssl);
			}
			else
			{
				assert(!sock_ssl);
			}
		}
	};
	
	co_await dotest("basic", "example.com", 443);
	co_await dotest("SNI", "git.io", 443);
	co_await dotest("bad root", "superfish.badssl.com", 443, false);
	co_await dotest("bad name", "wrong.host.badssl.com", 443, false);
	co_await dotest("expired", "expired.badssl.com", 443, false);
	
	//if (wrap_unsafe)
	//{
	//	co_await dotest("bad root, permissive", "superfish.badssl.com", 443, true, true);
	//	co_await dotest("bad name, permissive", "wrong.host.badssl.com", 443, true, true);
	//	co_await dotest("expired, permissive", "expired.badssl.com", 443, true, true);
	//}
	
	testctx("howsmyssl") {
		http_t http;
		http.wrap_socks([wrap](bool ssl, cstrnul domain, uint16_t port) -> async<autoptr<socket2>> {
			assert(ssl);
			co_return co_await wrap(co_await socket2::create(domain, port), domain);
		});
		http_t::req q = { "https://www.howsmyssl.com/a/check" };
		http_t::rsp r = co_await http.request(q);
		JSON json(r.text());
		assert_eq(json["rating"].str(), "Probably Okay");
		//puts(json.serialize());
	}
}

#ifdef ARLIB_SSL_OPENSSL
co_test("SSL client, OpenSSL",  "tcp", "ssl") { co_await ssltest(socket2::wrap_ssl_openssl); }
#endif
#ifdef ARLIB_SSL_SCHANNEL
co_test("SSL client, SChannel", "tcp", "ssl") { co_await ssltest(socket2::wrap_ssl_schannel); }
#endif
#ifdef ARLIB_SSL_BEARSSL
co_test("SSL client, BearSSL",  "tcp", "ssl") { co_await ssltest(socket2::wrap_ssl_bearssl); }
#endif

/*
co_test("TCP client, disconnecting server", "runloop,dns", "tcp")
{
	test_skip("too slow");
	test_skip_force("30 seconds is too slow");
	
	autoptr<runloop> loop = runloop::create();
	
	uintptr_t timer = loop->raw_set_timer_once(60000, bind_lambda([&]() { loop->exit(); assert_unreachable(); }));
	
	uint64_t start_ms = time_ms_ne();
	
	//need to provoke Shitty Server into actually dropping the connections
	autoptr<socket> sock2;
	uintptr_t timer2 = loop->raw_set_timer_repeat(5000, bind_lambda([&]() { sock2 = socket::create("floating.muncher.se", 9, loop); }));
	
	autoptr<socket> sock = socket::create("floating.muncher.se", 9, loop);
	assert(sock);
	
	sock->callback(bind_lambda([&]()
		{
			uint8_t buf[64];
			int ret = sock->recv(buf);
			assert_lte(ret, 0);
			if (ret < 0) loop->exit();
		}), nullptr);
	
	loop->enter();
	
	//test passes if this thing notices that Shitty Server dropped it within 60 seconds
	//Shitty will drop me after 10-20 seconds, depending on when exactly the other sockets connect
	//Arlib will send a keepalive after 30 seconds, which will immediately return RST and return ECONNRESET
	
	//make sure it didn't fail because Shitty Server is down
	assert_gt(time_ms_ne()-start_ms, 29000);
	loop->raw_timer_remove(timer);
	loop->raw_timer_remove(timer2);
}
*/

//test("SSL renegotiation", "tcp", "ssl")
//{
//	test_skip("too slow");
//	assert(!"find or create a renegotiating server, then use it");
//	// perhaps BoarSSL could help, or maybe I should add server support to some SSL engines and do some lamehack to renegotiate
//}

#ifdef ARLIB_SSL_BEARSSL
/*
static void ser_test(autoptr<socketssl>& s)
{
//int fd;
//s->serialize(&fd);
	
	socketssl* sp = s.release();
	assert(sp);
	assert(!s);
	int fd;
	array<uint8_t> data = sp->serialize(&fd);
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
	array<uint8_t> bytes = recvall(s, 4);
	assert_eq(string(bytes), "HTTP");
	testcall(ser_test(s));
	bytes = recvall(s, 4);
	assert_eq(string(bytes), "/1.1");
}
*/
#endif

/*
co_test("TCP listen", "tcp", "")
{
	runloop* loop = runloop::global();
	
	uintptr_t timer = loop->raw_set_timer_once(1000, bind_lambda([&]() { loop->exit(); assert_unreachable(); }));
	
	int port = time(NULL)%3600 + 10000; // running tests twice quickly fails due to TIME_WAIT unless port varies
	
	autoptr<socket> a;
	autoptr<socket> b;
	autoptr<socketlisten> lst = socketlisten::create(port, loop, [&](autoptr<socket> s) { b = std::move(s); loop->exit(); });
	
	assert(lst);
	a = socket::create("localhost", port, loop);
	assert(a);
	loop->enter();
	assert(b);
	
	a->send(cstring("hello").bytes());
	b->send(cstring("world").bytes());
	
	// not supposed to call recv outside a runloop, but it works in practice and compactness is better than correctness in tests.
	uint8_t tmp[6] = {0};
	b->recv(tmp);
	assert_eq(cstring((char*)tmp), "hello");
	a->recv(tmp);
	assert_eq(cstring((char*)tmp), "world");
	
	loop->raw_timer_remove(timer);
}
*/


struct fake_socket : public socket2 {
	ssize_t ret = 0;
	producer<void> recv_wait;
	producer<void> send_wait;
	
	ssize_t recv_sync(bytesw by) override { return ret; }
	ssize_t send_sync(bytesr by) override { return ret; }
	async<void> can_recv() override { return &recv_wait; }
	async<void> can_send() override { return &send_wait; }
};
test("socketbuf crash", "", "")
{
	// test fails if the socketbuf failing a send_sync() deletes the socket before disconnecting can_recv
	static int step;
	{
		step = 0;
		fake_socket* inner = new fake_socket();
		socketbuf sock = autoptr<socket2>(inner);
		sock.send(cstring("abc").bytes());
		assert_eq(++step, 1);
		runloop2::detach([](socketbuf& sock)->async<void>{
				assert_eq(++step, 2);
				co_await sock.u32l();
				assert_eq(++step, 4);
				assert(!sock);
			}(sock));
		assert_eq(++step, 3);
		inner->ret = -1;
		inner->send_wait.complete();
		assert_eq(++step, 5);
	}
	
	// same but it's the recv that fails
	{
		step = 0;
		fake_socket* inner = new fake_socket();
		socketbuf sock = autoptr<socket2>(inner);
		sock.send(cstring("abc").bytes());
		assert_eq(++step, 1);
		runloop2::detach([](socketbuf& sock)->async<void>{
				assert_eq(++step, 2);
				co_await sock.u32l();
				assert_eq(++step, 4);
				assert(!sock);
			}(sock));
		assert_eq(++step, 3);
		inner->ret = -1;
		inner->recv_wait.complete();
		assert_eq(++step, 5);
	}
}
#endif
