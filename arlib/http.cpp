#ifdef ARLIB_SOCKET
#include "http.h"

bool HTTP::parseUrl(cstring url, bool relative, location& out)
{
	if (!url) return false;
	
	int pos = 0;
	while (islower(url[pos])) pos++;
	if (url[pos]==':')
	{
		out.scheme = url.substr(0, pos);
		url = url.substr(pos+1, ~0);
	}
	else if (!relative) return false;
	
	string hash = out.path.contains("#") ? "#"+out.path.csplit<1>("#")[1] : "";
	
	if (url.startswith("//"))
	{
		url = url.substr(2, ~0);
		
		array<cstring> host_loc = url.csplit<1>("/");
		if (host_loc.size() == 1)
		{
			host_loc = url.csplit<1>("?");
			if (host_loc.size() == 2)
			{
				out.path = "/?"+host_loc[1];
			}
			else out.path = "/";
		}
		else out.path = "/"+host_loc[1];
		array<cstring> domain_port = host_loc[0].csplit<1>(":");
		out.domain = domain_port[0];
		if (domain_port.size() == 2)
		{
			if (!fromstring(domain_port[1], out.port)) return false;
			if (out.port <= 0 || out.port > 65535) return false;
		}
		else
		{
			out.port = 0;
		}
	}
	else if (!relative) return false;
	else if (url[0]=='/') out.path = url;
	else if (url[0]=='?') out.path = out.path.csplit<1>("?")[0] + url;
	else if (url[0]=='#') out.path = out.path.csplit<1>("#")[0] + url;
	else out.path = out.path.crsplit<1>("/")[0] + "/" + url;
	
	if (!url.contains("#")) out.path += hash;
	
	return true;
}

void HTTP::send(req q, function<void(rsp)> callback)
{
	rsp_i& i = requests.append();
	i.callback = callback;
	i.n_tries_left = ((q.flags & req::f_no_retry) ? 1 : 2);
	rsp& r = i.r;
	r.q = std::move(q);
	
	if (!lasthost.scheme)
	{
		if (!parseUrl(r.q.url, false, this->lasthost)) return resolve_err_v(requests.size()-1, rsp::e_bad_url);
	}
	else
	{
		location loc;
		if (!parseUrl(r.q.url, false, loc)) return resolve_err_v(requests.size()-1, rsp::e_bad_url);
		if (loc.scheme != lasthost.scheme || loc.domain != lasthost.domain || loc.port != lasthost.port)
		{
			return resolve_err_v(requests.size()-1, rsp::e_different_url);
		}
		lasthost.path = loc.path;
	}
	
	if (requests.size() == 1)
		reset_limits();
	activity(); // to create socket and compile request
}

bool HTTP::cancel(uintptr_t id)
{
	for (rsp_i& i : requests)
	{
		if (i.r.q.id == id)
		{
			i.r.status = rsp::e_canceled;
			i.r.q.limit_bytes = 80000; // if we canceled something small, let it finish; if huge, let socket die
			i.callback = NULL; // in case it holds a reference to something important
			return true;
		}
	}
	return false;
}

