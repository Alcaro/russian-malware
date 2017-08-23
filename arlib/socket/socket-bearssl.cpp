#include "socket.h"

#ifdef ARLIB_SSL_BEARSSL
#include "../file.h"
#include "../stringconv.h"
#include "../thread.h"
//Possible BearSSL improvements (not all of it is worth the effort; some may have been fixed since the version I tested):
//- (0.4) serialization that I didn't have to write myself
//- (0.3) extern "C" in header
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

extern "C" {
#include "../deps/bearssl-0.4/inc/bearssl.h"

//see bear-ser.c for docs
typedef struct br_frozen_ssl_client_context_ {
	br_ssl_client_context cc;
	br_x509_minimal_context xc;
} br_frozen_ssl_client_context;
void br_ssl_client_freeze(br_frozen_ssl_client_context* fr, const br_ssl_client_context* cc, const br_x509_minimal_context* xc);
void br_ssl_client_unfreeze(br_frozen_ssl_client_context* fr, br_ssl_client_context* cc, br_x509_minimal_context* xc);
}




//most of this is copied from bearssl-0.3/tools/certs.c and files.c, somewhat rewritten

//these have destructors; they shouldn't
//but GCC bug 19661 says I can't get them out.
static array<array<byte>> certs_blobs;
static array<br_x509_trust_anchor> certs;

static void bytes_append(void* dest_ctx, const void * src, size_t len)
{
	(*(array<byte>*)dest_ctx) += arrayview<byte>((byte*)src, len);
}
static byte* blobdup(arrayview<byte> blob)
{
	return certs_blobs.append(blob).ptr();
}
static bool append_cert_x509(arrayview<byte> xc)
{
	br_x509_trust_anchor& ta = certs.append();
	
	br_x509_decoder_context dc;
	
	array<byte>& vdn = certs_blobs.append();
	br_x509_decoder_init(&dc, bytes_append, &vdn);
	br_x509_decoder_push(&dc, xc.ptr(), xc.size());
	br_x509_pkey* pk = br_x509_decoder_get_pkey(&dc);
	if (pk == NULL) return false;
	
	ta.dn.data = vdn.ptr();
	ta.dn.len = vdn.size();
	ta.flags = (br_x509_decoder_isCA(&dc) ? BR_X509_TA_CA : 0);
	
	switch (pk->key_type) {
	case BR_KEYTYPE_RSA:
		ta.pkey.key_type = BR_KEYTYPE_RSA;
		ta.pkey.key.rsa.n = blobdup(arrayview<byte>(pk->key.rsa.n, pk->key.rsa.nlen));
		ta.pkey.key.rsa.nlen = pk->key.rsa.nlen;
		ta.pkey.key.rsa.e = blobdup(arrayview<byte>(pk->key.rsa.e, pk->key.rsa.elen));
		ta.pkey.key.rsa.elen = pk->key.rsa.elen;
		break;
	case BR_KEYTYPE_EC:
		ta.pkey.key_type = BR_KEYTYPE_EC;
		ta.pkey.key.ec.curve = pk->key.ec.curve;
		ta.pkey.key.ec.q = blobdup(arrayview<byte>(pk->key.ec.q, pk->key.ec.qlen));
		ta.pkey.key.ec.qlen = pk->key.ec.qlen;
		break;
	default:
		return false;
	}
	return true;
}

//unused on Windows, its cert store gives me x509s directly
MAYBE_UNUSED static void append_certs_pem_x509(arrayview<byte> certs_pem)
{
	br_pem_decoder_context pc;
	br_pem_decoder_init(&pc);
	array<byte> cert_this;
	
	while (certs_pem)
	{
		size_t tlen = br_pem_decoder_push(&pc, certs_pem.ptr(), certs_pem.size());
		certs_pem = certs_pem.skip(tlen);
		
		//what a strange API, does it really need both event streaming and a callback?
		switch (br_pem_decoder_event(&pc)) {
		case BR_PEM_BEGIN_OBJ:
			cert_this.reset();
			if (!strcmp(br_pem_decoder_name(&pc), "CERTIFICATE"))
				br_pem_decoder_setdest(&pc, bytes_append, &cert_this);
			else
				br_pem_decoder_setdest(&pc, NULL, NULL);
			break;
		
		case BR_PEM_END_OBJ:
			if (cert_this) append_cert_x509(cert_this);
			break;
		
		case BR_PEM_ERROR:
			certs.reset();
			return;
		}
	}
}

