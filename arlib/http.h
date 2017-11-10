#pragma once
#ifdef ARLIB_SOCKET
#include "global.h"
#include "containers.h"
#include "socket.h"
#include "bytepipe.h"

class HTTP : nocopy {
public:
	HTTP(runloop* loop) : loop(loop) {}
	
	struct req {
		//Every field except 'url' is optional.
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
		
		uintptr_t userdata; // Not used by the HTTP object. It's passed unchanged in the rsp object.
		
		req() {}
		req(string url) : url(url) {}
	};
	
	struct rsp {
		req q;
		
		bool success;
		
		enum {
			e_bad_url       = -1, // couldn't parse URL
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
		
		
		//Used internally. Ignore it.
		function<void(rsp)> callback;
	};
	
	//Multiple requests may be sent to the same object. This will make them use HTTP Keep-Alive.
	//The requests must be to the same protocol-domain-port tuple.
	//Failures are reported in the callback.
	//Callbacks will be called in an arbitrary order. If there's more than one request, use different callbacks or vary the userdata.
	void send(req q, function<void(rsp)> callback);
	
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
	static bool parseUrl(cstring url, bool relative, location& out);
	
	~HTTP() { if (deleted_p) *deleted_p = true; }
	
private:
	void resolve(bool* deleted, size_t id, bool success)
	{
		requests[id].success = success;
		deleted_p = deleted;
		requests[id].callback(std::move(requests[id]));
		if (deleted && *deleted) return;
		else deleted_p = NULL;
		requests.remove(id);
		if (next_send > id) next_send--;
	}
	void resolve_err_v(bool* deleted, size_t id, int err)
	{
		requests[id].status = err;
		resolve(deleted, id, false);
	}
	bool resolve_err_f(bool* deleted, size_t id, int err) { resolve_err_v(deleted, id, err); return false; }
	
	void sock_cancel() { sock = NULL; }
	
	//if tosend is empty, adds requests[active_req] to tosend, then increments active_req; if not empty, does nothing
	//also sets this->location, if not set already
	void try_compile_req();
	
	void create_sock();
	void activity(socket*);
	
	
	location lasthost; // used to verify that the requests aren't sent anywhere they don't belong
	array<rsp> requests;
	size_t next_send = 0; // index to requests[] next to sock->send(), or requests.size() if all done / in tosend
	
	runloop* loop;
	autoptr<socket> sock;
	
	bool* deleted_p = NULL;
	
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
