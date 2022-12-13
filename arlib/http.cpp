#ifdef ARLIB_SOCKET
#include "http.h"

// TODO: check what happens if a request is canceled after being pipelined, but before the previous one completes

bool http_t::location::parse(cstring url, bool relative)
{
	if (!url) return false;
	
	size_t urllen = url.length();
	size_t pos = 0;
	while (pos < urllen && islower(url[pos])) pos++;
	if (pos < urllen && url[pos]==':')
	{
		this->scheme = url.substr(0, pos);
		url = url.substr(pos+1, ~0);
	}
	else if (!relative) return false;
	
	string hash = this->path.contains("#") ? "#"+this->path.csplit<1>("#")[1] : "";
	
	if (url.startswith("//"))
	{
		url = url.substr(2, ~0);
		
		array<cstring> host_loc = url.csplit<1>("/");
		if (host_loc.size() == 1)
		{
			host_loc = url.csplit<1>("?");
			if (host_loc.size() == 2) this->path = "/?"+host_loc[1];
			else this->path = "/";
		}
		else this->path = "/"+host_loc[1];
		this->host = host_loc[0];
	}
	else if (!relative) return false;
	else if (url[0]=='/') this->path = url;
	else if (url[0]=='?') this->path = this->path.csplit<1>("?")[0] + url;
	else if (url[0]=='#') this->path = this->path.csplit<1>("#")[0] + url;
	else this->path = this->path.crsplit<1>("/")[0] + "/" + url;
	
	if (relative && !url.contains("#")) this->path += hash;
	
	return true;
}

string http_t::urlencode(cstring in)
{
	// not optimized, but it's hard to optimize things with variable width output and complex patterns
	string out;
	for (uint8_t c : in.bytes())
	{
		if (LIKELY(isalnum(c) || c=='*' || c=='-' || c=='.' || c=='_')) out += c;
		else if (c == ' ') out += '+';
		else out += "%"+tostringhex<2>(c);
	}
	return out;
}

string http_t::urldecode(cstring in)
{
	bytesr by = in.bytes();
	string out;
	for (size_t n=0;n<by.size();n++)
	{
		uint8_t ch;
		if (UNLIKELY(by[n] == '%') && n+2 < by.size() && fromstringhex(cstring(by.slice(n+1, 2)), ch))
		{
			out += ch;
			n += 2;
		}
		else if (by[n] == '+')
			out += ' ';
		else
			out += by[n];
	}
	return out;
}

bool http_t::can_keepalive(const req& q, const location& loc)
{
	return
		sock &&
		sock_keepalive_until > timestamp::now() &&
		sock_loc.same_origin(loc) &&
		true;
}

bool http_t::can_pipeline(const req& q, const location& loc)
{
	return (q.method == "GET" || q.method == "") && q.body.size() == 0 && can_keepalive(q, loc);
}

void http_t::send_request(const req& q, const location& loc)
{
	cstring method = q.method;
	if (!method) method = (q.body ? "POST" : "GET");
	
	sock.send_buf(method, " ", loc.path, " HTTP/1.1\r\n");
	
	bool httpHost = false;
	bool httpContentLength = false;
	bool httpContentType = false;
	bool httpConnection = false;
	for (cstring head : q.headers)
	{
		if (head.istartswith("Host:")) httpHost = true;
		if (head.istartswith("Content-Length:")) httpContentLength = true;
		if (head.istartswith("Content-Type:")) httpContentType = true;
		if (head.istartswith("Connection:")) httpConnection = true;
		sock.send_buf(head, "\r\n");
	}
	
	if (!httpHost) sock.send_buf("Host: ", loc.host, "\r\n");
	if (!httpContentLength && method != "GET") sock.send_buf("Content-Length: ", tostring(q.body.size()), "\r\n");
	if (!httpContentType && q.body)
	{
		if (q.body && (q.body[0] == '[' || q.body[0] == '{'))
			sock.send_buf("Content-Type: application/json\r\n");
		else
			sock.send_buf("Content-Type: application/x-www-form-urlencoded\r\n");
	}
	if (!httpConnection) sock.send_buf("Connection: keep-alive\r\n");
	
	sock.send_buf("\r\n");
	
	sock.send_buf(q.body);
	sock.send_flush();
}

