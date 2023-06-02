#pragma once
#include "socket.h"
#include "random.h"

class http_t {
public:
	struct location {
		string scheme; // https
		string host;   // muncher.se:443
		string path;   // /index.html
		
		location() {}
		location(cstring url) { parse(url); }
		location(const char * url) { parse(url); }
		location& operator=(cstring url) { parse(url); return *this; }
		
		//If 'relative' is false, 'out' can be uninitialized. If true, must be fully valid. On failure, the location is undefined.
		//If the base URL contains a #fragment, and the new one does not, the #fragment is preserved. This matches HTTP 3xx but not <a href>.
		bool parse(cstring url, bool relative = false);
		
		bool same_origin(const location& other) const
		{
			// will incorrectly claim that http://example.com/ != http://example.com:80/ != http://example.com:080/
			// I don't care; port specifications are rare, and this function just affects keep-alive
			return scheme == other.scheme && host == other.host;
		}
		bool same_origin(cstring url) const
		{
			// could optimize this a bit maybe
			location other;
			if (!other.parse(url)) return false;
			return this->same_origin(other);
		}
		void set_origin(const location& other)
		{
			scheme = other.scheme;
			host = other.host;
			// but leave path blank or unchanged
		}
		
		string stringify() const { return scheme+"://"+host+path; }
	};
	
	// Matches the application/x-www-form-urlencoded POST rules (or at least Firefox's interpretation thereof).
	// Space becomes plus, *-._ and alphanumerics are left as is, everything else (including slash and UTF-8) is percent escaped.
	// I don't know which RFCs it matches - they're hard to read, and they don't clearly define whether % should be escaped.
	static string urlencode(cstring in);
	// Does the inverse of the above. Percent not followed by two hex digits is silently left as is. Can return NULs and invalid UTF-8.
	static string urldecode(cstring in);
	
	enum {
		e_bad_url  = -1, // couldn't parse URL
		e_connect  = -2, // couldn't resolve domain name, or couldn't open TCP or SSL stream (the request is guaranteed safe to retry)
		e_broken   = -3, // server unexpectedly closed connection
		e_not_http = -4, // the server isn't speaking HTTP
		e_too_big  = -5, // limit_bytes was reached
	};
	
	struct req {
		//Every field except 'loc' is optional.
		location loc;
		
		string method; // defaults to GET if body is empty, POST if nonempty
		// These headers are added automatically, if not already present (case sensitive):
		//  Connection: keep-alive
		//  Host: per url
		// Additionally, if method is POST:
		//  Content-Length: body.size(), if method is POST
		//  Content-Type: application/json if body starts with [ or {, else application/x-www-form-urlencoded
		array<string> headers;
		bytearray body;
		
		size_t bytes_max = 16*1024*1024; // Counts total bytes received in the HTTP response. For request_chunked(), applies to headers only.
	};
	struct rsp_base {
		req request;
		
		int status;
		bool complete = false;
		array<string> headers;
		
		bool success() const
		{
			return complete && status >= 200 && status <= 299;
		}
		operator bool() const
		{
			return success();
		}
		
		cstrnul header(cstring name) const
		{
			for (const string& h : headers)
			{
				if (!h.istartswith(name)) continue;
				cstrnul tmp = h.substr_nul(name.length());
				if (!tmp.startswith(":")) continue;
				if (tmp.startswith(": "))
					return tmp.substr_nul(2);
				else
					return tmp.substr_nul(1);
			}
			return {};
		}
		
		location follow(bool force = false) const
		{
			if (!force && !(status >= 300 && status <= 399))
				return {};
			location loc = request.loc;
			if (!loc.parse(header("Location"), true))
				return {};
			return loc;
		}
	};
	struct rsp : public rsp_base {
	private:
		friend class http_t;
		bytearray body_raw;
	public:
		
		rsp() = default;
		
		// The normal ones return empty body if the request was unsuccessful, for example 404.
		// Unsafe ones always return the body.
		bytearray body_take() { return success() ? body_take_unsafe() : nullptr; }
		string text_take() { return success() ? text_take_unsafe() : string(); }
		
		bytearray body_take_unsafe() { return std::move(body_raw); }
		string text_take_unsafe() { return string::create_usurp(std::move(body_raw)); }
		
