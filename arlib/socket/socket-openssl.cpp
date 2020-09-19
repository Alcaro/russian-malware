#include "socket.h"

#ifdef ARLIB_SSL_OPENSSL
#include "../thread.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

static SSL_CTX* ctx;

RUN_ONCE_FN(initialize)
{
	//SSL_load_error_strings(); // TODO
	SSL_library_init();
	ctx = SSL_CTX_new(SSLv23_client_method());
	SSL_CTX_set_default_verify_paths(ctx);
	SSL_CTX_set_cipher_list(ctx, "HIGH:!DSS:!aNULL@STRENGTH");
}

#if OPENSSL_VERSION_NUMBER < 0x10002000
#error please upgrade your openssl
#endif

class socketssl_openssl : public socket {
public:
	runloop* loop;
	autoptr<socket> sock;
	SSL* ssl;
	bool connected = false;
	bool is_readable = false;
	
	BIO* to_sock;
	BIO* from_sock;
	
	function<void()> cb_read;
	function<void()> cb_write;
	
	DECL_TIMER(timer, socketssl_openssl);
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	
#if OPENSSL_VERSION_NUMBER < 0x10002000 // < 1.0.2
	bool permissive;
	string domain;
#endif
	
	socketssl_openssl(socket* parent, cstring domain, runloop* loop, bool permissive)
	{
		this->loop = loop;
		
		sock = parent;
		ssl = SSL_new(ctx);
		
		to_sock = BIO_new(BIO_s_mem());
		from_sock = BIO_new(BIO_s_mem());
		
		SSL_set_tlsext_host_name(ssl, (const char*)domain.c_str());
		SSL_set_bio(ssl, from_sock, to_sock);
		
		//what kind of drugs are SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER,
		// what possible excuse could there be for that to not always be on
		//partial writes are perfectly fine with me, and seem mandatory on nonblocking BIOs, enable that too
		SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
		
		SSL_set_verify(ssl, permissive ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, NULL);
		
#if OPENSSL_VERSION_NUMBER >= 0x10002000 && OPENSSL_VERSION_NUMBER < 0x10100000 // >= 1.0.2, < 1.1.0
		if (!permissive)
		{
			X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
			X509_VERIFY_PARAM_set1_host(param, domain.c_str(), 0);
		}
#endif
		
#if OPENSSL_VERSION_NUMBER >= 0x10100000 // >= 1.1.0
//#error test, especially [gs]et0 vs [gs]et1
//#error also check whether set_tlsext_host_name is needed
		SSL_set1_host(ssl, domain.c_str());
#endif
		
		update();
	}
	
	/*private*/ int fixret(int val)
	{
		if (val > 0) return val;
		//if (val == 0) return -1;
		if (val <= 0)
		{
			int sslerror = SSL_get_error(ssl, val);
			if (sslerror == SSL_ERROR_WANT_READ || sslerror == SSL_ERROR_WANT_WRITE) return 0;
			else return -1;
		}
		return -1; // unreachable, just to shut up a warning
	}
	
	/*private*/ void do_connect()
	{
		int ret = fixret(SSL_connect(ssl));
		if (ret < 0) sock = NULL;
		if (ret > 0)
		{
			connected = true;
#if OPENSSL_VERSION_NUMBER < 0x10002000 // < 1.0.2
			if (!permissive)
			{
				X509* cert = SSL_get_peer_certificate(ssl);
				if (!validate_hostname(domain.c_str(), cert))
					sock = NULL;
				X509_free(cert);
			}
#endif
		}
	}
	
	/*private*/ void update()
	{
		if (sock && !connected)
		{
			do_connect();
			if (sock && !connected)
			{
				set_child_cb();
				return;
			}
		}
		
	again: ;
		bool again = false;
		if (!sock) is_readable = true;
		
		if (cb_read && is_readable) { again = true; RETURN_IF_CALLBACK_DESTRUCTS(cb_read( )); }
		if (cb_write              ) { again = true; RETURN_IF_CALLBACK_DESTRUCTS(cb_write()); }
		
		if (sock && again) goto again;
		
		set_child_cb();
	}
	
	/*private*/ void set_child_cb()
	{
		if (sock)
		{
			sock->callback(                  bind_this(&socketssl_openssl::on_readable),
			          BIO_pending(to_sock) ? bind_this(&socketssl_openssl::on_writable) : NULL);
		}
	}
	