http_t::rsp& http_t::set_error(rsp& r, int status)
{
	sock = nullptr;
	r.status = status;
	return r;
}

async<http_t::rsp> http_t::request(req q_orig)
{
	rsp r;
	r.request = std::move(q_orig);
	req& q = r.request;
	
	// procedure:
	// - take mut1
	// - check if socket is usable for pipelining
	// - if no:
	//   - take mut2
	//   - replace socket, unless keep-alive is enough
	// - release mut1
	// - send request
	// - take mut2, unless done above
	// - if socket was recreated, resend request
	// - read response
	// - release mut2 and return
	co_mutex::lock l1 = co_await mut1;
	co_mutex::lock l2;
	
	location loc;
	if (!loc.parse(q.url))
		co_return set_error(r, e_bad_url);
	
	bool can_retry = true;
	
	if (!can_pipeline(q, loc))
	{
		l2 = co_await mut2;
		if (!can_retry)
			co_return set_error(r, e_broken);
		
		if (!can_keepalive(q, loc))
		{
		recreate_sock:
			if (!can_retry)
				co_return set_error(r, e_broken);
			
			if (loc.scheme == "http")
				sock = co_await cb_mksock(false, loc.host, 80);
			else if (loc.scheme == "https")
#ifdef ARLIB_SSL
				sock = co_await cb_mksock(true, loc.host, 443);
#else
				co_return set_error(r, e_connect);
#endif
			else co_return set_error(r, e_bad_url);
			
			sock_loc.set_origin(loc);
			sock_generation++;
			sock_sent = 0;
			sock_received = 0;
			sock_keepalive_until = timestamp::now() + duration::ms(2000);
			
			if (!sock)
				co_return set_error(r, e_connect);
		}
		can_retry = false;
	}
	
	l1.release();
	
	send_request(q, loc);
	uintptr_t last_sock_generation = sock_generation;
	uint16_t sock_recv_expect = sock_sent++;
	ssize_t bytes_left = q.bytes_max;
	
	if (!l2.locked())
	{
		l2 = co_await mut2;
		if (!sock || last_sock_generation != sock_generation || sock_recv_expect != sock_received)
			goto recreate_sock;
	}
	
	string status = co_await sock.line();
	if (!status)
		goto recreate_sock;
	if (status.length() <= 10 || !status.startswith("HTTP/1.") || status[8] != ' ')
		co_return set_error(r, e_not_http);
	bytes_left -= status.length();
	
	cstring status_i = status.csplit<2>(" ")[1];
	if (!fromstring(status_i, r.status) || r.status < 100 || r.status > 599)
		co_return set_error(r, e_not_http);
	
	while (true)
	{
		cstring line = co_await sock.line();
		bytes_left -= line.length();
		if (bytes_left < 0)
			co_return set_error(r, e_too_big);
		if (!line)
			co_return set_error(r, e_broken);
		cstring trimmed_line = bytepipe::trim_line(line);
		if (!trimmed_line)
			break;
		r.headers.append(trimmed_line);
	}
	
	// TODO: chunked return
	cstring transfer_encoding = r.header("Transfer-Encoding");
	if (!transfer_encoding)
	{
		size_t nbytes;
		cstring content_length = r.header("Content-Length");
		if (!content_length && r.status == 204)
			nbytes = 0; // 204 No Content
		else if (!fromstring(content_length, nbytes) || (ssize_t)nbytes < 0)
			co_return set_error(r, e_not_http);
		
		bytes_left -= nbytes;
		if (bytes_left < 0)
			co_return set_error(r, e_too_big);
		
		r.body_raw = co_await sock.bytes(nbytes);
	}
	else if (transfer_encoding == "chunked")
	{
		while (true)
		{
			cstring line = co_await sock.line();
			if (!line)
				co_return set_error(r, e_broken);
			
			size_t chunk_size;
			if (!fromstringhex(bytepipe::trim_line(line), chunk_size))
				co_return set_error(r, e_not_http);
			
			if (chunk_size > 0x70000000) // gotta limit it to something
				co_return set_error(r, e_not_http);
			
			bytes_left -= chunk_size;
			if (bytes_left < 0)
				co_return set_error(r, e_too_big);
			
			r.body_raw += co_await sock.bytes(chunk_size);
			co_await sock.line();
			
			if (chunk_size == 0)
				break;
		}
	}
	else
	{
		co_return set_error(r, e_not_http);
	}
	
	r.complete = true;
	sock_keepalive_until = timestamp::now() + duration::ms(2000);
	sock_received++;
	co_return r;
}

