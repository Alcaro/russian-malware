#pragma once
#ifdef ARLIB_SOCKET
#include "global.h"
#include "array.h"
#include "socket.h"
#include "bytepipe.h"

class HTTP : nocopy {
public:
	HTTP(runloop* loop) : loop(loop) {}
	
	struct req {
		//Every field except 'url' is optional.
		string url;
		
		string method; // defaults to GET if body is empty, POST if nonempty
		//These headers are added automatically, if not already present:
		//Connection: keep-alive
		//Host: <from url>
		//Content-Length: <from body> (if not GET)
		//Content-Type: application/json if body starts with [ or {, and method is POST
		//           or application/x-www-form-urlencoded, if method is POST and body is something else
		array<string> headers; // TODO: multimap
		array<byte> body;
		
		enum {
			f_no_retry = 0x00000001,
		};
		uint32_t flags = 0;
		
		// Passed unchanged in the rsp object, and used for cancel(). Otherwise not used.
		uintptr_t id = 0;
		
		//If the server sends this much data (including headers/etc), or hasn't finished in the given time, fail.
		//They're approximate; a request may succeed if the server sends slightly more than this.
		uint64_t limit_ms = 5000;
		size_t limit_bytes = 1048576;
		
		req() {}
		req(string url) : url(url) {}
	};
	
	struct rsp {
		req q;
		
		enum {
			e_bad_url       = -1, // couldn't parse URL
			e_different_url = -2, // can't use Keep-Alive between these, create a new http object
			e_connect       = -3, // couldn't open TCP/SSL stream
			e_broken        = -4, // server unexpectedly closed connection, or timeout
			e_not_http      = -5, // the server isn't speaking HTTP
			e_canceled      = -6, // request was canceled; used only internally, callback is never called with this reason
			e_timeout       = -7, // limit_ms was reached
			e_too_big       = -8, // limit_bytes was reached
			//may also be a normal http status code (200, 302, 404, etc)
		};
		int status = 0;
		//string status_str; // useless
		
		array<string> headers; // TODO: switch to multimap once it exists
		array<byte> body;
		
		
		bool success() const
		{
			return (status >= 200 && status <= 299);
		}
		operator arrayvieww<byte>() const
		{
			if (success()) return body;
			else return NULL;
		}
		//operator string() { return body; } // throws ambiguity errors if enabled
		
		cstring header(cstring name) const
		{
			for (cstring head : headers)
			{
				if (head.istartswith(name) && head[name.length()]==':' && head[name.length()+1]==' ')
				{
					return head.substr(name.length()+2, ~0);
				}
			}
			return "";
		}
		
		cstring text() const { return body; }
	};
	
private:
	struct rsp_i {
		rsp r;
		bool sent_once = false; // used for f_no_retry
		function<void(rsp)> callback;
	};
public:
	
	//A custom socket creation function, if you want proxy support.
	void wrap_socks(function<socket*(bool ssl, cstring domain, int port, runloop* loop)> cb) { cb_mksock = cb; }
	
	//Multiple requests may be sent to the same object. This will make them use HTTP Keep-Alive.
	//The requests must be to the same protocol-domain-port tuple.
	//Failures are reported in the callback. Results are not guaranteed to be reported in any particular order.
	void send(req q, function<void(rsp)> callback);
	//If the HTTP object is currently trying to send a request with this ID, it's cancelled.
	//The callback won't be called, and unless the request has already been sent, it won't be.
	//If multiple have that ID, at least one is canceled; it's unspecified which, or if one or multiple are removed.
	//Returns whether anything happened.
	bool cancel(uintptr_t id);
	
	//Discards any unfinished requests, errors, and similar.
	void reset()
	{
		lasthost = location();
		requests.reset();
		next_send = 0;
		sock = NULL;
		state = st_boundary;
	}
	
	
	struct location {
		string proto;
		string domain;
		int port;
		string path;
	};
	//If 'relative' is false, 'out' can be uninitialized. If true, must be fully valid. On failure, the output location is undefined.
	static bool parseUrl(cstring url, bool relative, location& out);
	
	~HTTP();
	
private:
	void resolve(size_t id);
	void resolve_err_v(size_t id, int err)
	{
		requests[id].r.status = err;
		resolve(id);
	}
	bool resolve_err_f(size_t id, int err) { resolve_err_v(id, err); return false; }
	
	void sock_cancel() { sock = NULL; }
	
	//if tosend is empty, adds requests[active_req] to tosend, then increments active_req; if not empty, does nothing
	//also sets this->location, if not set already
	void try_compile_req();
	
	void create_sock();
	void activity();
	
	
	location lasthost; // used to verify that the requests aren't sent anywhere they don't belong
	array<rsp_i> requests;
	size_t next_send = 0; // index to requests[] next to sock->send(), or requests.size() if all done / in tosend
	
	runloop* loop;
	function<socket*(bool ssl, cstring domain, int port, runloop* loop)> cb_mksock = socket::create_sslmaybe;
	autoptr<socket> sock;
	
	void do_timeout();
	uintptr_t timeout_id = 0;
	
	size_t bytes_in_req;
	void reset_limits();
	
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	
	enum httpstate {
		st_boundary, // between requests; if socket closes, make a new one
		st_boundary_retried, // between requests; if socket closes, abort request
		
		st_status, // waiting for HTTP/1.1 200 OK
		st_header, // waiting for header, or \r\n\r\n
		st_body, // waiting for additional bytes, non-chunked
		st_body_chunk_len, // waiting for chunk length
		st_body_chunk, // waiting for chunk
		st_body_chunk_term, // waiting for final \r\n in chunk
		st_body_chunk_term_final, // waiting for final \r\n in terminating 0\r\n\r\n chunk
	};
	httpstate state = st_boundary;
	string fragment;
	size_t bytesleft;
};
#endif
