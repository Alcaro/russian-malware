#include "socket.h"

#ifdef ARLIB_SSL_BEARSSL
#include "../file.h"
#include "../stringconv.h"
#include "../thread.h"
#include "../base64.h"
//Possible BearSSL improvements (not all of it is worth the effort; paren is last tested version, some may have been fixed since then):
//- (0.6) serialization that I didn't have to write myself
//- (0.3) official sample code demonstrating how to load /etc/ssl/certs/ca-certificates.crt
//    preferably putting most of it in BearSSL itself, but seems hard to implement without malloc
//- (0.3) more bool and int8_t, less int and char
//    it's fine if it's typedef br_bool=int rather than real bool, if needed to prevent compiler shenanigans
//    <https://bearssl.org/constanttime.html#compiler-woes>, but just plain int is suboptimal
//    (and most, if not all, booleans in the BearSSL API are constant and non-secret - no attacker cares if your iobuf is bidirectional)
//- (0.3) src/ec/ec_p256_m15.c:992 and :1001 don't seem to need to be u32, u16 works just as well
//    (tools/client.c:234 and test/test_x509.c:504 too, but those parts aren't size sensitive)
//- (0.3) fix typoed NULLs at src/hash/ghash_pclmul.c:241, src/hash/ghash_pclmul.c:250, tools/names.c:834
//    (not sure if that's the only ones)
//- (0.4) tools/client.c: typo minium: "ERROR: duplicate minium ClientHello length"

#include "../deps/bearssl-0.6/inc/bearssl.h"

extern "C" {
//see bear-ser.c for docs
typedef struct br_frozen_ssl_client_context_ {
	br_ssl_client_context cc;
	br_x509_minimal_context xc;
} br_frozen_ssl_client_context;
void br_ssl_client_freeze(br_frozen_ssl_client_context* fr, const br_ssl_client_context* cc, const br_x509_minimal_context* xc);
void br_ssl_client_unfreeze(br_frozen_ssl_client_context* fr, br_ssl_client_context* cc, br_x509_minimal_context* xc);
}