async<http_t::rsp> http_t::get(cstring url)
{
	http_t http;
	req q = { .url=url };
	co_return co_await http.request(q);
}

#include "test.h"
#ifdef ARLIB_TEST
static void test_url(cstring url, cstring url2, cstring expected)
{
	http_t::location loc;
	assert(loc.parse(url));
	assert(loc.path.startswith("/"));
	if (url2) assert(loc.parse(url2, true));
	assert_eq(loc.scheme+"    "+loc.host+"    "+loc.path, expected);
}
static void test_url(cstring url, cstring expected)
{
	test_url(url, "", expected);
}
static void test_url_fail(cstring url, cstring url2)
{
	http_t::location loc;
	assert(loc.parse(url));
	assert(!loc.parse(url2, true));
}
test("URL parser", "string", "http")
{
	testcall(test_url("wss://websocket.example.com/?abc=123&foo=true",          "wss    websocket.example.com    /?abc=123&foo=true"));
	testcall(test_url("wss://websocket.example.com?abc=123&foo=true",           "wss    websocket.example.com    /?abc=123&foo=true"));
	testcall(test_url("wss://websocket.example.com", "?abc=123&foo=true",       "wss    websocket.example.com    /?abc=123&foo=true"));
	testcall(test_url("http://example.com/foo/bar.html?baz", "/bar/foo.html", "http    example.com    /bar/foo.html"));
	testcall(test_url("http://example.com/foo/bar.html?baz", "foo.html",      "http    example.com    /foo/foo.html"));
	testcall(test_url("http://example.com/foo/bar.html?baz", "?quux",         "http    example.com    /foo/bar.html?quux"));
	testcall(test_url("http://example.com:80/",                               "http    example.com:80    /"));
	testcall(test_url("http://example.com:80/", "http://example.com:8080/",   "http    example.com:8080    /"));
	testcall(test_url_fail("http://example.com:80/", ""));
	testcall(test_url("http://a.com/foo/bar.html?baz#quux",  "#corge",                "http    a.com    /foo/bar.html?baz#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz",       "#corge",                "http    a.com    /foo/bar.html?baz#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "http://b.com/foo.html", "http    b.com    /foo.html#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "//b.com/bar/foo.html",  "http    b.com    /bar/foo.html#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "/bar/foo.html",         "http    a.com    /bar/foo.html#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "foo.html",              "http    a.com    /foo/foo.html#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "?quux",                 "http    a.com    /foo/bar.html?quux#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "http://b.com/#grault",  "http    b.com    /#grault"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "/bar/foo.html#grault",  "http    a.com    /bar/foo.html#grault"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "foo.html#grault",       "http    a.com    /foo/foo.html#grault"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "?quux#grault",          "http    a.com    /foo/bar.html?quux#grault"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "#grault",               "http    a.com    /foo/bar.html?baz#grault"));
	testcall(test_url("http://a.com:8080/",                  "//b.com/",              "http    b.com    /"));
}

static int n_socks;
static async<autoptr<socket2>> mksock_wrap(bool ssl, cstrnul host, uint16_t port)
{
	n_socks++;
	return socket2::create_sslmaybe(ssl, host, port);
}
static void wrap_socks(http_t& http)
{
	n_socks = 0;
	http.wrap_socks(mksock_wrap);
}

test("dummy","udp","tcp") {} // just for ordering
test("dummy","tcp","ssl") {} // just for ordering

co_test("http 1", "tcp", "http")
{
	test_skip("kinda slow");
	
	// most tests are https, not many sites offer http anymore
	http_t::rsp r = co_await http_t::get("http://floating.muncher.se/");
	
	string body = r.text_unsafe();
	assert_eq(r.status, 301);
	assert_gt(body.length(), 50);
	assert_eq(body.substr(0, 50), "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">");
	assert(body.endswith("</body></html>\n"));
	assert_eq(r.header("Location"), "https://floating.muncher.se/");
}

co_test("http 2", "tcp", "http")
{
	test_skip("kinda slow");
	
	http_t::rsp r = co_await http_t::get("https://floating.muncher.se/arlib/test.txt");
	assert_eq(r.status, 200);
	assert_eq(r.text(), "hello world");
}

co_test("http 3", "tcp", "http")
{
	test_skip("kinda slow");
	
	http_t::rsp r = co_await http_t::get("http://127.0.0.999/");
	assert_eq(r.status, http_t::e_connect);
}

test("http 4", "tcp", "http")
{
	test_skip("kinda slow");
	http_t http;
	wrap_socks(http);
	int n_done = 0;
	
	runloop2::detach([&]()->async<void> {
		http_t::req q1 = { "https://floating.muncher.se/arlib/test.txt" };
		http_t::rsp r1 = co_await http.request(q1);
		assert_eq(++n_done, 1);
		assert_eq(r1.status, 200);
		assert_eq(r1.text(), "hello world");
		http_t::req q4 = { "https://floating.muncher.se/arlib/test4.txt" };
		http_t::rsp r4 = co_await http.request(q4);
		assert_eq(++n_done, 4);
		assert_eq(r4.status, 200);
		assert_eq(r4.text(), "hello world 4");
	}());
	
	runloop2::detach([&]()->async<void> {
		http_t::req q2 = { "https://floating.muncher.se/arlib/test2.txt" };
		http_t::rsp r2 = co_await http.request(q2);
		assert_eq(++n_done, 2);
		assert_eq(r2.status, 200);
		assert_eq(r2.text(), "hello world 2");
	}());
	
	runloop2::detach([&]()->async<void> {
		http_t::req q3 = { "https://floating.muncher.se/arlib/test3.txt" };
		http_t::rsp r3 = co_await http.request(q3);
		assert_eq(++n_done, 3);
		assert_eq(r3.status, 200);
		assert_eq(r3.text(), "hello world 3");
	}());
	
	while (n_done < 4)
		runloop2::step();
	
	assert_eq(n_socks, 1);
}

test("http 5", "tcp", "http")
{
	test_skip("kinda slow");
	
	http_t http;
	wrap_socks(http);
	int n_done = 0;
	
	runloop2::detach([&]()->async<void> {
		http_t::req q1 = { "https://floating.muncher.se/404.html" };
		http_t::rsp r1 = co_await http.request(q1);
		assert_eq(++n_done, 1);
		assert_eq(r1.status, 404);
	}());
	
	runloop2::detach([&]()->async<void> {
		http_t::req q2 = { "https://stacked.muncher.se/404.html" };
		http_t::rsp r2 = co_await http.request(q2);
		assert_eq(++n_done, 2);
		assert_eq(r2.status, 404);
	}());
	
	runloop2::detach([&]()->async<void> {
		http_t::req q3 = { "https://floating.muncher.se/404.html" };
		http_t::rsp r3 = co_await http.request(q3);
		assert_eq(++n_done, 3);
		assert_eq(r3.status, 404);
	}());
	
	runloop2::run([&]()->async<void> {
		http_t::req q4 = { "https://floating.muncher.se/404.html" };
		http_t::rsp r4 = co_await http.request(q4);
		assert_eq(++n_done, 4);
		assert_eq(r4.status, 404);
	}());
	
	assert_eq(n_socks, 3);
}

test("http 6", "tcp", "http")
{
	test_skip("kinda slow");
	
	http_t http;
	wrap_socks(http);
	http_t::req q1;
	q1.url = "https://floating.muncher.se:443/arlib/echo.php"; // ensure it doesn't look at the port during ssl handshake
	// <?php
	// header("Content-Type: application/json");
	// echo json_encode(["post" => file_get_contents("php://input")]);
	q1.body.append('x');
	http_t::req q2 = q1;
	
	runloop2::detach([&]()->async<void> {
		http_t::rsp r = co_await http.request(q1);
		assert_eq(r.status, 200);
		assert_eq(r.text(), "{\"post\":\"x\"}");
	}());
	q2.body.append('y');
	runloop2::run([&]()->async<void> {
		http_t::rsp r = co_await http.request(q2);
		assert_eq(r.status, 200);
		assert_eq(r.text(), "{\"post\":\"xy\"}");
	}());
	
	assert_eq(n_socks, 1);
}

test("http 7", "tcp", "http")
{
	test_skip("kinda slow");
	
	http_t http;
	wrap_socks(http);
	http_t::req q1 = { "https://floating.muncher.se/arlib/test.txt" };
	q1.headers.append("Connection: close"); // ensure it tries again
	http_t::req q2 = q1;
	
	runloop2::detach([&]()->async<void> {
		http_t::rsp r = co_await http.request(q1);
		assert_eq(r.status, 200);
		assert_eq(r.text(), "hello world");
	}());
	runloop2::run([&]()->async<void> {
		http_t::rsp r = co_await http.request(q2);
		assert_eq(r.status, 200);
		assert_eq(r.text(), "hello world");
	}());
	
	assert_eq(n_socks, 2);
}

co_test("http 8", "tcp", "http")
{
	test_skip("kinda slow");
	
	http_t http;
	wrap_socks(http);
	
	http_t::req q1 = { "https://floating.muncher.se/" };
	http_t::rsp r1 = co_await http.request(q1);
	assert_eq(r1.status, 200);
	string body = r1.text();
	assert_gt(body.length(), 8000);
	assert(body.startswith("<"));
	assert(body.endswith(">\n"));
	
	http_t::req q2 = { "https://floating.muncher.se/" };
	q2.bytes_max = 2000;
	// 2000 is way below 8000, but the 8000 includes the http headers, and
	//  a up-to-4K buffer whose size is checked before parsing its contents
	http_t::rsp r2 = co_await http.request(q2);
	assert_eq(r2.status, http_t::e_too_big);
	
	// ensure it repairs itself after an overflow
	http_t::req q3 = { "https://floating.muncher.se/" };
	q3.bytes_max = 16777216;
	http_t::rsp r3 = co_await http.request(q3);
	assert_eq(r3.status, 200);
	assert_eq(r3.text(), body);
	
	assert_eq(n_socks, 2);
}

co_test("http 9", "tcp", "http")
{
	test_skip("kinda slow");
	
	http_t::rsp r = co_await http_t::get("https://floating.muncher.se/arlib/large.php");
	// <?php
	// for ($i=0;$i<10000;$i++) echo $i;
	
	assert_eq(r.status, 200);
	assert_eq(r.header("Transfer-Encoding"), "chunked");
	string body = r.text();
	assert(body.startswith("01234567891011121314151617181920"));
	assert_eq(body.length(), 38890);
	assert(body.endswith("99929993999499959996999799989999"));
}

co_test("http 10", "tcp", "http")
{
	test_skip("kinda slow");
	
	http_t http;
	
	http_t::form f;
	f.value("foo", "bar");
	f.value("bar", "baz");
	f.file("baz", "a.txt", cstring("hello world").bytes());
	f.file("quux", "b.txt", cstring("test test 123").bytes());
	
	http_t::req rq;
	rq.url = "https://floating.muncher.se/arlib/files.php";
	// <?php
	// header("Content-Type: text/plain");
	// foreach ($_POST as $k => $v) echo "$k = $v\n";
	// foreach ($_FILES as $k => $v)
	//     echo "$k = ${v["name"]} (", file_get_contents($v["tmp_name"]), ")\n";
	f.attach(rq);
	
	http_t::rsp r = co_await http.request(rq);
	
	assert_eq(r.status, 200);
	assert_eq(r.text(), "foo = bar\nbar = baz\nbaz = a.txt (hello world)\nquux = b.txt (test test 123)\n");
}

co_test("http 11", "tcp", "http")
{
	test_skip("kinda slow");
	
	uint8_t blob[300000];
	for (size_t i=0;i<sizeof(blob);i++) blob[i] = 'a' + (i&15);
	
	http_t http;
	
	http_t::req rq;
	rq.url = "https://floating.muncher.se/arlib/echo.php"; // same as an earlier test
	rq.body = blob;
	
	http_t::rsp r = co_await http.request(rq);
	
	assert_eq(r.status, 200);
	string body = r.text();
	assert_eq(body.length(), strlen("{\"post\":\"\"}")+sizeof(blob));
	assert_eq(body, "{\"post\":\""+cstring(blob)+"\"}");
}

co_test("http 12", "tcp", "http")
{
	test_skip("kinda slow");
	
	// if a request is pipelined then cancelled, the resulting response must not be paired with wrong request
	http_t http;
	wrap_socks(http);
	
	// first, create the socket
	http_t::req q1 = { "https://floating.muncher.se/arlib/test.txt" };
	http_t::rsp r1 = co_await http.request(q1);
	assert_eq(r1.status, 200);
	assert_eq(r1.text(), "hello world");
	
	class co_wait_token {
		void* ptr = nullptr;
	public:
		bool await_ready() { return ptr == (void*)1; }
		void await_suspend(std::coroutine_handle<> coro) { ptr = coro.address(); }
		void await_resume() {}
		
		void complete()
		{
			if (ptr == nullptr)
				ptr = (void*)1;
			else
				std::coroutine_handle<>::from_address(ptr).resume();
		}
	};
	
	// then send two requests, and cancel the second
	http_t::req q2 = { "https://floating.muncher.se/arlib/test2.txt" };
	struct waiter2_t : public waiter<http_t::rsp, waiter2_t> {
		co_wait_token cont;
		
		void complete(http_t::rsp r2)
		{
			assert_eq(r2.status, 200);
			assert_eq(r2.text(), "hello world 2");
			cont.complete();
		}
	} wait2;
	http.request(q2).then(&wait2);
	
	{
		http_t::req q3 = { "https://floating.muncher.se/arlib/test3.txt" };
		struct waiter3_t : public waiter<http_t::rsp, waiter3_t> {
			void complete(http_t::rsp val) { assert_unreachable(); }
		} wait3;
		http.request(q3).then(&wait3);
	}
	
	co_await wait2.cont;
	
	// finally, send a fourth request, and ensure either it's discarded, or the socket is reset
	http_t::req q4 = { "https://floating.muncher.se/arlib/test4.txt" };
	http_t::rsp r4 = co_await http.request(q4);
	assert_eq(r4.status, 200);
	assert_eq(r4.text(), "hello world 4");
}
#endif
#endif