		bytesr body() const { return success() ? body_unsafe() : nullptr; }
		cstring text() const { return success() ? text_unsafe() : cstring(); }
		
		bytesr body_unsafe() const { return body_raw; }
		cstring text_unsafe() const { return body_raw; }
	};
	// TODO: add a way to request chunked http response
	// TODO: find a usecase for requesting chunked http response
	
private:
	mksocket_t cb_mksock = socket2::create_sslmaybe;
	
	socketbuf sock;
	uint32_t sock_generation = 0; // Used to check if the socket was broken while waiting for mut2.
	uint16_t sock_sent = 0; // Used to check if anything was pipelined then cancelled, while waiting for mut2.
	uint16_t sock_received = 0; // Used to check if anything was pipelined then cancelled, while waiting for mut2.
	location sock_loc;
	// Set to now upon creating the socket, or completing a request.
	// If a request comes in while it's more than two seconds ago, keepalive is not available.
	timestamp sock_keepalive_until;
	
	co_mutex mut1; // Taken immediately, released when the socket is created.
	co_mutex mut2; // Taken after sending the request, or before creating the socket. Released when reading is done.
	
	bool can_keepalive(const req& q);
	bool can_pipeline(const req& q);
	void send_request(const req& q);
	rsp& set_error(rsp& r, int status);
	
public:
	
	async<rsp> request(req q);
	
#if defined(__GNUC__) && __GNUC__ < 13 && !defined(__clang__)
	// Extra overload because of https://gcc.gnu.org/bugzilla/show_bug.cgi?id=98401;
	//  an await-expression containing a list-initializer (for example co_await http.request({ .loc="http://example.com/" }))
	//  will double free the list-initializer, with the resulting Valgrind errors and worse.
	// To avoid this, the req must be a local created on a previous line.
	//  Any attempt to pass an initializer list directly will be unable to choose between the overloads, giving an error.
	// Once I drop support for GCC 12, all callers should be audited.
	struct bad_req { location loc; string method; array<string> headers; bytearray body; size_t bytes_max = 16*1024*1024; };
	async<rsp> request(bad_req q) = delete;
#endif
	
	// To replace the socket creation function. Intended for proxy support, but can be used for other purposes.
	void wrap_socks(mksocket_t cb) { cb_mksock = cb; }
	
	static async<rsp> get(cstring url);
	// Same as the above, but also accepts file:// URIs, and plain filenames.
	static async<bytearray> get_any(cstring uri, mksocket_t mksocket = socket2::create_sslmaybe);
	
	// helper to construct multipart/form-data, for HTTP file uploads
	// http://www.w3.org/TR/html401/interact/forms.html#h-17.13.4.2
	class form {
		string boundary;
		array<uint8_t> result;
		
		void set_boundary()
		{
			boundary = "--ArlibFormBoundary"+tostringhex<16>(g_rand.rand64()); // max 70 characters, not counting the two leading hyphens
		}
		
	public:
		form() { set_boundary(); }
		form(std::initializer_list<sarray<cstring, 2>> items)
		{
			set_boundary();
			for (auto[k,v] : items)
			{
				value(k, v);
			}
		}
		
		void value(cstring key, cstring value)
		{
			result += (boundary+"\r\n"
			           "Content-Disposition: form-data; name=\""+key+"\"\r\n\r\n"+
			           value+"\r\n").bytes();
		}
		
		void file(cstring key, cstring filename, arrayview<uint8_t> content)
		{
			result += (boundary+"\r\n"
			           "Content-Disposition: file; name=\""+key+"\"; filename=\""+filename.replace("\"","\\\"")+"\"\r\n\r\n").bytes();
			result += content;
			result += cstring("\r\n").bytes();
		}
		void file(cstring key, cstring filename, cstring content) { file(key, filename, content.bytes()); }
		
		//Can only be called once.
		array<uint8_t> pack()
		{
			result += (boundary+"--").bytes();
			return std::move(result);
		}
		
		//Counts as calling pack(), so only once.
		void attach(http_t::req& rq)
		{
			rq.body = pack();
			rq.headers.append("Content-Type: multipart/form-data; boundary="+boundary.substr(2, ~0));
		}
	};
};
