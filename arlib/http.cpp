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

void HTTP::try_compile_req()
{
	if (tosend.remaining()) return;
	if (active_req == requests.size()) return;
	
	const req& q = requests[active_req].q;
	if (!host.proto)
	{
		if (!parseUrl(q.url, false, this->host)) return error_v(active_req, rsp::e_bad_url);
	}
	else
	{
		location loc;
		if (!parseUrl(q.url, false, loc)) return error_v(active_req, rsp::e_bad_url);
		if (loc.proto != host.proto || loc.domain != host.domain || loc.port != host.port)
		{
			return error_v(active_req, rsp::e_different_url);
		}
		host.path = loc.path;
	}
	
	cstring method = q.method;
	if (!method) method = (q.postdata ? "POST" : "GET");
	tosend.push(method, " ", host.path, " HTTP/1.1\r\n");
	
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
	
	if (!httpHost) tosend.push("Host: ", host.domain, "\r\n");
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
	
	active_req++;
}

void HTTP::activity(bool block)
{
netagain:
	if (active_rsp == requests.size())
	{
		if (!sock) return;
		
		array<byte> ignore;
		if (sock->recv(ignore, false) < 0) return sock_cancel();
		return;
	}
	
	try_compile_req();
	
	if (!sock)
	{
		if (state == st_boundary)
		{
			if (host.proto == "https")  sock = socketssl::create(host.domain, host.port ? host.port : 443);
			else if (host.proto == "http") sock = socket::create(host.domain, host.port ? host.port : 80);
			else return error_v(active_rsp, rsp::e_bad_url);
		}
		if (!sock) return error_v(active_rsp, rsp::e_connect);
		
		state = st_boundary_retried;
		tosend.reset();
		active_req = active_rsp;
		
		try_compile_req();
	}
	
	array<byte> newrecv;
	
	if (tosend.remaining())
	{
		if (sock->recv(newrecv, false) < 0) return sock_cancel();
		
		if (!newrecv)
		{
			arrayview<byte> sendbuf = tosend.pull_buf();
			//this can give deadlocks if
			//- a request was made for something big
			//- a big request (big POST body) was sent on the same object
			//- the server does not infinitely buffer written data, but insists I read stuff on my end
			//- the network is jittery, so sock->recv() above returns empty
			//- and this is called from await(), not ready()
			//which is so unlikely I'll ignore it.
			//and it'll eventually exit, anyways; server will close the connection, breaking the send().
			int bytes = sock->sendp(sendbuf, block);
			if (bytes < 0) return sock_cancel();
			if (bytes > 0)
			{
				block = false;
				tosend.pull_done(bytes);
			}
		}
	}
	
	try_compile_req(); // to ensure tosend is never empty when there's stuff to do
	
	if (!newrecv && sock->recv(newrecv, block) < 0) return sock_cancel();
	block = false;
again:
	if (!newrecv) return;
	
	rsp& r = requests[active_rsp];
	
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
					return error_v(active_rsp, rsp::e_not_http);
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
		goto netagain;
	
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
		goto netagain;
	}
	abort(); // shouldn't be reachable
	
req_finish:
	state = st_boundary;
	r.success = true;
	requests[active_rsp].finished = true;
	active_rsp++;
	goto again;
}

bool HTTP::i_ready() const
{
	for (size_t i=0;i<requests.size();i++)
	{
		if (requests[i].finished) return true;
	}
	return false;
}

HTTP::rsp HTTP::recv()
{
	if (!requests.size())
	{
		rsp r;
		r.success = false;
		r.status = rsp::e_not_sent;
		r.q.userdata = 0;
		return r;
	}
	await();
	for (size_t i=0;i<requests.size();i++)
	{
		if (requests[i].finished)
		{
			//equal should be impossible
			if (active_req > i) active_req--;
			if (active_rsp > i) active_rsp--;
			return requests.pop(i);
		}
	}
	abort();
}

void HTTP::await()
{
	while (requests.size() && !i_ready()) activity(true);
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
test("URL parser")
{
	test_url("wss://gateway.discord.gg?v=5&encoding=json", "wss", "gateway.discord.gg", 0, "/?v=5&encoding=json");
	test_url("wss://gateway.discord.gg", "?v=5&encoding=json", "wss", "gateway.discord.gg", 0, "/?v=5&encoding=json");
	test_url("http://example.com/foo/bar.html?baz", "/bar/foo.html", "http", "example.com", 0, "/bar/foo.html");
	test_url("http://example.com/foo/bar.html?baz", "foo.html", "http", "example.com", 0, "/foo/foo.html");
	test_url("http://example.com/foo/bar.html?baz", "?quux", "http", "example.com", 0, "/foo/bar.html?quux");
}

test("HTTP")
{
	//test_skip("too slow");
#define URL "http://httpbin.org/user-agent"
#define CONTENTS "{\n  \"user-agent\": null\n}\n"
	{
		string ret = HTTP::request(URL);
		assert_eq(ret, CONTENTS);
	}
	
	{
		HTTP h;
		assert_eq(h.ready(), false);
		
		h.send(HTTP::req(URL));
		assert_eq(h.ready(), false);
		assert_eq((string)h.recv(), CONTENTS);
		assert_eq(h.recv().status, HTTP::rsp::e_not_sent);
		h.send((string)URL);
		assert_eq(h.recv().text(), CONTENTS);
		assert_eq(h.recv().status, HTTP::rsp::e_not_sent);
	}
	
	{
		HTTP::req r;
		r.url = "http://httpbin.org/post";
		r.headers.append("Host: httpbin.org");
		r.postdata.append('x');
		
		HTTP h;
		h.send(r);
		h.send(r);
		string data1 = (string)h.recv();
		assert(data1.startswith("{\n"));
		string data2 = (string)h.recv();
		assert_eq(data2, data1);
	}
	
	{
		HTTP::req r;
		r.url = URL;
		r.headers.append("Connection: close");
		
		HTTP h;
		h.send(r);
		h.send(r);
		assert_eq(h.ready(), false);
		assert_eq((string)h.recv(), CONTENTS);
		assert_eq((string)h.recv(), CONTENTS); // ensure it tries again
	}
	
	{
		HTTP::req r;
		r.url = URL;
		r.headers.append("Connection: close");
		
		HTTP h;
		h.send(r);
		assert_eq((string)h.recv(), CONTENTS);
		h.send(r); // ensure it opens a new socket
		assert_eq((string)h.recv(), CONTENTS);
		
		int readies = 0;
		for (int i=0;i<1000;i++)
		{
			socket::monitor mon;
			h.monitor(mon, (void*)42);
			if (mon.select(0) == (void*)42) readies++;
			assert_eq(h.ready(), false);
		}
		assert(readies < 10);
	}
	
	{
		HTTP::req r("https://httpbin.org/stream-bytes/128?chunk_size=30&seed=1"); // throw in a https test too for no reason
		r.userdata = 42;
		HTTP h;
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
	
	{
		HTTP::rsp r = HTTP::request(HTTP::req("https://www.smwcentral.net/ajax.php?a=getdiscordusers"));
		assert(r.success);
		assert_eq(r.status, 200);
		assert(r.body.size() > 20000);
		assert_eq(r.body[0], '[');
		assert_eq(r.body[r.body.size()-1], ']');
	}
}
#endif