	/*private*/ void process_send()
	{
		if (!sock) return;
		
		char* buf;
		long buflen = BIO_get_mem_data(to_sock, &buf);
//printf("SOCKWRITE(%li)=...\n",buflen);
		
		if (buflen > 0)
		{
			if (buflen > 4096) buflen = 4096;
			
			int bytes = sock->send(arrayview<uint8_t>((uint8_t*)buf, buflen));
//printf("(send %d crypt)",bytes);fflush(stdout);
//printf("SOCKWRITE(%li)=%i\n",buflen,bytes);
			if (bytes < 0) sock = NULL;
//else puts(tostringhex_dbg(arrayview<uint8_t>((uint8_t*)buf,bytes)));
			if (bytes > 0)
			{
				uint8_t discard[4096];
				int discarded = BIO_read(to_sock, discard, bytes);
				if (discarded != bytes) abort();
			}
			
			set_child_cb();
		}
		if (buflen < 0) sock = NULL;
	}
	
	/*private*/ void process_recv()
	{
		if (!sock) return;
		
		uint8_t buf[4096];
		int bytes = sock->recv(buf);
//VALGRIND_PRINTF_BACKTRACE
//printf("SOCKREAD(4096)=%i\n",bytes);
//printf("(recv %d crypt)",bytes);fflush(stdout);
		if (bytes < 0) sock = NULL;
//else puts(tostringhex_dbg(arrayview<uint8_t>(buf,bytes)));
		if (bytes > 0)
		{
			int bytes2 = BIO_write(from_sock, buf, bytes);
			if (bytes != bytes2) abort();
		}
		
		is_readable = true; // SSL_pending is unreliable
		
		set_child_cb();
	}
	
	int recv(arrayvieww<uint8_t> data) override
	{
		int ret = fixret(SSL_read(ssl, data.ptr(), data.size()));
//printf("USERREAD(%lu)=%i\n",data.size(),ret);
		if (ret == 0)
		{
			if (!sock) ret = -1;
			else is_readable = false;
		}
		if (ret < 0)
		{
			sock = NULL;
			is_readable = false;
		}
		set_child_cb();
		
		return ret;
	}
	
	int send(arrayview<uint8_t> data) override
	{
		if (!sock) return -1;
		
		int ret = fixret(SSL_write(ssl, data.ptr(), data.size()));
//printf("USERWRITE(%lu)=%i\n",data.size(),ret);
		process_send();
		if (ret < 0)
		{
			sock = NULL;
			return ret;
		}
		if (!sock) return -1;
		return ret;
	}
	
	/*private*/ void on_readable() { process_recv(); update(); }
	/*private*/ void on_writable() { process_send(); update(); }
	void callback(function<void()> cb_read, function<void()> cb_write) override
	{
		this->cb_read = cb_read;
		this->cb_write = cb_write;
		if (cb_write) timer.set_idle(bind_this(&socketssl_openssl::update));
	}
	
	~socketssl_openssl()
	{
		if (sock)
		{
			SSL_shutdown(ssl);
			process_send();
		}
		
		//the BIOs should not be freed, the SSL* grabs the only reference
		SSL_free(ssl);
	}
};

socket* socket::wrap_ssl_raw_openssl(socket* inner, cstring domain, runloop* loop)
{
	initialize();
	if (!ctx) return NULL;
	if (!inner) return NULL;
	return new socketssl_openssl(inner, domain, loop, false);
}

socket* socket::wrap_ssl_raw_openssl_noverify(socket* inner, runloop* loop)
{
	initialize();
	if (!ctx) return NULL;
	if (!inner) return NULL;
	return new socketssl_openssl(inner, "", loop, true);
}

#include "../test.h"
#ifdef ARLIB_TEST
#include "../os.h"
//this is more to initialize this thing before the actual ssl tests than a real test
//most of them are in a runloop, but initialization takes longer (9-33ms) than the runloop watchdog (3ms)
//this is also why it provides 'tcp' rather than 'ssl';
// if it provides 'ssl', it could run after the other SSL tests so they fail the watchdog
test("OpenSSL init", "array,base64", "tcp")
{
	test_skip("kinda slow");
	
	uint64_t begin_us = time_us_ne();
	initialize();
	uint64_t end_us = time_us_ne();
	if (!RUNNING_ON_VALGRIND)
	{
		assert_lt(end_us-begin_us, 5000); // takes about 1ms - no clue why Bear is so much slower
	}
}
#endif
#endif