namespace {
// based on bearssl-0.3/tools/certs.c and files.c, heavily rewritten

// use a static arena for allocations, to avoid memory leaks on hybrid DLL, and for performance
// the anchor structs are allocated backwards, while the actual keys they point to go forwards, so they meet in the middle
static uint8_t alloc_arena[256*1024]; // on a normal Linux, 64KB of this is used (ca-certificates.crt is 200KB); 17KB on Windows
static size_t alloc_blobs = 0; // reserving more than I need is harmless, the OS won't allocate unused pages
static br_x509_trust_anchor * const alloc_certs_initial = (br_x509_trust_anchor*)(alloc_arena + sizeof(alloc_arena));
static br_x509_trust_anchor * alloc_certs = alloc_certs_initial;

static uint8_t* blobdup(const void * src, size_t len)
{
	uint8_t * ret = alloc_arena+alloc_blobs;
	memcpy(ret, src, len);
	alloc_blobs += len;
	return ret;
}
static void vdn_append(void* dest_ctx, const void * src, size_t len)
{
	blobdup(src, len);
}
static bool append_cert_x509(arrayview<uint8_t> xc)
{
	alloc_certs--;
	new(alloc_certs) br_x509_trust_anchor;
	br_x509_trust_anchor& ta = *alloc_certs;
	
	br_x509_decoder_context dc;
	
	size_t vdn_start = alloc_blobs;
	br_x509_decoder_init(&dc, vdn_append, NULL);
	br_x509_decoder_push(&dc, xc.ptr(), xc.size());
	br_x509_pkey* pk = br_x509_decoder_get_pkey(&dc);
	if (pk == NULL || !br_x509_decoder_isCA(&dc)) goto fail;
	
	ta.dn.len = alloc_blobs - vdn_start;
	ta.dn.data = alloc_arena + vdn_start;
	ta.flags = BR_X509_TA_CA;
	
	switch (pk->key_type)
	{
	case BR_KEYTYPE_RSA:
		ta.pkey.key_type = BR_KEYTYPE_RSA;
		ta.pkey.key.rsa.nlen = pk->key.rsa.nlen;
		ta.pkey.key.rsa.n = blobdup(pk->key.rsa.n, pk->key.rsa.nlen);
		ta.pkey.key.rsa.elen = pk->key.rsa.elen;
		ta.pkey.key.rsa.e = blobdup(pk->key.rsa.e, pk->key.rsa.elen);
		break;
	case BR_KEYTYPE_EC:
		ta.pkey.key_type = BR_KEYTYPE_EC;
		ta.pkey.key.ec.curve = pk->key.ec.curve;
		ta.pkey.key.ec.qlen = pk->key.ec.qlen;
		ta.pkey.key.ec.q = blobdup(pk->key.ec.q, pk->key.ec.qlen);
		break;
	default:
		goto fail;
	}
	return true;
	
fail:
	ta.~br_x509_trust_anchor();
	alloc_certs++;
	alloc_blobs = vdn_start;
	return false;
}

//unused on Windows, its cert store gives me x509s directly
MAYBE_UNUSED static void append_certs_pem_x509(cstring certs_pem)
{
	array<cstring> certs = certs_pem.csplit("-----BEGIN CERTIFICATE-----");
	array<uint8_t> buf;
	for (cstring cert : certs)
	{
		size_t certend = cert.indexof("-----END CERTIFICATE-----");
		if (certend == (size_t)-1) continue;
		
		size_t buflen = base64_dec_len(certend);
		buf.reserve(buflen);
		size_t actuallen = base64_dec_raw(buf, cert.substr(0, certend));
		append_cert_x509(buf.slice(0, actuallen));
	}
}

#ifdef _WIN32
// crypt32.dll seems to be the only way to access the Windows cert store
#include <wincrypt.h>
#endif

RUN_ONCE_FN(initialize)
{
#ifndef _WIN32
	append_certs_pem_x509(file::readallt("/etc/ssl/certs/ca-certificates.crt"));
#else
	HCERTSTORE store = CertOpenSystemStore((HCRYPTPROV)NULL, "ROOT");
	if (!store) return;
	
	const CERT_CONTEXT * ctx = NULL;
	while ((ctx = CertEnumCertificatesInStore(store, ctx)))
	{
		append_cert_x509(arrayview<uint8_t>(ctx->pbCertEncoded, ctx->cbCertEncoded));
	}
	CertCloseStore(store, 0);
#endif
//unsigned used_ta = (alloc_certs_initial-alloc_certs)*sizeof(br_x509_trust_anchor);
//unsigned used_blob = alloc_blobs;
//printf("%u+%u= %u / %u\n", used_blob,used_ta,used_blob+used_ta,(unsigned)sizeof(alloc_arena));
}


/*
struct x509_noanchor_context {
	const br_x509_class * vtable;
	const br_x509_class ** inner;
};
static void xwc_start_chain(const br_x509_class ** ctx, const char * server_name)
{
	x509_noanchor_context* xwc = (x509_noanchor_context*)ctx;
	(*xwc->inner)->start_chain(xwc->inner, server_name);
}
static void xwc_start_cert(const br_x509_class ** ctx, uint32_t length)
{
	x509_noanchor_context* xwc = (x509_noanchor_context*)ctx;
	(*xwc->inner)->start_cert(xwc->inner, length);
}
static void xwc_append(const br_x509_class ** ctx, const unsigned char * buf, size_t len)
{
	x509_noanchor_context* xwc = (x509_noanchor_context*)ctx;
	(*xwc->inner)->append(xwc->inner, buf, len);
}
static void xwc_end_cert(const br_x509_class ** ctx)
{
	x509_noanchor_context* xwc = (x509_noanchor_context*)ctx;
	(*xwc->inner)->end_cert(xwc->inner);
}
static unsigned xwc_end_chain(const br_x509_class ** ctx)
{
	x509_noanchor_context* xwc = (x509_noanchor_context*)ctx;
	unsigned r = (*xwc->inner)->end_chain(xwc->inner);
	if (r == BR_ERR_X509_NOT_TRUSTED) return 0;
	//if (r == BR_ERR_X509_BAD_SERVER_NAME) return 0; // doesn't work
	return r;
}
static const br_x509_pkey * xwc_get_pkey(const br_x509_class * const * ctx, unsigned * usages)
{
	x509_noanchor_context* xwc = (x509_noanchor_context*)ctx;
	return (*xwc->inner)->get_pkey(xwc->inner, usages);
}
static const br_x509_class x509_noanchor_vtable = {
	sizeof(x509_noanchor_context),
	xwc_start_chain,
	xwc_start_cert,
	xwc_append,
	xwc_end_cert,
	xwc_end_chain,
	xwc_get_pkey
};
*/


class socketssl_bearssl : public socket {
public:
	autoptr<socket> sock;
	
