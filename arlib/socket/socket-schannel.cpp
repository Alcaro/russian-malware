#include "socket.h"
#include "../test.h"

//based on http://wayback.archive.org/web/20100528130307/http://www.coastrd.com/c-schannel-smtp
//but heavily rewritten for stability and compactness

#ifdef ARLIB_SSL_SCHANNEL
//#include "../thread.h"
#include "../bytepipe.h"
#ifndef _WIN32
#error SChannel only exists on Windows
#endif

#undef socket
#define SECURITY_WIN32
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winsock.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <schannel.h>
#include <security.h>
#include <sspi.h>
#define socket socket_t

namespace {

static SecurityFunctionTable* SSPI;
static CredHandle cred;

#define SSPIFlags \
	(ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | \
	 ISC_RET_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM)

//my mingw headers are outdated
#ifndef SCH_USE_STRONG_CRYPTO
#define SCH_USE_STRONG_CRYPTO 0x00400000
#endif
#ifndef SP_PROT_TLS1_2_CLIENT
#define SP_PROT_TLS1_2_CLIENT 0x00000800
#endif
#ifndef SEC_Entry
#define SEC_Entry WINAPI
#endif

oninit()
{
	SSPI = InitSecurityInterfaceA();
	
	SCHANNEL_CRED SchannelCred = {};
	SchannelCred.dwVersion = SCHANNEL_CRED_VERSION;
	SchannelCred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
	// fun fact: IE11 doesn't use SCH_USE_STRONG_CRYPTO. I guess it favors accepting outdated servers over rejecting evil ones.
	SchannelCred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT; // MS recommends setting this to zero, but that makes it use TLS 1.0.
	//howsmyssl expects session ticket support for the Good rating, but that's only supported on windows 8, according to
	// https://connect.microsoft.com/IE/feedback/details/997136/internet-explorer-11-on-windows-7-does-not-support-tls-session-tickets
	//and I can't find which flag enables that, anyways
	
	SSPI->AcquireCredentialsHandleA(NULL, (char*)UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
	                                NULL, &SchannelCred, NULL, NULL, &cred, NULL);
}

class socketssl_schannel : public socket {
public:
	socket* inner;
	CtxtHandle ssl;
	SecPkgContext_StreamSizes bufsizes;
	
	bytepipe recv_crypt;
	bytepipe recv_plain;
	bytepipe send_crypt;
	
	bool in_handshake;
	bool permissive;
	
	function<void()> cb_read;
	function<void()> cb_write;
	
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	
	void fetch()
	{
		arrayvieww<uint8_t> bytes = recv_crypt.push_buf(1024);
		int ret = inner->recv(bytes);
		if (ret < 0) return error();
		recv_crypt.push_done(ret);
	}
	
	void error()
	{
		SSPI->DeleteSecurityContext(&ssl);
		delete inner;
		inner = NULL;
		in_handshake = false;
	}
	
