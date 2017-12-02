#ifdef ARLIB_SOCKET
#include "http.h"
#include "test.h"
#include "stringconv.h"

bool HTTP::parseUrl(cstring url, bool relative, location& out)
{
	int pos = 0;
	while (islower(url[pos])) pos++;
	if (url[pos]==':')
	{
		out.proto = url.substr(0, pos);
		url = url.substr(pos+1, ~0);
	}
	else if (!relative) return false;
	
	if (url.startswith("//"))
	{
		url = url.substr(2, ~0);
		
		array<string> host_loc = url.split<1>("/");
		out.path = "/"+host_loc[1];
		if (!host_loc[1])
		{
			host_loc = url.split<1>("?");
			if (host_loc[1])
			{
				out.path = "/?"+host_loc[1];
			}
		}
		array<string> domain_port = host_loc[0].split<1>(":");
		out.domain = domain_port[0];
		if (domain_port[1])
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
	else if (url[0]=='?') out.path = out.path.split<1>("?")[0] + url;
	else out.path = out.path.rsplit<1>("/")[0] + "/" + url;
	
	return true;
}

void HTTP::send(req q, function<void(rsp)> callback)
{
	rsp& r = requests.append();
	r.q = std::move(q);
	r.callback = callback;
	
	if (!lasthost.proto)
	{
		if (!parseUrl(r.q.url, false, this->lasthost)) return resolve_err_v(NULL, requests.size()-1, rsp::e_bad_url);
	}
	else
	{
		location loc;
		if (!parseUrl(r.q.url, false, loc)) return resolve_err_v(NULL, requests.size()-1, rsp::e_bad_url);
		if (loc.proto != lasthost.proto || loc.domain != lasthost.domain || loc.port != lasthost.port)
		{
			return resolve_err_v(NULL, requests.size()-1, rsp::e_different_url);
		}
		lasthost.path = loc.path;
	}
	
	activity(NULL);
}

void HTTP::try_compile_req()
{
	if (next_send == requests.size()) return;
	if (next_send > 1) return; // only pipeline two requests at once
	if (!sock) return;
	
	const req& q = requests[next_send].q;
	
	cstring method = q.method;
	if (!method) method = (q.postdata ? "POST" : "GET");
	if (method != "GET" && next_send != 0) return;
	
	location loc;
	parseUrl(q.url, false, loc); // known to succeed, it was tested in send()
	
	bytepipe tosend;
	tosend.push(method, " ", loc.path, " HTTP/1.1\r\n"); // FIXME: lasthost.path is wrong
	
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
	
	if (!httpHost) tosend.push("Host: ", loc.domain, "\r\n"); // FIXME: lasthost.domain is wrong
	if (method!="GET" && !httpContentLength) tosend.push("Content-Length: ", tostring(q.postdata.size()), "\r\n");
	if (method!="GET" && !httpContentType)
	{
		if (q.postdata && (q.postdata[0] == '[' || q.postdata[0] == '{'))
		{
			tosend.push("Content-Type: application/json\r\n");
		}
		else
		{
			tosend.push("Content-Type: application/x-www-form-urlencoded\r\n");
		}
	}
	if (!httpConnection) tosend.push("Connection: keep-alive\r\n");
	
	tosend.push("\r\n");
	
	tosend.push(q.postdata);
	
	bool ok = true;
	if (sock->send(tosend.pull_buf( )) < 0) ok = false;
	if (sock->send(tosend.pull_next()) < 0) ok = false;
	if (!ok) sock = NULL;
	
	next_send++;
}

void HTTP::activity(socket*)
{
	bool deleted = false;
newsock:
	if (deleted) return;
	
	if (requests.size() == 0)
	{
		if (!sock) return;
		
		uint8_t ignore[1];
		if (sock->recv(ignore) != 0) return sock_cancel(); // we shouldn't get anything at this point
		return;
	}
	
	if (!sock)
	{
		if (state == st_boundary)
		{
			//lasthost.proto/domain/port never changes between requests
			if (lasthost.proto == "https")  sock = socket::create_ssl(lasthost.domain, lasthost.port ? lasthost.port : 443, loop);
			else if (lasthost.proto == "http") sock = socket::create( lasthost.domain, lasthost.port ? lasthost.port : 80,  loop);
			else { resolve_err_v(&deleted, 0, rsp::e_bad_url); goto newsock; }
		}
		if (!sock) { resolve_err_v(&deleted, 0, rsp::e_connect); goto newsock; }
		sock->callback(bind_this(&HTTP::activity), NULL);
		
		state = st_boundary_retried;
		next_send = 0;
	}
	
	try_compile_req();
	
	array<byte> newrecv;
	if (sock->recv(newrecv) < 0) { sock = NULL; goto newsock; }
again:
	if (!newrecv) return;
	
	rsp& r = requests[0];
	
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
				if (fragment.startswith("HTTP/"))
				{
					string status_i = fragment.split<2>(" ")[1];
					fromstring(status_i, r.status);
					state = st_header;
				}
				else
				{
					sock = NULL;
					return resolve_err_v(NULL, 0, rsp::e_not_http);
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
							abort();
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
	resolve(&deleted, 0, true);
	if (deleted) return;
	try_compile_req();
	
	goto again;
}

static void test_url(cstring url, cstring url2, cstring proto, cstring domain, int port, cstring path)
{
	HTTP::location loc;
	assert(HTTP::parseUrl(url, false, loc));
	if (url2) assert(HTTP::parseUrl(url2, true, loc));
	assert_eq(loc.proto, proto);
	assert_eq(loc.domain, domain);
	assert_eq(loc.port, port);
	assert_eq(loc.path, path);
}
static void test_url(cstring url, cstring proto, cstring domain, int port, cstring path)
{
	test_url(url, "", proto, domain, port, path);
}
test("URL parser", "", "http")
{
	test_url("wss://gateway.discord.gg?v=5&encoding=json", "wss", "gateway.discord.gg", 0, "/?v=5&encoding=json");
	test_url("wss://gateway.discord.gg", "?v=5&encoding=json", "wss", "gateway.discord.gg", 0, "/?v=5&encoding=json");
	test_url("http://example.com/foo/bar.html?baz", "/bar/foo.html", "http", "example.com", 0, "/bar/foo.html");
	test_url("http://example.com/foo/bar.html?baz", "foo.html", "http", "example.com", 0, "/foo/foo.html");
	test_url("http://example.com/foo/bar.html?baz", "?quux", "http", "example.com", 0, "/foo/bar.html?quux");
}

test("HTTP", "tcp,ssl", "http")
{
	test_skip("too slow");
	
	socket::wrap_ssl(NULL, "", NULL); // bearssl takes forever to initialize, do it outside the runloop check
	
	autoptr<runloop> loop = runloop::create();
	HTTP::rsp r;
	//ugly, but the alternative is nesting lambdas forever or busywait. I need a way to break it anyways
	function<void(HTTP::rsp)> break_runloop = bind_lambda([&](HTTP::rsp inner_r) { r = std::move(inner_r); loop->exit(); });
	
#define URL "http://media.smwcentral.net/Alcaro/test.txt"
#define URL2 "http://media.smwcentral.net/Alcaro/test2.txt"
#define URL3 "http://media.smwcentral.net/Alcaro/test3.txt"
#define URL4 "http://media.smwcentral.net/Alcaro/test4.txt"
#define CONTENTS "hello world"
#define CONTENTS2 "hello world 2"
#define CONTENTS3 "hello world 3"
#define CONTENTS4 "hello world 4"
	{
		HTTP h(loop);
		
		h.send(HTTP::req(URL), break_runloop);
		loop->enter();
		assert_eq(r.text(), CONTENTS);
	}
	
	{
		HTTP h(loop);
		
		h.send(HTTP::req(URL), break_runloop);
		loop->enter();
		assert_eq(r.text(), CONTENTS);
		
		h.send(HTTP::req(URL), break_runloop);
		loop->enter();
		assert_eq(r.text(), CONTENTS);
	}
	
	{
		HTTP h(loop);
		
		h.send(HTTP::req(URL), break_runloop);
		h.send(HTTP::req(URL), break_runloop);
		
		loop->enter();
		assert_eq(r.text(), CONTENTS);
		loop->enter();
		assert_eq(r.text(), CONTENTS);
	}
	
	//ensure it doesn't mix up the URL it's supposed to request
	{
		HTTP h(loop);
		
		function<void(HTTP::rsp)> break_runloop_testc =
			bind_lambda([&](HTTP::rsp inner_r)
			{
				loop->exit();
				assert_eq(inner_r.text(), CONTENTS);
			});
		function<void(HTTP::rsp)> break_runloop_testc2 =
			bind_lambda([&](HTTP::rsp inner_r)
			{
				loop->exit();
				assert_eq(inner_r.text(), CONTENTS2);
			});
		function<void(HTTP::rsp)> break_runloop_testc3 =
			bind_lambda([&](HTTP::rsp inner_r)
			{
				loop->exit();
				assert_eq(inner_r.text(), CONTENTS3);
			});
		function<void(HTTP::rsp)> break_runloop_testc4 =
			bind_lambda([&](HTTP::rsp inner_r)
			{
				loop->exit();
				assert_eq(inner_r.text(), CONTENTS4);
			});
		
		h.send(HTTP::req(URL),  break_runloop_testc);
		h.send(HTTP::req(URL2), break_runloop_testc2);
		h.send(HTTP::req(URL3), break_runloop_testc3);
		h.send(HTTP::req(URL4), break_runloop_testc4);
		
		loop->enter();
		loop->enter();
		loop->enter();
		loop->enter();
	}
	
	{
		HTTP::req rq;
		rq.url = "http://media.smwcentral.net/Alcaro/test.php";
		rq.headers.append("Host: media.smwcentral.net");
		rq.postdata.append('x');
		
		HTTP h(loop);
		h.send(rq, break_runloop);
		h.send(rq, break_runloop);
		loop->enter();
		assert_eq(r.text(), "{\"post\":\"x\"}");
		loop->enter();
		assert_eq(r.text(), "{\"post\":\"x\"}");
	}
	
	{
		HTTP::req rq;
		rq.url = URL;
		rq.headers.append("Connection: close");
		
		HTTP h(loop);
		h.send(rq, break_runloop);
		h.send(rq, break_runloop);
		
		loop->enter();
		assert_eq(r.text(), CONTENTS);
		loop->enter();
		assert_eq(r.text(), CONTENTS); // ensure it tries again
	}
	
	/*
	//httpbin response time is super slow, and super erratic
	//it offers me unmatched flexibility in requesting strange http parameters,
	// but doubling the test suite runtime isn't worth it
	//getdiscordusers is chunked as well, I don't need two tests for that
	if (false)
	{
		HTTP::req r("https://httpbin.org/stream-bytes/128?chunk_size=30&seed=1");
		r.userdata = 42;
		HTTP h(loop);
		h.send(r);
		h.send(r);
		
		HTTP::rsp r1 = h.recv();
		assert_eq(r1.status, 200);
		assert_eq(r1.body.size(), 128);
		assert_eq(r1.q.userdata, 42);
		
		HTTP::rsp r2 = h.recv();
		assert_eq(r2.status, 200);
		assert_eq(r2.body.size(), 128);
		assert_eq(r2.q.userdata, 42);
		
		assert_eq(tostringhex(r1.body), tostringhex(r2.body));
	}
	*/
	
	{
		//throw in https for no reason
		HTTP h(loop);
		h.send(HTTP::req("https://www.smwcentral.net/ajax.php?a=getdiscordusers"), break_runloop);
		
		loop->enter();
		assert(r.success);
		assert_eq(r.status, 200);
		assert(r.body.size() > 20000);
		assert_eq(r.body[0], '[');
		assert_eq(r.body[r.body.size()-1], ']');
	}
}

test("","","")
{
	test_expfail("add support for canceling requests, so timeouts can be set, then use that in xdiscord");
}
#endif