	struct bearstate {
		br_ssl_client_context sc;
		br_x509_minimal_context xc;
		//x509_noanchor_context xwc = { NULL, NULL }; // initialized for serialization, otherwise not needed
		uint8_t iobuf[BR_SSL_BUFSIZE_BIDI];
	} s;
	
	function<void()> cb_read;
	function<void()> cb_write;
	
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	bool errored = false;
	
	socketssl_bearssl(socket* inner, cstring domain, runloop* loop, bool permissive)
	{
		this->sock = inner;
		
		br_ssl_client_init_full(&s.sc, &s.xc, alloc_certs, alloc_certs_initial-alloc_certs);
		//if (permissive)
		//{
		//	s.xwc.vtable = &x509_noanchor_vtable;
		//	s.xwc.inner = &s.xc.vtable;
		//	br_ssl_engine_set_x509(&s.sc.eng, &s.xwc.vtable);
		//}
		br_ssl_engine_set_buffer(&s.sc.eng, s.iobuf, sizeof(s.iobuf), true);
		br_ssl_client_reset(&s.sc, domain.c_str(), false);
		
		set_child_cb();
	}
	
	/*private*/ void set_child_cb()
	{
		if (sock)
		{
			unsigned state = br_ssl_engine_current_state(&s.sc.eng);
			sock->callback((state & BR_SSL_RECVREC) ? bind_this(&socketssl_bearssl::on_readable) : NULL,
			               (state & BR_SSL_SENDREC) ? bind_this(&socketssl_bearssl::on_writable) : NULL);
		}
	}
	
	/*private*/ void process_send()
	{
		if (!sock) return;
		
		size_t buflen;
		uint8_t* buf = br_ssl_engine_sendrec_buf(&s.sc.eng, &buflen);
		if (buflen)
		{
			int bytes = sock->send(arrayview<uint8_t>(buf, buflen));
			if (bytes < 0) sock = NULL;
			if (bytes > 0) br_ssl_engine_sendrec_ack(&s.sc.eng, bytes);
			
			set_child_cb();
		}
	}
	
	/*private*/ void process_recv()
	{
		if (!sock) return;
		
		size_t buflen;
		uint8_t* buf = br_ssl_engine_recvrec_buf(&s.sc.eng, &buflen);
		if (buflen)
		{
			int bytes = sock->recv(arrayvieww<uint8_t>(buf, buflen));
			if (bytes < 0) sock = NULL;
			if (bytes > 0) br_ssl_engine_recvrec_ack(&s.sc.eng, bytes);
			
			set_child_cb();
		}
	}
	
	int recv(arrayvieww<uint8_t> data)
	{
		if (!sock) { errored = true; return -1; }
		
		size_t buflen;
		uint8_t* buf = br_ssl_engine_recvapp_buf(&s.sc.eng, &buflen);
		if (buflen > data.size()) buflen = data.size();
		if (buflen == 0) return 0;
		
		memcpy(data.ptr(), buf, buflen);
		br_ssl_engine_recvapp_ack(&s.sc.eng, buflen);
		
		set_child_cb();
		return buflen;
	}
	
	int send(arrayview<uint8_t> data)
	{
		if (!sock) { errored = true; return -1; }
		
		size_t buflen;
		uint8_t* buf = br_ssl_engine_sendapp_buf(&s.sc.eng, &buflen);
		if (buflen > data.size()) buflen = data.size();
		if (buflen == 0) return 0;
		
		memcpy(buf, data.ptr(), buflen);
		br_ssl_engine_sendapp_ack(&s.sc.eng, buflen);
		br_ssl_engine_flush(&s.sc.eng, false);
		
		set_child_cb();
		return buflen;
	}
	
	/*private*/ void do_cbs()
	{
		//this function is known to be called only directly by the runloop, and as such, can't recurse
		//this is likely a use-after-free if the callbacks throw, but runloop itself doesn't support exceptions, so who cares
		unsigned state = br_ssl_engine_current_state(&s.sc.eng);
	again:
		if (cb_read  && (state&(BR_SSL_RECVAPP|BR_SSL_CLOSED))) RETURN_IF_CALLBACK_DESTRUCTS(cb_read( ));
		if (cb_write && (state&(BR_SSL_SENDAPP|BR_SSL_CLOSED))) RETURN_IF_CALLBACK_DESTRUCTS(cb_write());
		if (state & BR_SSL_CLOSED) sock = NULL;
		
		state = br_ssl_engine_current_state(&s.sc.eng);
		if (!errored)
		{
			if (cb_read  && (state&(BR_SSL_RECVAPP|BR_SSL_CLOSED))) goto again;
			if (cb_write && (state&(BR_SSL_SENDAPP|BR_SSL_CLOSED))) goto again;
		}
		
		set_child_cb();
	}
	
