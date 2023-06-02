#define SECURITY_WIN32

#ifdef ARLIB_SSL_SCHANNEL
#include "socket.h"
#include <sspi.h>
#include <schannel.h>

namespace {

static SecurityFunctionTable* g_sspi;
static CredHandle g_cred;

oninit()
{
	g_sspi = InitSecurityInterfaceA();
	SCHANNEL_CRED SchannelCred = {};
	SchannelCred.dwVersion = SCHANNEL_CRED_VERSION;
	SchannelCred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
	SchannelCred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT; // MS recommends setting this to zero, but that makes it use TLS 1.0.
	g_sspi->AcquireCredentialsHandleA(nullptr, UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
	                                  nullptr, &SchannelCred, nullptr, nullptr, &g_cred, nullptr);
}
}

namespace {
class socket2_schannel : public socket2 {
public:
	autoptr<socket2> sock;
	
	CtxtHandle ctx;
	SecPkgContext_StreamSizes bufsizes;
	
	uint16_t recv_buf_plaintext = 0;
	uint16_t recv_buf_amount = 0;
	uint16_t send_buf_amount = 0;
	
	uint8_t recv_buf[5 + 16384 + 256];
	uint8_t send_buf[5 + 16384 + 256];
	
	producer<void> prod_recv;
	producer<void> prod_send;
	
	waiter<void> wait_recv = make_waiter<&socket2_schannel::wait_recv, &socket2_schannel::wait_recv_done>();
	waiter<void> wait_send = make_waiter<&socket2_schannel::wait_send, &socket2_schannel::wait_send_done>();
	
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	
	void error()
	{
		sock = nullptr;
		if (prod_send.has_waiter())
			RETURN_IF_CALLBACK_DESTRUCTS(prod_send.complete());
		if (prod_recv.has_waiter())
			prod_recv.complete();
	}
	
	void try_decrypt()
	{
		SecBuffer in_buf[4] = {
			{ (uint32_t)recv_buf_amount, SECBUFFER_DATA,  recv_buf },
			{ 0,                         SECBUFFER_EMPTY, NULL },
			{ 0,                         SECBUFFER_EMPTY, NULL },
			{ 0,                         SECBUFFER_EMPTY, NULL },
		};
		SecBufferDesc in_bufs = { SECBUFFER_VERSION, ARRAY_SIZE(in_buf), in_buf };
		
		SECURITY_STATUS st = g_sspi->DecryptMessage(&ctx, &in_bufs, 0, NULL);
		if (st == SEC_E_INCOMPLETE_MESSAGE)
		{
			return;
		}
		else if (st != SEC_E_OK) // SEC_I_RENEGOTIATE is allegedly possible, but I've never seen a server that sends that
		{
			sock = nullptr;
			return;
		}
		
		size_t n_extra = 0;
		for (SecBuffer& buf : in_buf)
		{
			if (buf.BufferType == SECBUFFER_DATA)
			{
				memmove(recv_buf, buf.pvBuffer, buf.cbBuffer);
				recv_buf_plaintext = buf.cbBuffer;
			}
			if (buf.BufferType == SECBUFFER_EXTRA)
			{
				n_extra = buf.cbBuffer;
			}
		}
		memmove(recv_buf+recv_buf_plaintext, recv_buf+recv_buf_amount-n_extra, n_extra);
		recv_buf_amount = n_extra + recv_buf_plaintext;
	}
	
	void wait_recv_done()
	{
		if (recv_buf_amount)
		{
			try_decrypt();
			if (recv_buf_plaintext)
				goto have_data;
		}
		if (!sock)
			return error();
		
		ssize_t n;
		n = sock->recv_sync(bytesw(recv_buf).skip(recv_buf_amount));
		if (n < 0)
			return error();
		recv_buf_amount += n;
		
		try_decrypt();
		if (!sock)
			return error();
		
		if (recv_buf_plaintext)
		{
		have_data:
			if (prod_recv.has_waiter())
				prod_recv.complete();
		}
		else
		{
			sock->can_recv().then(&wait_recv);
		}
	}
	
	void wait_send_done()
	{
		if (!sock)
			return;
		ssize_t n = sock->send_sync(bytesr(send_buf, send_buf_amount));
		if (n < 0)
			return error();
		
		memmove(send_buf, send_buf+n, send_buf_amount-n);
		send_buf_amount -= n;
		if (send_buf_amount)
		{
			sock->can_send().then(&wait_send);
		}
		else
		{
			if (prod_send.has_waiter())
				prod_send.complete();
		}
	}
	