	void handshake()
	{
		if (!in_handshake) return;
		
		SecBuffer InBuffers[2] = { { (uint32_t)recv_crypt.size(), SECBUFFER_TOKEN, (void*)recv_crypt.pull_buf_full().ptr() },
		                           { 0, SECBUFFER_EMPTY, NULL } };
		SecBufferDesc InBufferDesc = { SECBUFFER_VERSION, 2, InBuffers };
		
		SecBuffer OutBuffer = { 0, SECBUFFER_TOKEN, NULL };
		SecBufferDesc OutBufferDesc = { SECBUFFER_VERSION, 1, &OutBuffer };
		
		DWORD ignore;
		SECURITY_STATUS scRet;
		ULONG flags = SSPIFlags;
		if (this->permissive) flags |= ISC_REQ_MANUAL_CRED_VALIDATION; // +1 for defaulting to secure
		scRet = SSPI->InitializeSecurityContextA(&cred, &ssl, NULL, flags, 0, SECURITY_NATIVE_DREP,
		                                         &InBufferDesc, 0, NULL, &OutBufferDesc, &ignore, NULL);
		
		// according to the original program, extended errors are success
		// but they also hit the error handler below, so I guess it just sends an error to the server?
		// either way, ignore
		if (scRet == SEC_E_OK || scRet == SEC_I_CONTINUE_NEEDED)
		{
			if (OutBuffer.cbBuffer != 0 && OutBuffer.pvBuffer != NULL)
			{
				send_crypt.push(arrayview<uint8_t>((BYTE*)OutBuffer.pvBuffer, OutBuffer.cbBuffer));
				SSPI->FreeContextBuffer(OutBuffer.pvBuffer);
			}
		}
		
		if (scRet == SEC_E_INCOMPLETE_MESSAGE) return;
		
		if (scRet == SEC_E_OK)
		{
			in_handshake = false;
			SSPI->QueryContextAttributes(&ssl, SECPKG_ATTR_STREAM_SIZES, &bufsizes);
		}
		
		if (FAILED(scRet))
		{
			error();
			return;
		}
		
		// SEC_I_INCOMPLETE_CREDENTIALS is possible and means server requested client authentication
		// we don't support that, just ignore it
		
		if (InBuffers[1].BufferType == SECBUFFER_EXTRA)
			recv_crypt.pull_done(recv_crypt.size() - InBuffers[1].cbBuffer);
		else recv_crypt.reset();
		
		if (scRet == SEC_E_OK) set_child_cb();
	}
	
	bool handshake_first(const char * domain)
	{
		SecBuffer OutBuffer = { 0, SECBUFFER_TOKEN, NULL };
		SecBufferDesc OutBufferDesc = { SECBUFFER_VERSION, 1, &OutBuffer };
		
		DWORD ignore;
		if (SSPI->InitializeSecurityContextA(&cred, NULL, (char*)domain, SSPIFlags, 0, SECURITY_NATIVE_DREP,
		                                     NULL, 0, &ssl, &OutBufferDesc, &ignore, NULL)
		    != SEC_I_CONTINUE_NEEDED)
		{
			return false;
		}
		
		if (OutBuffer.cbBuffer != 0)
		{
			send_crypt.push(arrayview<uint8_t>((BYTE*)OutBuffer.pvBuffer, OutBuffer.cbBuffer));
			process();
			SSPI->FreeContextBuffer(OutBuffer.pvBuffer); // Free output buffer.
		}
		
		in_handshake = true;
		return true;
	}
	
	bool init(socket* inner, const char * domain, bool permissive, runloop* loop)
	{
		if (!inner) return false;
		
		this->inner = inner;
		this->permissive = permissive;
		
		if (!handshake_first(domain)) return false;
		set_child_cb();
		
		return (this->inner);
	}
	
	void process()
	{
		handshake();
		
		if (send_crypt)
		{
			int ret = inner->send(send_crypt.pull_buf());
			if (ret < 0) return error();
			send_crypt.pull_done(ret);
		}
		
		bool again = true;
		
		while (again)
		{
			again = false;
			
			SecBuffer Buffers[4] = {
				{ (uint32_t)recv_crypt.size(), SECBUFFER_DATA,  (void*)recv_crypt.pull_buf_full().ptr() },
				{ 0,                           SECBUFFER_EMPTY, NULL },
				{ 0,                           SECBUFFER_EMPTY, NULL },
				{ 0,                           SECBUFFER_EMPTY, NULL },
			};
			SecBufferDesc Message = { SECBUFFER_VERSION, 4, Buffers };
			
			SECURITY_STATUS scRet = SSPI->DecryptMessage(&ssl, &Message, 0, NULL);
			if (scRet == SEC_E_INCOMPLETE_MESSAGE) return;
			else if (scRet == SEC_I_RENEGOTIATE) in_handshake = true;
			else if (scRet != SEC_E_OK) return error();
			
			bool do_reset = true;
			for (int i=0;i<4;i++)
			{
				if (Buffers[i].BufferType == SECBUFFER_DATA)
				{
					recv_plain.push(arrayview<uint8_t>((uint8_t*)Buffers[i].pvBuffer, Buffers[i].cbBuffer));
					again = true;
				}
				if (Buffers[i].BufferType == SECBUFFER_EXTRA)
				{
					do_reset = false;
					recv_crypt.pull_done(recv_crypt.size() - Buffers[i].cbBuffer);
					again = true;
				}
			}
			if (do_reset) recv_crypt.reset();
		}
		
		set_child_cb();
	}
	