	/*private*/ void on_readable() { process_recv(); do_cbs(); }
	/*private*/ void on_writable() { process_send(); do_cbs(); }
	void callback(function<void()> cb_read, function<void()> cb_write)
	{
		this->cb_read = cb_read;
		this->cb_write = cb_write;
	}
	
	~socketssl_bearssl()
	{
		if (!sock) return;
		
		//gracefully tear this down, not really useful but not harmful either
		br_ssl_engine_close(&s.sc.eng);
		process_send();
		//but don't worry too much about ensuring the remote peer gets our closure notification
	}
	
	
	
	//struct state_fr {
	//	br_frozen_ssl_client_context sc;
	//	bool permissive;
	//	byte iobuf[BR_SSL_BUFSIZE_BIDI];
	//};
	//
	//array<uint8_t> serialize(int* fd)
	//{
	//	array<uint8_t> bytes;
	//	bytes.resize(sizeof(state_fr));
	//	state_fr& out = *(state_fr*)bytes.ptr();
	//	
	//	br_ssl_client_freeze(&out.sc, &s.sc, &s.xc);
	//	out.permissive = (s.xwc.vtable != NULL);
	//	memcpy(out.iobuf, s.iobuf, sizeof(out.iobuf));
	//	
	//	*fd = decompose(&this->sock);
	//	
	//	delete this;
	//	return bytes;
	//}
	//
	////deserializing constructor
	//socketssl_bearssl(int fd, arrayview<uint8_t> data) : socket(fd)
	//{
	//	this->sock = socket::create_from_fd(fd);
	//	const state_fr& in = *(state_fr*)data.ptr();
	//	
	//	state ref;
	//	
	//	br_ssl_client_init_full(&s.sc, &s.xc, certs.ptr(), certs.size());
	//	if (in.permissive)
	//	{
	//		s.xwc.vtable = &x509_noanchor_vtable;
	//		s.xwc.inner = &s.xc.vtable;
	//		br_ssl_engine_set_x509(&s.sc.eng, &s.xwc.vtable);
	//	}
	//	else s.xwc.vtable = NULL;
	//	br_ssl_engine_set_buffer(&s.sc.eng, s.iobuf, sizeof(s.iobuf), true);
	//	br_ssl_client_reset(&s.sc, NULL, false);
	//	
	//	br_frozen_ssl_client_context fr_sc;
	//	memcpy(&fr_sc, &in.sc, sizeof(fr_sc));
	//	br_ssl_client_unfreeze(&fr_sc, &s.sc, &s.xc);
	//	memcpy(s.iobuf, in.iobuf, sizeof(s.iobuf));
	//}
};
}

socket* socket::wrap_ssl_raw_bearssl(socket* inner, cstring domain, runloop* loop)
{
	initialize();
	if (alloc_certs_initial == alloc_certs) return NULL;
	if (!inner) return NULL;
	return new socketssl_bearssl(inner, domain, loop, false);
}

//array<uint8_t> socketssl::serialize(int* fd)
//{
//	return ((socketssl_bearssl*)this)->serialize(fd);
//}
//socketssl* socketssl::deserialize(int fd, arrayview<uint8_t> data)
//{
//	if (sizeof(socketssl_bearssl::state_fr) != data.size()) return NULL;
//	initialize();
//	if (!certs) return NULL;
//	
//	return new socketssl_bearssl(fd, data);
//}

#include "../test.h"
#ifdef ARLIB_TEST
#include "../os.h"
//this is more to initialize this thing before the actual ssl tests than a real test
//most of the tests are in a runloop, but initialization takes longer (9-33ms) than the runloop watchdog (3ms)
//this is also why it provides 'tcp' rather than 'ssl';
// if it provides 'ssl', it could be after other SSL tests and fail watchdog
test("BearSSL init", "array,base64,file", "tcp")
{
	test_skip("kinda slow");
	
	uint64_t begin_us = time_us_ne();
	initialize();
	uint64_t end_us = time_us_ne();
	if (!RUNNING_ON_VALGRIND)
	{
		assert_lt(end_us-begin_us, 50000); // randomly takes either 10ms or 32ms, probably depending on cpu power saving policy
	}
}
#endif
#endif