#ifdef _WIN32
//seems to be no way to access the Windows cert store without crypt32.dll
//but that's fine, cert management is boring, the useful parts remain Bear
#include <wincrypt.h>
#endif

RUN_ONCE_FN(initialize)
{
#ifndef _WIN32
	append_certs_pem_x509(file::read("/etc/ssl/certs/ca-certificates.crt"));
#else
	HCERTSTORE store = CertOpenSystemStore((HCRYPTPROV)NULL, "ROOT");
	if (!store) return;
	
	const CERT_CONTEXT * ctx = NULL;
	while ((ctx = CertEnumCertificatesInStore(store, ctx)))
	{
		append_cert_x509(arrayview<byte>(ctx->pbCertEncoded, ctx->cbCertEncoded));
	}
	CertFreeCertificateContext(ctx);
	CertCloseStore(store, 0);
#endif
}


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


class socketssl_impl : public socketssl {
public:
	autoptr<socket> sock;
	
	struct state {
		br_ssl_client_context sc;
		br_x509_minimal_context xc;
		x509_noanchor_context xwc = { NULL, NULL };
		byte iobuf[BR_SSL_BUFSIZE_BIDI];
	} s;
	
	socketssl_impl(socket* parent, cstring domain, bool permissive) : socketssl(parent->get_fd())
	{
		this->sock = parent;
		
		br_ssl_client_init_full(&s.sc, &s.xc, certs.ptr(), certs.size());
		if (permissive)
		{
			s.xwc.vtable = &x509_noanchor_vtable;
			s.xwc.inner = &s.xc.vtable;
			br_ssl_engine_set_x509(&s.sc.eng, &s.xwc.vtable);
		}
		else s.xwc.vtable = NULL;
		br_ssl_engine_set_buffer(&s.sc.eng, s.iobuf, sizeof(s.iobuf), true);
		br_ssl_client_reset(&s.sc, domain.c_str(), false);
	}
	
	//returns whether anything happened
	/*private*/ bool process_send(bool block)
	{
		if (!sock) return false;
		
		size_t buflen;
		byte* buf = br_ssl_engine_sendrec_buf(&s.sc.eng, &buflen);
		if (buflen)
		{
			int bytes = sock->sendp(arrayview<byte>(buf, buflen), block);
			if (bytes < 0)
			{
				sock = NULL;
				return true;
			}
			if (bytes > 0)
			{
				br_ssl_engine_sendrec_ack(&s.sc.eng, bytes);
				return true;
			}
		}
		return false;
	}
	
	//returns whether anything happened
	/*private*/ bool process_recv(bool block)
	{
		if (!sock) return false;
		
		size_t buflen;
		byte* buf = br_ssl_engine_recvrec_buf(&s.sc.eng, &buflen);
		if (buflen)
		{
			int bytes = sock->recv(arrayvieww<byte>(buf, buflen), block);
			if (bytes < 0)
			{
				sock = NULL;
				return true;
			}
			if (bytes > 0)
			{
				br_ssl_engine_recvrec_ack(&s.sc.eng, bytes);
				return true;
			}
		}
		return false;
	}
	
	/*private*/ void process(bool block)
	{
		if (br_ssl_engine_current_state(&s.sc.eng) == BR_SSL_CLOSED)
		{
			//int err = br_ssl_engine_last_error(&s.sc.eng);
			sock = NULL;
		}
		if (process_send(false)) block = false;
		if (process_recv(block)) block = false;
		if (process_send(block)) block = false;
	}
	
	bool establish()
	{
		//https://bearssl.org/apidoc/bearssl__ssl_8h.html#ad58834389d963630e201c4d0f2fe4be6
		//  br_ssl_engine_get_session_parameters
		//"The initial handshake is completed when the context first allows application data to be injected."
		size_t dummy;
		while (!br_ssl_engine_sendapp_buf(&s.sc.eng, &dummy))
		{
			process(true);
			if (!sock) return false;
			if (br_ssl_engine_last_error(&s.sc.eng) != BR_ERR_OK) return false;
			if (br_ssl_engine_current_state(&s.sc.eng) == BR_SSL_CLOSED) return false;
		}
		return true;
	}
	
