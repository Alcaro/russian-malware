#ifdef ARLIB_SOCKET
#include "socks5.h"

namespace {
class socket_socks5 : public socket {
public:
	runloop* loop;
	socket* inner;
	
	// 0 - expecting (ver)05 (auth)00 (ver)05 (reply=success)00 (reserved)00 (addrtype)??
	// 1 - expecting number of bytes, for addrtype=03 domain
	// 2 - all remaining data is uninteresting and will be discarded
	// 3 - done, all data belongs to caller
	// 4 - failure, all calls will return failure
	int phase;
	int phase_bytes; // for phases 0-2, bytes left until phase switch; for 3/4, ignored
	
	bool create(const socks5_par& param)
	{
		inner = param.to_proxy;
		loop = param.loop;
		
		uint8_t ipbytes[16];
		int iplen = socket::string_to_ip(ipbytes, param.target);
		
		uint8_t req[] = {
			/*version*/ 5, /*nmethods*/ 1, /*anonymous*/ 0, // spec demands GSSAPI support, but screw that
			/*version*/ 5, /*cmd=connect*/ 1, /*reserved*/ 0, (uint8_t)(iplen==4 ? 1 : iplen==16 ? 4 : 3),
		};
		if (inner->send(req) < 0) return false;
		
		if (iplen)
		{
			if (inner->send(arrayview<uint8_t>(ipbytes, iplen)) < 0) return false;
		}
		else
		{
			uint8_t len = param.target.length();
			if (len != param.target.length()) return false; // truncation?
			if (inner->send(arrayview<uint8_t>(&len, 1)) < 0) return false;
			if (inner->send(param.target.bytes()) < 0) return false;
		}
		
		uint8_t port[] = { (uint8_t)(param.port>>8), (uint8_t)(param.port&255) };
		if (inner->send(port) < 0) return false;
		
		phase_bytes = 6;
		return true;
	}
	
	int recv(arrayvieww<uint8_t> data)
	{
		if (LIKELY(phase == 3)) return inner->recv(data);
		
		if (phase < 3)
		{
			// expected response:
			// (ver)05 (auth)00 (ver)05 (reply=success)00 (reserved)00 (atyp)01 (ip)00 00 00 00 (port)00 00
			uint8_t buf[258];
			int n = inner->recv(arrayvieww<uint8_t>(buf, phase_bytes));
			if (n < 0) { phase = 4; return -1; }
			
			if (phase == 0)
			{
				int end = n - (n == phase_bytes);
				for (int i=0;i<end;i++)
				{
					static const uint8_t buf_exp[] = {
						/*ver*/05, /*auth*/00,
						/*ver*/05, /*reply=success*/00, /*reserved*/00, /*addrtype (not checked here)*/
					};
					if (buf[i] != buf_exp[6-phase_bytes+i])
					{
						phase = 4;
						return -1;
					}
				}
			}
			phase_bytes -= n;
			if (phase_bytes) return 0;
			
			uint8_t last = buf[n-1];
			if (phase == 0)
			{
				phase = 2;
				if (last == 1) phase_bytes = 4+2;
				else if (last == 4) phase_bytes = 16+2;
				else if (last == 3) phase = 1;
				else { phase = 4; return -1; }
			}
			else if (phase == 1)
			{
				phase = 2;
				phase_bytes = last+2;
			}
			else
			{
				phase = 3;
			}
			return 0;
		}
		
		return -1;
	}
	int send(arrayview<uint8_t> data)
	{
		return inner->send(data);
	}
	void callback(function<void()> cb_read, function<void()> cb_write) { inner->callback(cb_read, cb_write); }
	
	~socket_socks5() { delete inner; }
};
}

socket* wrap_socks5(const socks5_par& param)
{
	socket_socks5* ret = new socket_socks5();
	if (ret->create(param)) return ret;
	else { delete ret; return NULL; }
}

socket* socks5::connect(
#ifdef ARLIB_SSL
                        bool ssl,
#endif
                        cstring domain, int port, runloop* loop)
{
	socks5_par par = { loop, socket::create(m_host, m_port, loop), domain, (uint16_t)port };
	socket* sock = wrap_socks5(par);
#ifdef ARLIB_SSL
	if (ssl) sock = socket::wrap_ssl(sock, domain, loop);
#endif
	return sock;
}

#include "test.h"

#ifdef ARLIB_TEST
static void clienttest(cstring target)
{
	test_skip("too slow");
	test_skip_force("ssh -D is offline");
	
	autoptr<runloop> loop = runloop::create();
	struct socks5_par param = { loop, socket::create("localhost", 1080, loop), target, 80 };
	socket* sock = wrap_socks5(param);
	
	socket_test_http(sock, loop);
}

test("SOCKS5 with IP", "tcp", "socks5") { clienttest("1.1.1.1"); }
test("SOCKS5 with domain", "tcp", "socks5") { clienttest("google.com"); }
#endif
#endif