	int recv(arrayvieww<uint8_t> data) override
	{
		if (inner)
		{
			fetch();
			process();
		}
		else if (!recv_plain) return -1;
		
		arrayview<uint8_t> ret = recv_plain.pull_buf();
		size_t n = min(ret.size(), data.size());
		memcpy(data.ptr(), ret.ptr(), n);
		recv_plain.pull_done(n);
		return n;
	}
	
	int send(arrayview<uint8_t> bytes) override
	{
		if (!inner) return -1;
		if (in_handshake) return 0;
		
		const uint8_t * data = bytes.ptr();
		uint32_t len = bytes.size();
		
		fetch();
		
		BYTE sendbuf[0x1000];
		
		unsigned int maxmsglen = sizeof(sendbuf) - bufsizes.cbHeader - bufsizes.cbTrailer;
		if (len > maxmsglen) len = maxmsglen;
		
		memcpy(sendbuf+bufsizes.cbHeader, data, len);
		SecBuffer Buffers[4] = {
			{ bufsizes.cbHeader,  SECBUFFER_STREAM_HEADER,  sendbuf },
			{ len,                SECBUFFER_DATA,           sendbuf+bufsizes.cbHeader },
			{ bufsizes.cbTrailer, SECBUFFER_STREAM_TRAILER, sendbuf+bufsizes.cbHeader+len },
			{ 0,                  SECBUFFER_EMPTY,          NULL },
		};
		SecBufferDesc Message = { SECBUFFER_VERSION, 4, Buffers };
		if (FAILED(SSPI->EncryptMessage(&ssl, 0, &Message, 0))) { error(); return -1; }
		
		send_crypt.push(arrayview<uint8_t>(sendbuf, Buffers[0].cbBuffer + Buffers[1].cbBuffer + Buffers[2].cbBuffer));
		process();
		return len;
	}
	
	/*private*/ void set_child_cb()
	{
		if (inner)
		{
			inner->callback(bind_this(&socketssl_schannel::on_readable), send_crypt ? bind_this(&socketssl_schannel::on_writable) : NULL);
		}
	}
	
	/*private*/ void do_cbs()
	{
		while (cb_read  && (recv_plain    || !inner)) RETURN_IF_CALLBACK_DESTRUCTS(cb_read( ));
		while (cb_write && (!in_handshake || !inner)) RETURN_IF_CALLBACK_DESTRUCTS(cb_write());
		
		set_child_cb();
	}
	
	// TODO: clean this up
	/*private*/ void on_readable() { fetch(); process(); do_cbs(); }
	/*private*/ void on_writable() { fetch(); process(); do_cbs(); }
	void callback(function<void()> cb_read, function<void()> cb_write)
	{
		this->cb_read = cb_read;
		this->cb_write = cb_write;
	}
	
	~socketssl_schannel()
	{
		error();
	}
};

}

socket* socket::wrap_ssl_raw(socket* inner, cstring domain, runloop* loop)
{
	socketssl_schannel* ret = new socketssl_schannel();
	if (!ret->init(inner, domain.c_str(), false, loop)) { delete ret; return NULL; }
	else return ret;
}
socket* socket::wrap_ssl_raw_noverify(socket* inner, runloop* loop)
{
	socketssl_schannel* ret = new socketssl_schannel();
	if (!ret->init(inner, "example.com", true, loop)) { delete ret; return NULL; }
	else return ret;
}
#endif