	int recv(arrayvieww<byte> data, bool block)
	{
		process(false);
		
	again:
		if (!sock) return -1;
		
		size_t buflen;
		byte* buf = br_ssl_engine_recvapp_buf(&s.sc.eng, &buflen);
		if (buflen > data.size()) buflen = data.size();
		if (buflen == 0)
		{
			if (block)
			{
				process(true);
				goto again;
			}
			else return 0;
		}
		
		memcpy(data.ptr(), buf, buflen);
		br_ssl_engine_recvapp_ack(&s.sc.eng, buflen);
		return buflen;
	}
	
	int sendp(arrayview<byte> data, bool block)
	{
		process(false);
		
	again:
		if (!sock) return -1;
		
		size_t buflen;
		byte* buf = br_ssl_engine_sendapp_buf(&s.sc.eng, &buflen);
		if (buflen > data.size()) buflen = data.size();
		if (buflen == 0)
		{
			if (block)
			{
				process(true);
				goto again;
			}
			else return 0;
		}
		
		memcpy(buf, data.ptr(), buflen);
		br_ssl_engine_sendapp_ack(&s.sc.eng, buflen);
		br_ssl_engine_flush(&s.sc.eng, false);
		return buflen;
	}
	
	bool active(bool want_recv, bool want_send)
	{
		int bearstate = br_ssl_engine_current_state(&s.sc.eng);
		if (want_recv && (bearstate&BR_SSL_RECVAPP)) return true;
		if (want_send && (bearstate&BR_SSL_SENDAPP)) return true;
		return false;
	}
	
	~socketssl_impl()
	{
		if (!sock) return;
		
		//gracefully tear this down, not really useful but not harmful either
		br_ssl_engine_close(&s.sc.eng);
		br_ssl_engine_flush(&s.sc.eng, 0);
		process(false);
		//but don't worry too much about ensuring the remote gets our closure notification, it's not really useful
	}
	
	
	
	struct state_fr {
		br_frozen_ssl_client_context sc;
		bool permissive;
		byte iobuf[BR_SSL_BUFSIZE_BIDI];
	};
	
	array<byte> serialize(int* fd)
	{
		array<byte> bytes;
		bytes.resize(sizeof(state_fr));
		state_fr& out = *(state_fr*)bytes.ptr();
		
		br_ssl_client_freeze(&out.sc, &s.sc, &s.xc);
		out.permissive = (s.xwc.vtable != NULL);
		memcpy(out.iobuf, s.iobuf, sizeof(out.iobuf));
		
		*fd = decompose(&this->sock);
		
		delete this;
		return bytes;
	}
	
	//deserializing constructor
	socketssl_impl(int fd, arrayview<byte> data) : socketssl(fd)
	{
		this->sock = socket::create_from_fd(fd);
		const state_fr& in = *(state_fr*)data.ptr();
		
		state ref;
		
		br_ssl_client_init_full(&s.sc, &s.xc, certs.ptr(), certs.size());
		if (in.permissive)
		{
			s.xwc.vtable = &x509_noanchor_vtable;
			s.xwc.inner = &s.xc.vtable;
			br_ssl_engine_set_x509(&s.sc.eng, &s.xwc.vtable);
		}
		else s.xwc.vtable = NULL;
		br_ssl_engine_set_buffer(&s.sc.eng, s.iobuf, sizeof(s.iobuf), true);
		br_ssl_client_reset(&s.sc, NULL, false);
		
		br_frozen_ssl_client_context fr_sc;
		memcpy(&fr_sc, &in.sc, sizeof(fr_sc));
		br_ssl_client_unfreeze(&fr_sc, &s.sc, &s.xc);
		memcpy(s.iobuf, in.iobuf, sizeof(s.iobuf));
	}
};

socketssl* socketssl::create(socket* parent, cstring domain, bool permissive)
{
	initialize();
	if (!certs) return NULL;
	
	if (!parent) return NULL;
	
	socketssl_impl* ret = new socketssl_impl(parent, domain, permissive);
	if (!ret->establish())
	{
		delete ret;
		return NULL;
	}
	return ret;
}

array<byte> socketssl::serialize(int* fd)
{
	return ((socketssl_impl*)this)->serialize(fd);
}
socketssl* socketssl::deserialize(int fd, arrayview<byte> data)
{
	if (sizeof(socketssl_impl::state_fr) != data.size()) return NULL;
	initialize();
	if (!certs) return NULL;
	
	return new socketssl_impl(fd, data);
}
#endif