void HTTP::try_compile_req()
{
again:
	if (next_send == requests.size()) return;
	if (next_send > 1) return; // only pipeline two requests at once
	if (!sock) return;
	
	rsp_i& ri = requests[next_send];
	rsp& r = ri.r;
	if (r.status == rsp::e_canceled)
	{
		requests.remove(next_send);
		goto again;
	}
	const req& q = r.q;
	
	cstring method = q.method;
	if (!method) method = (q.body ? "POST" : "GET");
	if (method != "GET" && next_send != 0)
		return;
	
	if (ri.n_tries_left == 1 && next_send != 0)
		return;
	
	if (ri.n_tries_left-- <= 0)
	{
		if (r.status >= 0) r.status = rsp::e_broken;
		RETURN_IF_CALLBACK_DESTRUCTS(resolve(next_send));
		goto again;
	}
	
	location loc;
	parseUrl(q.url, false, loc); //known to succeed, it was tested in send()
	
	bytepipe tosend;
	tosend.push(method, " ", loc.path, " HTTP/1.1\r\n");
	
	bool httpHost = false;
	bool httpContentLength = false;
	bool httpContentType = false;
	bool httpConnection = false;
	for (cstring head : q.headers)
	{
		if (head.startswith("Host:")) httpHost = true;
		if (head.startswith("Content-Length:")) httpContentLength = true;
		if (head.startswith("Content-Type:")) httpContentType = true;
		if (head.startswith("Connection:")) httpConnection = true;
		tosend.push(head, "\r\n");
	}
	
	if (!httpHost) tosend.push("Host: ", loc.domain, "\r\n");
	if (method != "GET" && !httpContentLength) tosend.push("Content-Length: ", tostring(q.body.size()), "\r\n");
	if (method != "GET" && !httpContentType)
	{
		if (q.body && (q.body[0] == '[' || q.body[0] == '{'))
			tosend.push("Content-Type: application/json\r\n");
		else
			tosend.push("Content-Type: application/x-www-form-urlencoded\r\n");
	}
	if (!httpConnection) tosend.push("Connection: keep-alive\r\n");
	
	tosend.push("\r\n");
	
	tosend.push(q.body);
	
	bool ok = true;
	if (ok && sock->send(tosend.pull_buf( )) < 0) ok = false;
	if (ok && sock->send(tosend.pull_next()) < 0) ok = false;
	if (!ok) sock = NULL;
	
	next_send++;
}

void HTTP::resolve(size_t id)
{
	rsp_i i = std::move(requests[id]);
	
	requests.remove(id);
	if (next_send > id) next_send--;
	
	if (i.r.status != rsp::e_canceled)
	{
		i.callback(std::move(i.r));
	}
	i.callback = NULL; // destroy this before the delete_protector
}

void HTTP::do_timeout()
{
	if (requests.size() != 0)
		requests[0].r.status = rsp::e_timeout;
	
	sock_cancel();
	timeout.reset();
	
	activity(); // can delete 'this', don't do anything fancy afterwards
}

void HTTP::reset_limits()
{
	this->bytes_in_req = 0;
	if (requests.size() != 0)
	{
		timeout.set_once(this->requests[0].r.q.limit_ms, bind_this(&HTTP::do_timeout));
	}
}

