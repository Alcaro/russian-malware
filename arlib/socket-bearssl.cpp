#ifdef ARLIB_SSL_BEARSSL
#include "socket.h"
#include "base64.h"
#include "file.h"

#include "deps/bearssl-0.6/inc/bearssl.h"

namespace {
// based on bearssl-0.3/tools/certs.c and files.c, heavily rewritten

// use a static arena for allocations, to avoid memory leaks on hybrid DLL
// the anchor structs are allocated backwards, while the actual keys they point to go forwards, so they meet in the middle
static uint8_t alloc_arena[256*1024]; // on a normal Linux, 64KB of this is used (ca-certificates.crt is 200KB); 17KB on Windows
static size_t alloc_blobs = 0; // reserving more than I need is harmless, the OS won't allocate unused pages
static br_x509_trust_anchor * const alloc_certs_initial = (br_x509_trust_anchor*)(alloc_arena + sizeof(alloc_arena));
// can't just = alloc_certs_initial, gcc initializes it in wrong order vs the oninit
static br_x509_trust_anchor * alloc_certs = (br_x509_trust_anchor*)(alloc_arena + sizeof(alloc_arena));

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

oninit_static()
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

class socket2_bearssl : public socket2 {
public:
	autoptr<socket2> sock;
	
	br_ssl_client_context sc;
	br_x509_minimal_context xc;
	uint8_t iobuf[BR_SSL_BUFSIZE_BIDI];
	
	socket2_bearssl(autoptr<socket2> sock, cstring domain) : sock(std::move(sock))
	{
		br_ssl_client_init_full(&sc, &xc, alloc_certs, alloc_certs_initial - alloc_certs);
		br_ssl_engine_set_buffer(&sc.eng, &iobuf, sizeof(iobuf), true);
		br_ssl_client_reset(&sc, domain.c_str(), false);
		process();
	}
	
	// this object has to track four different byte streams
	
	producer<void> sendapp_p;
	producer<void> recvapp_p;
	
	struct sendrec_wt : public waiter<void, sendrec_wt> {
		void complete() { container_of<&socket2_bearssl::sendrec_w>(this)->ready_sendrec(); }
	} sendrec_w;
	struct recvrec_wt : public waiter<void, recvrec_wt> {
		void complete() { container_of<&socket2_bearssl::recvrec_w>(this)->ready_recvrec(); }
	} recvrec_w;
	
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	
	void process()
	{
		unsigned st = br_ssl_engine_current_state(&sc.eng);
		if (st & BR_SSL_CLOSED)
			sock = nullptr;
		if (!sock)
		{
			if (sendapp_p.has_waiter())
				RETURN_IF_CALLBACK_DESTRUCTS(sendapp_p.complete());
			if (recvapp_p.has_waiter())
				recvapp_p.complete(); // don't bother with RETURN_IF here, we're gonna return anyways
			return;
		}
		
		if ((st & BR_SSL_SENDREC) && !sendrec_w.is_waiting())
			RETURN_IF_CALLBACK_DESTRUCTS(ready_sendrec());
		if ((st & BR_SSL_RECVREC) && !recvrec_w.is_waiting() && sock != nullptr)
			RETURN_IF_CALLBACK_DESTRUCTS(sock->can_recv().then(&recvrec_w));
		
		st = br_ssl_engine_current_state(&sc.eng);
		if ((st & BR_SSL_SENDAPP) && sendapp_p.has_waiter())
			RETURN_IF_CALLBACK_DESTRUCTS(sendapp_p.complete());
		if ((st & BR_SSL_RECVAPP) && recvapp_p.has_waiter())
			recvapp_p.complete();
	}
	
	void ready_recvrec()
	{
		size_t len;
		uint8_t* out = br_ssl_engine_recvrec_buf(&sc.eng, &len);
		bytesw by(out, len);
		ssize_t n = sock->recv_sync(by);
		if (n > 0)
			br_ssl_engine_recvrec_ack(&sc.eng, n);
		if (n < 0)
			sock = nullptr;
		if (n != 0)
			process();
		if (n == 0)
			sock->can_recv().then(&recvrec_w);
	}
	void ready_sendrec()
	{
		size_t len;
		uint8_t* out = br_ssl_engine_sendrec_buf(&sc.eng, &len);
		bytesr by(out, len);
		ssize_t n = sock->send_sync(by);
		if (n > 0)
			br_ssl_engine_sendrec_ack(&sc.eng, n);
		if (n < 0)
			sock = nullptr;
		if (n != 0)
			process();
		if (n == 0)
			sock->can_send().then(&sendrec_w);
	}
	
	
	ssize_t recv_sync(bytesw by) override
	{
		size_t len;
		uint8_t* out = br_ssl_engine_recvapp_buf(&sc.eng, &len);
		if (len)
		{
			size_t n = min(len, by.size());
			memcpy(by.ptr(), out, n);
			br_ssl_engine_recvapp_ack(&sc.eng, n);
			process();
			return n;
		}
		else if (!sock)
			return -1;
		return 0;
	}
	ssize_t send_sync(bytesr by) override
	{
		size_t len;
		uint8_t* out = br_ssl_engine_sendapp_buf(&sc.eng, &len);
		if (len)
		{
			size_t n = min(len, by.size());
			memcpy(out, by.ptr(), n);
			br_ssl_engine_sendapp_ack(&sc.eng, n);
			br_ssl_engine_flush(&sc.eng, false);
			process();
			return n;
		}
		else if (!sock)
			return -1;
		
		return 0;
	}
	
	async<void> can_recv() override
	{
		size_t len;
		uint8_t* out = br_ssl_engine_recvapp_buf(&sc.eng, &len);
		if (out) return recvapp_p.complete_sync();
		else return &recvapp_p;
	}
	async<void> can_send() override
	{
		size_t len;
		uint8_t* out = br_ssl_engine_sendapp_buf(&sc.eng, &len);
		if (out) return sendapp_p.complete_sync();
		else return &sendapp_p;
	}
};
}

async<autoptr<socket2>> socket2::wrap_ssl_bearssl(autoptr<socket2> inner, cstring domain)
{
	if (!inner)
		co_return nullptr;
	socket2_bearssl* bear = new socket2_bearssl(std::move(inner), domain);
	autoptr<socket2> ret = bear;
	co_await bear->can_send();
	if (!bear->sock)
		co_return nullptr;
	co_return ret;
}
#endif

//#ifdef ARLIB_SOCKET
//#include "socket.h"
//async<autoptr<socket2>> socket2::wrap_ssl_openssl(autoptr<socket2> inner, cstrnul domain) { co_return nullptr; }
//#endif
