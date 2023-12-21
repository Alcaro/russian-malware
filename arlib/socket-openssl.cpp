#ifdef ARLIB_SSL_OPENSSL
#include "socket.h"
#include "thread.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

namespace {

// putting those as statics in the class gives weird linker errors
static SSL_CTX* g_ctx;
static BIO_METHOD* g_bio_meth;

class socket2_openssl : public socket2 {
public:
	static void initialize()
	{
		g_ctx = SSL_CTX_new(TLS_client_method());
		SSL_CTX_set_default_verify_paths(g_ctx); // don't know why this one isn't on by default
		
		g_bio_meth = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "arlib");
		BIO_meth_set_write_ex(g_bio_meth, [](BIO* bio, const char * data, size_t dlen, size_t* written) -> int {
			socket2_openssl* ossl = (socket2_openssl*)BIO_get_data(bio);
			if (!ossl->sock)
				return 0;
			ssize_t n = ossl->sock->send_sync(bytesr((uint8_t*)data, dlen));
			BIO_clear_retry_flags(bio);
			if (n == 0)
				BIO_set_retry_write(bio);
			if (n < 0)
				ossl->sock = nullptr;
			if (n <= 0)
				return 0;
			*written = n;
			return 1;
		});
		BIO_meth_set_read_ex(g_bio_meth, [](BIO* bio, char * data, size_t dlen, size_t* readbytes) -> int {
			socket2_openssl* ossl = (socket2_openssl*)BIO_get_data(bio);
			if (!ossl->sock)
				return 0;
			ssize_t n = ossl->sock->recv_sync(bytesw((uint8_t*)data, dlen));
			BIO_clear_retry_flags(bio);
			if (n == 0)
				BIO_set_retry_read(bio);
			if (n < 0)
				ossl->sock = nullptr;
			if (n <= 0)
				return 0;
			*readbytes = n;
			return 1;
		});
		BIO_meth_set_ctrl(g_bio_meth, [](BIO* bio, int cmd, long larg, void* parg) -> long
			{
				// the default ctrl handler returns -2 for everything
				// despite https://www.openssl.org/docs/man3.0/man3/BIO_ctrl.html saying unfamiliar ioctls should return 0
				// (and FLUSH needs to return success for some reason)
				if (cmd == BIO_CTRL_FLUSH)
					return 1;
				return 0;
			});
	}
	
	autoptr<socket2> sock;
	SSL* ssl;
	enum { block_none, block_send, block_recv };
	uint8_t send_block = block_none; // send_block = block_send means that last time SSL_write() was called, it failed with SSL_WANT_WRITE
	uint8_t recv_block = block_none; // if the operation succeeded or wasn't attempted, it's block_none and considered ready immediately
	
	socket2_openssl(autoptr<socket2> sock_, cstring domain) : sock(std::move(sock_))
	{
		ssl = SSL_new(g_ctx);
		int fd = sock->get_fd();
		if (fd >= 0 && false)
		{
			// do not enable until https://github.com/openssl/openssl/issues/16399 is fixed
			//  (gtk and game-x11 set SIGPIPE to SIG_IGN, but making the openssl handler conditional on gui stuff is just wrong,
			//   and gdb breaks on SIGPIPE anyways)
			SSL_set_fd(ssl, fd); // does not claim ownership of the fd
			// todo: test (trying to enable ktls returns ENOENT on every machine I have)
			//SSL_set_options(ssl, SSL_OP_ENABLE_KTLS);
		}
		else
		{
			BIO* bio = BIO_new(g_bio_meth);
			BIO_set_data(bio, (socket2_openssl*)this);
			SSL_set_bio(ssl, bio, bio);
		}
		SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
		
		SSL_set_verify(ssl, SSL_VERIFY_PEER, nullptr); // don't know why this one is off by default
		SSL_set1_host(ssl, domain.c_str().c_str()); // needed for hostname verification
		SSL_set_tlsext_host_name(ssl, domain.c_str().c_str()); // needed for SNI; don't know why they're different functions
	}
	