void HTTP::activity()
{
	//TODO: replace this state machine with bytepipe::pull_line
newsock:
	if (requests.size() == 0)
	{
	discard_sock:
		if (!sock) return;
		
		uint8_t ignore[1];
		if (sock->recv(ignore) != 0) return sock_cancel(); // we shouldn't get anything at this point
		return;
	}
	
	if (!sock)
	{
		if (state == st_boundary || state == st_boundary_retried)
		{
			//lasthost.proto/domain/port never changes between requests
			int defport;
			if (lasthost.scheme == "http") defport = 80;
#ifdef ARLIB_SSL
			else if (lasthost.scheme == "https") defport = 443;
#endif
			else { RETURN_IF_CALLBACK_DESTRUCTS(resolve_err_v(0, rsp::e_bad_url)); goto newsock; }
			sock = cb_mksock(
#ifdef ARLIB_SSL
			                 defport==443,
#endif
			                 lasthost.domain, lasthost.port ? lasthost.port : defport, loop);
		}
		if (!sock) { RETURN_IF_CALLBACK_DESTRUCTS(resolve_err_v(0, rsp::e_connect)); goto newsock; }
		sock->callback(bind_this(&HTTP::activity), NULL);
		
		state = st_boundary_retried;
		next_send = 0;
		
		reset_limits();
	}
	
	if (!sock) goto newsock;
	try_compile_req();
	if (!sock) goto newsock;
	
	array<uint8_t> newrecv;
	if (sock->recv(newrecv) < 0) { sock = NULL; goto newsock; }
	this->bytes_in_req += newrecv.size();
	
again:
	if (requests.size() == 0)
		goto discard_sock;
	rsp& r = requests[0].r;
	
	if (r.status == rsp::e_timeout || this->bytes_in_req > r.q.limit_bytes)
	{
		if (r.status != rsp::e_timeout) r.status = rsp::e_too_big;
		sock = NULL;
		goto req_finish;
	}
	
	if (!newrecv) return;
	
	switch (state)
	{
	case st_boundary:
	case st_boundary_retried:
		fragment = "";
		state = st_status;
		goto again;
	
	case st_status:
	case st_header:
	case st_body_chunk_len:
	case st_body_chunk_term:
	case st_body_chunk_term_final:
		if (newrecv.contains('\n'))
		{
			size_t n = newrecv.find('\n');
			fragment += newrecv.slice(0, n);
			if (fragment.endswith("\r")) fragment = fragment.substr(0, ~1);
			newrecv = newrecv.skip(n+1);
			
			if (state == st_status)
			{
				if (fragment.length() > 10 && fragment.startswith("HTTP/1.") && fragment[8] == ' ')
				{
					string status_i = fragment.split<2>(" ")[1];
					if (!fromstring(status_i, r.status) || r.status < 100 || r.status > 599)
					{
						sock = NULL;
						return resolve_err_v(0, rsp::e_not_http);
					}
					state = st_header;
				}
				else
				{
					sock = NULL;
					return resolve_err_v(0, rsp::e_not_http);
				}
			}
			else if (state == st_header)
			{
				if (fragment != "")
				{
					r.headers.append(fragment);
				}
				else
				{
					string transferEncoding = r.header("Transfer-Encoding");
					if (transferEncoding)
					{
						if (transferEncoding == "chunked")
						{
							state = st_body_chunk_len;
						}
						else
						{
							//valid: chunked, (compress, deflate, gzip), identity
							//ones in parens only with Accept-Encoding
							sock = NULL;
							//e_not_http isn't completely accurate, but good enough
							//a proper http server doesn't use this header like this
							return resolve_err_v(0, rsp::e_not_http);
						}
					}
					else
					{
						cstring lengthstr = r.header("Content-Length");
						if (!lengthstr && r.status == 204) bytesleft = 0; // 204 No Content
						else if (!fromstring(r.header("Content-Length"), bytesleft))
						{
							bytesleft = -1;
						}
						state = st_body;
						if (bytesleft == 0) goto req_finish;
					}
				}
			}
			else if (state == st_body_chunk_len)
			{
				fromstringhex(fragment, bytesleft);
				if (bytesleft) state = st_body_chunk;
				else state = st_body_chunk_term_final;
			}
			else if (state == st_body_chunk_term)
			{
				state = st_body_chunk_len;
			}
			else // st_body_chunk_term_final
			{
				goto req_finish;
			}
			fragment = "";
			goto again;
		}
		else fragment += (string)newrecv;
		return;
	
	case st_body:
	case st_body_chunk:
		size_t bytes = min(newrecv.size(), bytesleft);
		r.body += newrecv.slice(0, bytes);
		if (bytesleft != (size_t)-1) bytesleft -= bytes;
		
		if (!bytesleft)
		{
			newrecv = newrecv.skip(bytes);
			if (state == st_body)
			{
				goto req_finish;
			}
			else
			{
				state = st_body_chunk_term;
				goto again;
			}
		}
		return;
	}
	abort(); // shouldn't be reachable
	
req_finish:
	state = st_boundary;
	RETURN_IF_CALLBACK_DESTRUCTS(resolve(0));
	try_compile_req();
	reset_limits();
	
	if (!requests.size())
	{
		if (newrecv) sock_cancel(); // we shouldn't get anything at this point
		//return immediately, so we don't poke requests[0] if that doesn't exist
		return;
	}
	goto again;
}

#include "test.h"
#include "os.h"

