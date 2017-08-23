#pragma once
#ifdef ARLIB_SOCKET
#include "global.h"
#include "containers.h"
#include "socket.h"
#include "bytepipe.h"

class HTTP : nocopy {
public:
	struct req {
		//Every field except 'url' is safe to leave as default.
		string url;
		
		string method;
		//These headers are added automatically, if not already present:
		//Connection: keep-alive
		//Host: <from url>
		//Content-Length: <from postdata> (if not GET)
		//Content-Type: application/x-www-form-urlencoded
		//           or application/json if postdata starts with [ or {
		array<string> headers; // TODO: multimap
		array<byte> postdata;
		
		uintptr_t userdata; // Not used by the HTTP object.
		
		req() {}
		req(string url) : url(url) {}
	};
	
	struct rsp {
		req q;
		
		bool finished; // Always true for anything returned by the HTTP object, 
		bool success;
		
		enum {
			e_not_sent      = -1, // too few send() calls
			e_bad_url       = -2, // couldn't parse URL
			e_different_url = -2, // can't use Keep-Alive between these, create a new http object
			e_connect       = -3, // couldn't open TCP/SSL stream
			e_broken        = -4, // server unexpectedly closed connection, or timeout
			                      // if partial=true in recv(), this may be a real status code instead; to detect this, look for success=false
			e_not_http      = -5, // the server isn't speaking HTTP
			//may also be a normal http status code (200, 302, 404, etc)
		};
		int status;
		//string status_str; // useless
		
		array<string> headers; // TODO: switch to multimap once it exists
		array<byte> body;
		
		
		operator arrayvieww<byte>()
		{
			if (status >= 200 && status <= 299) return body;
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
	
	static string request(string url) { return std::move(request((req)url).body); }
	static rsp request(const req& q) { HTTP http; http.send(q); return std::move(http.recv()); }
	
	//Multiple requests may be sent to the same object. This will make them use HTTP Keep-Alive.
	//The requests must be to the same protocol-domain-port tuple.
	//Always succeeds, failures are reported in recv(). Failures may be reported in arbitrary order.
	//They will be returned in an arbitrary order. Use the userdata to keep them apart.
	void send(req q)
	{
		rsp& r = requests.append();
		r.q = std::move(q);
		activity(false);
	}
	bool ready() { activity(false); return i_ready(); }
	//void progress(size_t& done, size_t& total); // Both are 0 if unknown, or if empty. Do not use for checking if it's done, use ready().
	
	// Waits until a response object is ready to be returned. If no request was made, returns status=e_not_sent, userdata=0.
	rsp recv();
	void await(); // Waits until recv() would return immediately.
	
	//Discards any unfinished requests, errors, and similar.
	void reset()
	{
		host = location();
		requests.reset();
		active_req = 0;
		active_rsp = 0;
		tosend.reset();
		sock = NULL;
		state = st_boundary;
	}
	
	//If this key is returned, call .ready(), then .monitor() again.
	//This object may be returned even if no request is pending. In this case, call .ready() anyways; it will return false.
	void monitor(socket::monitor& mon, void* key) { if (sock) mon.add(sock, key, true, tosend.remaining()); }
	
	
	struct location {
		string proto;
		string domain;
		int port;
		string path;
	};
	static bool parseUrl(cstring url, bool relative, location& out);
	
	
private:
	bool i_ready() const;
	
	void error_v(size_t id, int err)
	{
		requests[id].finished = true;
		requests[id].success = false;
		requests[id].status = err;
	}
	bool error_f(size_t id, int err) { error_v(id, err); return false; }
	
	void sock_cancel() { sock = NULL; }
	
	//if tosend is empty, adds requests[active_req] to tosend, then increments active_req; if not empty, does nothing
	//also sets this->location, if not set already
	void try_compile_req();
	
	void create_sock();
	void activity(bool block);
	
	
	location host; // used to verify that the requests aren't sent anywhere they don't belong
	array<rsp> requests;
	size_t active_req = 0; // index to requests[] next to be added to tosend, or requests.size() if all done / in tosend
	size_t active_rsp = 0; // index to requests[] currently being returned; if state is st_boundary, this is the next one to be returned
	
	autoptr<socket> sock;
	bytepipe tosend;
	
	enum {
		st_boundary, // between requests; if socket closes, make a new one
		st_boundary_retried, // between requests; if socket closes, abort request
		
		st_status, // waiting for HTTP/1.1 200 OK
		st_header, // waiting for header, or \r\n\r\n
		st_body, // waiting for additional bytes, non-chunked
		st_body_chunk_len, // waiting for chunk length
		st_body_chunk, // waiting for chunk
		st_body_chunk_term, // waiting for final \r\n in chunk
		st_body_chunk_term_final, // waiting for final \r\n in terminating 0\r\n\r\n chunk
	} state = st_boundary;
	string fragment;
	size_t bytesleft;
};
#endif