	ssize_t process_ret(int success, size_t& amount, uint8_t& block)
	{
		if (success > 0)
		{
			block = block_none;
			return amount;
		}
		int err = SSL_get_error(ssl, success);
		if (err == SSL_ERROR_WANT_READ) { block = block_recv; return 0; }
		else if (err == SSL_ERROR_WANT_WRITE) { block = block_send; return 0; }
		else { sock = nullptr; return -1; }
	}
	
	ssize_t recv_sync(bytesw by) override
	{
		if (!sock)
			return -1;
		size_t ret;
		return process_ret(SSL_read_ex(ssl, by.ptr(), by.size(), &ret), ret, recv_block);
	}
	ssize_t send_sync(bytesr by) override
	{
		if (!sock)
			return -1;
		size_t ret;
		return process_ret(SSL_write_ex(ssl, by.ptr(), by.size(), &ret), ret, send_block);
	}
	
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	
	producer<void> sendapp_p;
	producer<void> recvapp_p;
	
	waiter<void> wait_send = make_waiter<&socket2_openssl::wait_send, &socket2_openssl::wait_send_done>();
	waiter<void> wait_recv = make_waiter<&socket2_openssl::wait_recv, &socket2_openssl::wait_recv_done>();
	
	void wait_send_done() { wait_done(block_send); }
	void wait_recv_done() { wait_done(block_recv); }
	
	void wait_done(uint8_t op)
	{
		if (send_block == op)
			send_block = block_none;
		if (recv_block == op)
			recv_block = block_none;
		if (send_block == block_none && sendapp_p.has_waiter())
			RETURN_IF_CALLBACK_DESTRUCTS(sendapp_p.complete());
		if (recv_block == block_none && recvapp_p.has_waiter())
			recvapp_p.complete();
	}
	
	async<void> prepare_wait(producer<void>& prod, uint8_t op)
	{
		if (op == block_none || !sock)
			return prod.complete_sync();
		if (op == block_send && !wait_send.is_waiting())
			sock->can_send().then(&wait_send);
		if (op == block_recv && !wait_recv.is_waiting())
			sock->can_recv().then(&wait_recv);
		return &prod;
	}
	async<void> can_recv() override { return prepare_wait(recvapp_p, recv_block); }
	async<void> can_send() override { return prepare_wait(sendapp_p, send_block); }
	
	~socket2_openssl()
	{
		if (sock)
		{
			// It is acceptable for an application to only send its shutdown alert and then close
			// the underlying connection without waiting for the peer's response.
			// ~ https://www.openssl.org/docs/man3.0/man3/SSL_shutdown.html
			// if the handshake is cancelled, it will fail with SSL_R_SHUTDOWN_WHILE_IN_INIT; not worth caring about
			// OpenSSL errors are in a thread-local ring buffer; leaking them will eventually be collected
			// (could confuse other components in this process that read those errors, but too rare, no point caring)
			SSL_shutdown(ssl);
		}
		
		//the BIO should not be freed, the SSL* grabs the only reference
		SSL_free(ssl);
	}
};

#ifdef ARLIB_TEST
static bool initialized = false;
static void try_initialize()
{
	if (!initialized)
		socket2_openssl::initialize();
	initialized = true;
}
// this isn't a real test; it's to initialize this thing before the actual ssl tests, so latency isn't misattributed too badly
// this is also why it provides 'tcp' rather than 'ssl'; if it provides 'ssl', it could run after the other SSL tests
// I could put it on the oninit, but that'd take 1.5 seconds under Valgrind even if nothing tested uses sockets
test("OpenSSL init", "", "tcp")
{
	test_skip("kinda slow");
	try_initialize();
}
#else
oninit() { socket2_openssl::initialize(); }
#endif
}

async<autoptr<socket2>> socket2::wrap_ssl_openssl(autoptr<socket2> inner, cstring domain)
{
#ifdef ARLIB_TEST
	try_initialize();
#endif
	if (!inner) co_return nullptr;
	socket2_openssl* ossl = new socket2_openssl(std::move(inner), domain);
	autoptr<socket2> ret = ossl;
	
	while (true)
	{
		size_t dummy = 0;
		int ret = SSL_connect(ossl->ssl);
		if (ret > 0)
			break;

		ossl->process_ret(ret, dummy, ossl->recv_block);
		if (!ossl->sock)
			co_return nullptr;
		co_await ossl->can_recv();
	}
	
	co_return ret;
}
#endif