static void test_url(cstring url, cstring url2, cstring scheme, cstring domain, int port, cstring path)
{
	HTTP::location loc;
	assert(HTTP::parseUrl(url, false, loc));
	if (url2) assert(HTTP::parseUrl(url2, true, loc));
	assert_eq(loc.scheme, scheme);
	assert_eq(loc.domain, domain);
	assert_eq(loc.port, port);
	assert_eq(loc.path, path);
}
static void test_url(cstring url, cstring scheme, cstring domain, int port, cstring path)
{
	test_url(url, "", scheme, domain, port, path);
}
static void test_url_fail(cstring url, cstring url2)
{
	HTTP::location loc;
	assert(HTTP::parseUrl(url, false, loc));
	assert(!HTTP::parseUrl(url2, true, loc));
}
test("URL parser", "string", "http")
{
	testcall(test_url("wss://gateway.discord.gg/?v=5&encoding=json",          "wss", "gateway.discord.gg", 0, "/?v=5&encoding=json"));
	testcall(test_url("wss://gateway.discord.gg?v=5&encoding=json",           "wss", "gateway.discord.gg", 0, "/?v=5&encoding=json"));
	testcall(test_url("wss://gateway.discord.gg", "?v=5&encoding=json",       "wss", "gateway.discord.gg", 0, "/?v=5&encoding=json"));
	testcall(test_url("http://example.com/foo/bar.html?baz", "/bar/foo.html", "http", "example.com", 0, "/bar/foo.html"));
	testcall(test_url("http://example.com/foo/bar.html?baz", "foo.html",      "http", "example.com", 0, "/foo/foo.html"));
	testcall(test_url("http://example.com/foo/bar.html?baz", "?quux",         "http", "example.com", 0, "/foo/bar.html?quux"));
	testcall(test_url("http://example.com:80/",                               "http", "example.com", 80, "/"));
	testcall(test_url("http://example.com:80/", "http://example.com:8080/",   "http", "example.com", 8080, "/"));
	testcall(test_url_fail("http://example.com:80/", ""));
	testcall(test_url("http://a.com/foo/bar.html?baz#quux",  "#corge",                "http", "a.com", 0, "/foo/bar.html?baz#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz",       "#corge",                "http", "a.com", 0, "/foo/bar.html?baz#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "http://b.com/foo.html", "http", "b.com", 0, "/foo.html#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "//b.com/bar/foo.html",  "http", "b.com", 0, "/bar/foo.html#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "/bar/foo.html",         "http", "a.com", 0, "/bar/foo.html#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "foo.html",              "http", "a.com", 0, "/foo/foo.html#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "?quux",                 "http", "a.com", 0, "/foo/bar.html?quux#corge"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "http://b.com/#grault",  "http", "b.com", 0, "/#grault"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "/bar/foo.html#grault",  "http", "a.com", 0, "/bar/foo.html#grault"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "foo.html#grault",       "http", "a.com", 0, "/foo/foo.html#grault"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "?quux#grault",          "http", "a.com", 0, "/foo/bar.html?quux#grault"));
	testcall(test_url("http://a.com/foo/bar.html?baz#corge", "#grault",               "http", "a.com", 0, "/foo/bar.html?baz#grault"));
	testcall(test_url("http://a.com:8080/",                  "//b.com/",              "http", "b.com", 0, "/"));
}

test("HTTP", "tcp,ssl,random", "http")
{
	test_skip("too slow");
	
	autoptr<runloop> loop = runloop::create();
	//runloop* loop = runloop::global();
	HTTP::rsp r;
	//ugly, but the alternative is nesting lambdas forever or busywait. and I need a way to break it anyways
	function<void(HTTP::rsp)> break_runloop = bind_lambda([&](HTTP::rsp inner_r) { r = std::move(inner_r); loop->exit(); });
	
	//TODO: some of these tests are redundant and slow; find and purge, or parallelize
	
#define URL "https://floating.muncher.se/arlib/test.txt"
#define URL2 "https://floating.muncher.se/arlib/test2.txt"
#define URL3 "https://floating.muncher.se/arlib/test3.txt"
#define URL4 "https://floating.muncher.se/arlib/test4.txt"
#define CONTENTS "hello world"
#define CONTENTS2 "hello world 2"
#define CONTENTS3 "hello world 3"
#define CONTENTS4 "hello world 4"
//#define T puts(tostring(__LINE__));
//#define T printf("%d %lu\n",__LINE__, tx.ms_reset());
//timer tx;
//#define T if (__LINE__ >= 590)
#ifndef T
#define T /* */
#endif
	T {
		// most tests are https, not many sites offer http anymore
		HTTP h(loop);
		
		h.send(HTTP::req("http://floating.muncher.se/"), break_runloop);
		loop->enter();
		assert_eq(r.status, 301);
		assert_eq(r.text(),
			"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n"
			"<html><head>\n<title>301 Moved Permanently</title>\n</head><body>\n"
			"<h1>Moved Permanently</h1>\n<p>The document has moved <a href=\"https://floating.muncher.se/\">here</a>.</p>\n"
			"<hr>\n<address>Apache/2.4.38 (Debian) Server at floating.muncher.se Port 80</address>\n</body></html>\n");
		assert_eq(r.header("Location"), "https://floating.muncher.se/");
	}
	
	T {
		HTTP h(loop);
		
		h.send(HTTP::req(URL), break_runloop);
		loop->enter();
		assert_eq(r.status, 200);
		assert_eq(r.text(), CONTENTS);
	}
	
	T {
		HTTP h(loop);
		
		function<void(HTTP::rsp)> break_runloop_testc =
			bind_lambda([&](HTTP::rsp inner_r)
			{
				loop->exit();
				assert_eq(inner_r.status, 200);
				assert_eq(inner_r.text(), CONTENTS);
			});
		function<void(HTTP::rsp)> break_runloop_testc2 =
			bind_lambda([&](HTTP::rsp inner_r)
			{
				loop->exit();
				assert_eq(inner_r.status, 200);
				assert_eq(inner_r.text(), CONTENTS2);
			});
		function<void(HTTP::rsp)> break_runloop_testc3 =
			bind_lambda([&](HTTP::rsp inner_r)
			{
				loop->exit();
				assert_eq(inner_r.status, 200);
				assert_eq(inner_r.text(), CONTENTS3);
			});
		function<void(HTTP::rsp)> break_runloop_testc4 =
			bind_lambda([&](HTTP::rsp inner_r)
			{
				loop->exit();
				assert_eq(inner_r.status, 200);
				assert_eq(inner_r.text(), CONTENTS4);
			});
		
		h.send(HTTP::req(URL),  break_runloop_testc);
		h.send(HTTP::req(URL2), break_runloop_testc2);
		h.send(HTTP::req(URL3), break_runloop_testc3);
		loop->enter();
		h.send(HTTP::req(URL4), break_runloop_testc4);
		loop->enter();
		loop->enter();
		loop->enter();
	}
	
	T {
		HTTP::req rq;
		rq.url = "https://floating.muncher.se/arlib/echo.php";
		// <?php
		// header("Content-Type: application/json");
		// echo json_encode(["post" => file_get_contents("php://input")]);
		rq.headers.append("Host: floating.muncher.se");
		rq.body.append('x');
		
		HTTP h(loop);
		h.send(rq, break_runloop);
		rq.body.append('y');
		h.send(rq, break_runloop);
		
		loop->enter();
		assert_eq(r.status, 200);
		assert_eq(r.text(), "{\"post\":\"x\"}");
		
		loop->enter();
		assert_eq(r.status, 200);
		assert_eq(r.text(), "{\"post\":\"xy\"}");
	}
	
	T {
		HTTP::req rq;
		rq.url = URL;
		rq.limit_bytes = 2000;
		rq.headers.append("Connection: close");
		
		HTTP h(loop);
		h.send(rq, break_runloop);
		h.send(rq, break_runloop);
		
		loop->enter();
		assert_eq(r.status, 200);
		assert_eq(r.text(), CONTENTS);
		
		loop->enter();
		assert_eq(r.status, 200);
		assert_eq(r.text(), CONTENTS); // ensure it tries again
	}
	
	//httpbin response time is super slow, and super erratic
	//it offers me unmatched flexibility in requesting strange http parameters,
	// but doubling the test suite runtime isn't worth it
	//yiram is chunked as well, I don't need two tests for that
	T if (false)
	{
		HTTP::req rq("https://httpbin.org/stream-bytes/128?chunk_size=30&seed=1");
		rq.id = 42;
		
		HTTP h(loop);
		h.send(rq, break_runloop);
		h.send(rq, break_runloop);
		
		loop->enter();
		HTTP::rsp r1 = r;
		assert_eq(r1.status, 200);
		assert_eq(r1.body.size(), 128);
		assert_eq(r1.q.id, 42);
		
		loop->enter();
		HTTP::rsp r2 = r;
		assert_eq(r2.status, 200);
		assert_eq(r2.body.size(), 128);
		assert_eq(r2.q.id, 42);
		
		assert_eq(tostringhex(r1.body), tostringhex(r2.body));
	}
	
	T {
		HTTP h(loop);
		h.send(HTTP::req("https://floating.muncher.se/"), break_runloop);
		
		loop->enter();
		assert_eq(r.status, 200);
		assert_gt(r.body.size(), 8000);
		assert_eq(r.body[0], '<');
		assert_eq(r.body[r.body.size()-2], '>');
		assert_eq(r.body[r.body.size()-1], '\n');
		
		HTTP::req rq("https://floating.muncher.se/");
		rq.limit_bytes = 8000;
		h.send(rq, break_runloop);
		
		loop->enter();
		//2000 is way below 8000, but the 8000 includes the http headers, and
		// a up-to-4K buffer whose size is checked before parsing its contents
		assert_gte(r.body.size(), 2000);
		assert_eq(r.status, HTTP::rsp::e_too_big);
	}
	
	T {
		HTTP h(loop);
		h.send(HTTP::req("https://floating.muncher.se/arlib/large.php"), break_runloop);
		// <?php
		// for ($i=0;$i<10000;$i++) echo $i;
		
		loop->enter();
		assert_eq(r.status, 200);
		assert_eq(r.header("Transfer-Encoding"), "chunked");
		assert_gte(r.body.size(), 2000);
		assert_eq(r.body[0], '0');
		assert_eq(r.body[r.body.size()-1], '9');
	}
	
	T {
		HTTP::form f;
		f.value("foo", "bar");
		f.value("bar", "baz");
		f.file("baz", "a.txt", cstring("hello world").bytes());
		f.file("quux", "b.txt", cstring("test test 123").bytes());
		
		HTTP::req rq;
		rq.url = "https://floating.muncher.se/arlib/files.php";
		// <?php
		// header("Content-Type: text/plain");
		// foreach ($_POST as $k => $v) echo "$k = $v\n";
		// foreach ($_FILES as $k => $v)
		//     echo "$k = ${v["name"]} (", file_get_contents($v["tmp_name"]), ")\n";
		f.attach(rq);
		
		HTTP h(loop);
		h.send(rq, break_runloop);
		
		loop->enter();
		assert_eq(r.status, 200);
		assert_eq(r.text(), "foo = bar\nbar = baz\nbaz = a.txt (hello world)\nquux = b.txt (test test 123)\n");
	}
	
	T {
		uint8_t blob[100000];
		for (size_t i=0;i<sizeof(blob);i++) blob[i] = 'a' + (i&15);
		runloop_blocktest_recycle(loop);
		
		HTTP::req rq;
		rq.url = "https://floating.muncher.se/arlib/echo.php"; // same as an earlier test
		rq.body = blob;
		rq.limit_ms = 2000 + sizeof(blob)/250;
		rq.limit_bytes = sizeof(blob)+32768;
		
		HTTP h(loop);
		h.send(rq, break_runloop);
		
		loop->enter();
		assert_eq(r.status, 200);
		assert_eq(r.body.size(), strlen("{\"post\":\"\"}")+sizeof(blob));
		assert_eq(r.text(), "{\"post\":\""+cstring(blob)+"\"}");
		
		timer t;
		rq.limit_ms = 20;
		h.send(rq, break_runloop);
		
		loop->enter();
		assert_lt(t.ms(), 500);
		assert_eq(r.status, HTTP::rsp::e_timeout);
		
		//ensure it repairs itself after a timeout
		h.send(HTTP::req(URL), break_runloop);
		loop->enter();
		assert_eq(r.status, 200);
		assert_eq(r.text(), CONTENTS);
	}
	
	T {}
}
#endif