	ssize_t recv_sync(bytesw by) override
	{
		if (!recv_buf_plaintext)
			return sock ? 0 : -1;
		size_t n = min(by.size(), recv_buf_plaintext);
		memcpy(by.ptr(), recv_buf, n);
		memmove(recv_buf, recv_buf+n, recv_buf_amount-n);
		recv_buf_plaintext -= n;
		recv_buf_amount -= n;
		if (recv_buf_plaintext == 0)
			wait_recv_done();
		return n;
	}
	ssize_t send_sync(bytesr by) override
	{
		if (!sock)
			return -1;
		if (send_buf_amount)
			return 0;
		
		uint32_t len = min(by.size(), bufsizes.cbMaximumMessage, sizeof(send_buf) - bufsizes.cbHeader - bufsizes.cbTrailer);
		
		memcpy(send_buf+bufsizes.cbHeader, by.ptr(), len);
		SecBuffer out_buf[4] = {
			{ bufsizes.cbHeader,  SECBUFFER_STREAM_HEADER,  send_buf },
			{ len,                SECBUFFER_DATA,           send_buf+bufsizes.cbHeader },
			{ bufsizes.cbTrailer, SECBUFFER_STREAM_TRAILER, send_buf+bufsizes.cbHeader+len },
			{ 0,                  SECBUFFER_EMPTY,          NULL },
		};
		SecBufferDesc out_bufs = { SECBUFFER_VERSION, ARRAY_SIZE(out_buf), out_buf };
		if (FAILED(g_sspi->EncryptMessage(&ctx, 0, &out_bufs, 0)))
		{
			error();
			return -1;
		}
		
		send_buf_amount = out_buf[0].cbBuffer + out_buf[1].cbBuffer + out_buf[2].cbBuffer + out_buf[3].cbBuffer;
		wait_send_done();
		return len;
	}
	
	async<void> can_recv() override
	{
		if (recv_buf_plaintext || !sock)
			return prod_recv.complete_sync();
		else
			return &prod_recv;
	}
	
	async<void> can_send() override
	{
		if (!send_buf_amount || !sock)
			return prod_send.complete_sync();
		else
			return &prod_send;
	}
	
	~socket2_schannel()
	{
		g_sspi->DeleteSecurityContext(&ctx);
	}
};

}

async<autoptr<socket2>> socket2::wrap_ssl_schannel(autoptr<socket2> inner, cstring domain)
{
	if (!inner) co_return nullptr;
	socket2_schannel* sch = new socket2_schannel;
	sch->sock = std::move(inner);
	autoptr<socket2> ret = sch;
	
	unsigned long flags = (ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
	                       ISC_REQ_STREAM | ISC_REQ_USE_SUPPLIED_CREDS);
	unsigned long attrib;
	SECURITY_STATUS st;
	
	{
		SecBuffer out_buf[1] = { sizeof(sch->send_buf), SECBUFFER_TOKEN, sch->send_buf };
		SecBufferDesc out_bufs = { SECBUFFER_VERSION, ARRAY_SIZE(out_buf), out_buf };
		
		SECURITY_STATUS st = g_sspi->InitializeSecurityContextA(&g_cred, nullptr, (char*)domain.c_str().c_str(), flags, 0, 0,
		                                                        nullptr, 0, &sch->ctx, &out_bufs, &attrib, nullptr);
		if (st != SEC_I_CONTINUE_NEEDED)
			co_return nullptr;
		
		sch->send_buf_amount = out_buf[0].cbBuffer;
		sch->wait_send_done();
		co_await sch->can_send();
	}
	
	while (true)
	{
		SecBuffer in_buf[2] = { { sch->recv_buf_amount, SECBUFFER_TOKEN, sch->recv_buf },
		                        { 0, SECBUFFER_EMPTY, NULL } };
		SecBufferDesc in_bufs = { SECBUFFER_VERSION, ARRAY_SIZE(in_buf), in_buf };
		
		SecBuffer out_buf[1] = { sizeof(sch->send_buf), SECBUFFER_TOKEN, sch->send_buf };
		SecBufferDesc out_bufs = { SECBUFFER_VERSION, ARRAY_SIZE(out_buf), out_buf };
		
		SECURITY_STATUS st = g_sspi->InitializeSecurityContextA(&g_cred, &sch->ctx, nullptr, flags, 0, 0,
		                                                        &in_bufs, 0, nullptr, &out_bufs, &attrib, nullptr);
		if (st == SEC_E_INCOMPLETE_MESSAGE) // like EAGAIN, I'm not sure why this one is tagged as an error
		{
			co_await sch->sock->can_recv();
			ssize_t n = sch->sock->recv_sync(bytesw(sch->recv_buf).skip(sch->recv_buf_amount));
			if (n < 0)
				co_return nullptr;
			sch->recv_buf_amount += n;
			continue;
		}
		if (FAILED(st))
			co_return nullptr;
		
		if (out_buf[0].cbBuffer)
		{
			sch->send_buf_amount = out_buf[0].cbBuffer;
			sch->wait_send_done();
			co_await sch->can_send();
		}
		
		size_t remainder = 0;
		if (in_buf[1].BufferType == SECBUFFER_EXTRA)
			remainder = in_buf[1].cbBuffer;
		memmove(sch->recv_buf, sch->recv_buf+sch->recv_buf_amount-remainder, remainder);
		sch->recv_buf_amount = remainder;
		
		if (st == SEC_E_OK)
			break;
	}
	
	sch->wait_recv_done();
	
	g_sspi->QueryContextAttributes(&sch->ctx, SECPKG_ATTR_STREAM_SIZES, &sch->bufsizes);
	// these three numbers should be 5, 16384 and 256, respectively
	if (sch->bufsizes.cbHeader + sch->bufsizes.cbMaximumMessage + sch->bufsizes.cbTrailer > sizeof(socket2_schannel::recv_buf))
		abort();
	
	co_return ret;
}
#endif
