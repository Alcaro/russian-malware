#ifdef ARLIB_SOCKET
#include "set.h"
#include "file.h"
#include "bytestream.h"
#include "random.h"
#include "socket.h"

#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#endif

// This is a fairly primitive DNS client. It doesn't retry, it doesn't try multiple resolvers, it doesn't support recursion.
// But it works in practice on all systems I've seen. (Which admittedly isn't many.)

namespace {

// Per RFCs and other rules and specifications, a legal domain name matches
//  ^(?=.{1,253}$)(xn--[a-z0-9\-]{1,59}|(?!..--)[a-z0-9\-]{1,63}\.)*([a-z]{1,63}|xn--[a-z0-9\-]{1,59})$
// or, in plaintext:
// - The domain is 1 to 253 chars textual (3 to 255 bytes encoded)
// - The domain consists of zero or more dot-separated child labels, followed by the TLD label
// - Each label consists of 1 to 63 of a-z 0-9 and dash, no other char
// - Each label either starts with xn--, or does not start with letter-letter-dash-dash
// - The TLD label either starts with xn--, or is strictly alphabetical
// This function enforces the first three rules, but replaces the latter two with 'the TLD must start with a letter'.
// If 'port' is not null, the domain may be optionally suffixed with colon and a port. If so, *port should contain a default value.
// 'out' must be at least 256 bytes.
static bool encode_domain_name(cstring addr, bytestreamw& out, uint16_t* port)
{
	bytesr by = addr.bytes();
	const uint8_t * start = by.ptr();
	const uint8_t * end = start + by.size();
	
	const uint8_t * labelstart = start;
	const uint8_t * it = start;
	while (true)
	{
		if (it != end && (islower(*it) || isdigit(*it) || *it == '-'))
		{
			it++;
			continue;
		}
		
		if (it-start > 253)
			return false;
		
		size_t labellen = it-labelstart;
		if (labellen == 0 || labellen > 63)
			return false;
		out.u8(labellen);
		out.bytes(bytesr(labelstart, labellen));
		
		if (it == end)
		{
			out.u8(0);
			return isalpha(*labelstart);
		}
		
		char sep = *it++;
		if (sep == '.')
		{
			labelstart = it;
		}
		else if (sep == ':')
		{
			out.u8(0);
			return (isalpha(*labelstart) && port && fromstring(cstring(bytesr(it, end-it)), *port));
		}
		else return false;
	}
}

class hosts_txt {
public:
	// key is a dns-encoded domain name
	map<bytearray, socket2::address> entries;
	
	hosts_txt()
	{
#ifdef __unix__
		for (string line : file::readallt("/etc/hosts").split("\n"))
#else
		static const uint8_t localhost_name[] = { 9,'l','o','c','a','l','h','o','s','t', 0 };
		static const uint8_t localhost_addr[] = { 127,0,0,1 };
		entries.insert(bytesr(localhost_name), bytesr(localhost_addr)); // commented out and hardcoded in default hosts file
		for (string line : file::readallt("C:/Windows/System32/drivers/etc/hosts").split("\n"))
#endif
		{
			auto classify = [](char ch) -> int {
				if (ch == '\0' || ch == '\r' || ch == '\n' || ch == '#') return 0;
				if (ch == '\t' || ch == ' ') return 1;
				return 2;
			};
			
			size_t i = 0;
			while (classify(line[i]) == 1) i++;
			size_t addrstart = i;
			while (classify(line[i]) == 2) i++;
			if (addrstart == i) continue;
			
			cstring addr = line.substr(addrstart, i);
			while (classify(line[i]) != 0)
			{
				while (classify(line[i]) == 1) i++;
				size_t domstart = i;
				while (classify(line[i]) == 2) i++;
				if (domstart == i) continue;
				
				socket2::address addr_bin = (string)addr;
				
				uint8_t buf[256];
				bytestreamw by = bytesw(buf);
				if (addr_bin && encode_domain_name(line.substr(domstart, i), by, nullptr))
				{
					entries.insert(by.finish(), addr_bin);
				}
			}
		}
	}
};

class dns_t;
dns_t* get_dns() { return (dns_t*)runloop2::get_dns(); }

static socket2::address parse_reply(bytesr packet, bytesr domain_encoded, uint16_t port);

class dns_t {
public:
	autoptr<socket2_udp> sock;
	hosts_txt hosts;
	uint8_t recv_buf[512];
	
	struct recv_wt : public waiter_fn<ssize_t, recv_wt> {
		void complete(ssize_t n) { container_of<&dns_t::recv_w>(this)->complete_recv(n); }
	} recv_w;
	struct time_wt : public waiter_fn<void, time_wt> {
		void complete() { container_of<&dns_t::time_w>(this)->timeout(nullptr); }
	} time_w;
	
	struct waiter_node {
		struct prod_t : public producer_fn<socket2::address, prod_t> {
			void cancel() { get_dns()->cancel(container_of<&waiter_node::prod>(this)); }
		} prod;
		
		timestamp sent;
		
		uint16_t trid;
		uint16_t port;
		
		uint8_t send_buf_domain_start;
		uint8_t send_buf_domain_len;
		uint16_t send_buf_size;
		uint8_t send_buf[512];
	};
	allocatable_array<waiter_node, [](waiter_node* n) { return &n->trid; }, [](waiter_node* n) { n->prod.update_waiter(); }> waiting;
	
	static socket2::address default_resolver()
	{
#ifdef __unix__
		return { ("\n"+file::readall("/etc/resolv.conf")).split<1>("\nnameserver ")[1].split<1>("\n")[0], 53 };
#else
		// TODO: figure out why this fails
		/*
		uint8_t buf[16384];
		array<uint8_t> buf_dyn;
		
		ULONG bufsize = sizeof(buf);
		IP_ADAPTER_ADDRESSES* ipaa = (IP_ADAPTER_ADDRESSES*)buf;
		
	again:
		ULONG flags = GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_FRIENDLY_NAME;
		ULONG status = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, ipaa, &bufsize);
		if (status == ERROR_BUFFER_OVERFLOW)
		{
			buf_dyn.resize(bufsize);
			ipaa = (IP_ADAPTER_ADDRESSES*)buf_dyn.ptr();
			goto again;
		}
		if (status != ERROR_SUCCESS) return "";
		
	puts("a");
	printf("%lx\n",status);
	printf("%p\n",ipaa);
	printf("%p\n",ipaa->FirstDnsServerAddress);
	printf("%p\n",ipaa->FirstDnsServerAddress->Address);
		SOCKET_ADDRESS* rawaddr = &ipaa->FirstDnsServerAddress->Address;
		puts("=="+socket::ip_to_string(arrayview<uint8_t>((uint8_t*)rawaddr->lpSockaddr, rawaddr->iSockaddrLength)));
		exit(42);
		/*/
		FIXED_INFO info;
		ULONG info_size = sizeof(info);
		DWORD status = GetNetworkParams(&info, &info_size);
		if (status == ERROR_BUFFER_OVERFLOW)
		{
			array<byte> buf;
			buf.resize(info_size);
			if (GetNetworkParams(&info, &info_size) == ERROR_SUCCESS)
			{
				FIXED_INFO* info2 = (FIXED_INFO*)buf.ptr();
				return { info2->DnsServerList.IpAddress.String, 53 };
			}
			else return {};
		}
		if (status == ERROR_SUCCESS) return { info.DnsServerList.IpAddress.String, 53 };
		else return {};
		//*/
#endif
	}
	
	dns_t()
	{
		sock = socket2_udp::create(default_resolver());
		sock->recv(recv_buf).then(&recv_w);
	}
	
	waiter_node* node_for_trid(uint16_t trid)
	{
		// just loop them all, it'll rarely go above 2 or so
		for (waiter_node& n : waiting)
		{
			if (n.trid == trid && n.prod.has_waiter())
				return &n;
		}
		return nullptr;
	}
	
	uint16_t pick_trid()
	{
	again:
		uint16_t trid = g_rand.rand32();
		if (node_for_trid(trid))
			goto again;
		return trid;
	}
	
	async<socket2::address> my_complete(waiter_node* n, socket2::address addr)
	{
		waiting.dealloc(n);
		n->prod.complete(addr);
		return &n->prod;
	}
	
	async<socket2::address> await_lookup(cstring domain, uint16_t port)
	{
		waiter_node* n = waiting.alloc();
		
		socket2::address addr { domain, port };
		if (addr)
			return my_complete(n, addr);
		
		bytestreamw req(n->send_buf);
		
		n->trid = this->pick_trid();
		n->port = port;
		n->sent = timestamp::now();
		req.u16b(n->trid);
		
		uint16_t flags = 0;
		flags |= 0<<15; // QR, 'is response' flag
		flags |= 0<<11; // OPCODE, 4 bits; 0 = normal query
		flags |= 0<<10; // AA, 'is authorative' flag
		flags |= 0<<9; // TC, 'answer truncated' flag
		flags |= 1<<8; // RD, recursion desired
		flags |= 0<<7; // RA, recursion available
		flags |= 0<<4; // Z, 3 bits, reserved
		flags |= 0<<0; // RCODE, 4 bits; 0 = no error
		req.u16b(flags);
		
		req.u16b(1); // QDCOUNT
		req.u16b(0); // ANCOUNT
		req.u16b(0); // NSCOUNT
		req.u16b(0); // ARCOUNT
		
		n->send_buf_domain_start = req.tell();
		if (!encode_domain_name(domain, req, &n->port))
			return my_complete(n, {});
		n->send_buf_domain_len = req.tell() - n->send_buf_domain_start;
		
		bytesr domain_encoded = bytesr(n->send_buf+n->send_buf_domain_start, n->send_buf_domain_len);
		socket2::address* ret_fixed = this->hosts.entries.get_or_null(domain_encoded);
		if (ret_fixed)
			return my_complete(n, ret_fixed->with_port(n->port)); // will change it in hosts.txt, but that doesn't matter
		
		req.u16b(0x0001); // type A (could switch to 0x00FF Everything, but I can't test ipv6 so let's not ask for it)
		req.u16b(0x0001); // class IN
		//judging by musl libc, there's no way to ask for both ipv4 and ipv6 but not everything else, it sends two separate queries
		
		n->send_buf_size = req.tell();
		
		sock->send(req.finish());
		timeout(n); // pass current waiter; it doesn't have a waiter yet, but it will soon, and its timeout should count
		return &n->prod;
	}
	
	void complete_recv(ssize_t len)
	{
		if (len >= 2)
		{
			uint16_t trid = readu_be16(recv_buf);
			waiter_node* n = node_for_trid(trid);
			if (n)
			{
				waiting.dealloc(n);
				bytesr domain_encoded = bytesr(n->send_buf+n->send_buf_domain_start, n->send_buf_domain_len);
				n->prod.complete(parse_reply(bytesr(recv_buf, len), domain_encoded, n->port));
			}
		}
		sock->recv(recv_buf).then(&recv_w);
	}
	
	void timeout(waiter_node* special)
	{
		timestamp expired = timestamp::now() - duration::ms(2000);
		timestamp first = timestamp::at_never();
		for (size_t i=0;i<waiting.size();i++) // don't foreach, it'll break if waiting grows during the loop
		{
			waiter_node& n = waiting.begin()[i];
			if (&n != special && !n.prod.has_waiter())
				continue;
			if (n.sent <= expired)
			{
				waiting.dealloc(&n);
				n.prod.complete({});
			}
			else
				first = min(first, n.sent);
		}
		if (first != timestamp::at_never() && !time_w.is_waiting())
			runloop2::await_timeout(first).then(&time_w);
	}
	
	void cancel(waiter_node* n)
	{
		waiting.dealloc(n);
	}
};

static bool get_label(bytestream& stream, bytesr& ret)
{
again:
	size_t last_pos = stream.tell();
	if (stream.remaining() < 1) return false;
	uint8_t byte = stream.u8();
	if (byte == 0)
	{
		return true;
	}
	else if ((byte & 0xC0) == 0x00)
	{
		size_t partlen = byte;
		if (stream.remaining() < partlen) return false;
		ret = stream.bytes(partlen);
		return true;
	}
	else if ((byte & 0xC0) == 0xC0)
	{
		if (stream.remaining() < 1) return false;
		size_t pos = (byte&0x3F) << 8 | stream.u8();
		if (pos >= last_pos)
			return false;
		stream.seek(pos);
		goto again; // I don't know if these compressed pointers can point to each other, I'll assume they can
	}
	else return false;
}

static bool same_name(bytestream first, bytestream second)
{
	size_t len_seen = 0;
	while (true)
	{
		bytesr l1;
		bytesr l2;
		if (!get_label(first, l1)) return false;
		if (!get_label(second, l2)) return false;
		if (l1 != l2) return false;
		len_seen += 1 + l1.size(); // max allowed length is 255 bytes encoded, with length prefix and nul terminator, aka 253 in text
		if (len_seen >= 255) return false; // this variable counts the prefix, but not the terminator
		if (!l1) return true;
	}
}

static bool skip_name(bytestream& stream)
{
	size_t maxpos = stream.tell();
	while (true)
	{
		if (stream.remaining() < 1) return false;
		uint8_t byte = stream.u8();
		if (byte == 0)
		{
			return true;
		}
		else if ((byte & 0xC0) == 0x00)
		{
			size_t partlen = byte;
			if (stream.remaining() < partlen) return false;
			stream.bytes(partlen);
		}
		else if ((byte & 0xC0) == 0xC0)
		{
			if (stream.remaining() < 1) return false;
			stream.u8();
			return true;
		}
		else return false;
	}
}

// Updates the second argument to point to after its encoded name. The first remains unchanged.
// Passing a bytesr instead of a bytestream for the first argument is safe only if it's known to not contain any backreferences.
static bool same_name_skip(bytestream first, bytestream& second)
{
	if (!same_name(first, second)) return false;
	skip_name(second);
	return true;
}

static socket2::address parse_reply(bytesr packet, bytesr domain_encoded, uint16_t port)
{
	//header:
	//4567 8180 0001 0001 0000 0000
	//
	//query:
	//13 676F6F676C652D7075626C69632D646E732D61
	//06 676F6F676C65
	//03 636F6D
	//00
	//0001 0001
	//
	//answer (RR):
	//C00C 0001 0001
	//00011857 0004 08080808
	
	bytestream stream = packet;
	if (stream.remaining() < 12) return {}; // can't fit dns header? fake packet, discard
	
	stream.u16b(); // trid already checked by await_lookup
	
	bytesr ret;
	
	if ((stream.u16b()&~0x0480) != 0x8100) return {}; // QR, RD (discard AA and RA)
	if (stream.u16b() != 0x0001) return {}; // QDCOUNT
	uint16_t ancount = stream.u16b(); // git.io gives eight different IPs
	if (ancount < 0x0001) return {}; // ANCOUNT
	uint16_t nscount = stream.u16b(); // NSCOUNT
	uint16_t arcount = stream.u16b(); // ARCOUNT
	
	//query
	if (!same_name_skip(domain_encoded, stream)) return {};
	if (stream.remaining() < 4) return {};
	if (stream.u16b() != 0x0001) return {}; // type A
	if (stream.u16b() != 0x0001) return {}; // class IN
	
	if (!same_name_skip(domain_encoded, stream)) return {};
again:
	if (stream.remaining() < 2+2+4+2) return {};
	
	uint16_t type = stream.u16b();
	if (stream.u16b() != 0x0001) return {}; // class IN
	stream.u32b(); // TTL, ignore
	
	if (type == 0x0005) // type CNAME (cnames can be stacked, so this must be a loop)
	{
		size_t namelen;
		namelen = stream.u16b();
		if (stream.remaining() < namelen) return {};
		bytestream cname = stream; // canonical name
		if (!skip_name(stream)) return {};
		
		ancount--;
		if (!same_name_skip(cname, stream)) return {}; // new relevant name
		goto again;
	}
	
	if (type != 1 && type != 28) return {}; // type A, AAAA
	
	size_t iplen = stream.u16b();
	if (stream.remaining() < iplen) return {};
	if (ancount==1 && nscount==0 && arcount==0 && stream.remaining() != iplen) return {};
	
	return { stream.bytes(iplen), port };
	
	//ignore remaining answers, as well as nscount and arcount
}

}

async<socket2::address> socket2::dns(cstring domain, uint16_t port)
{
	return get_dns()->await_lookup(domain, port);
}
void* socket2::dns_create() { return new dns_t; }
void socket2::dns_destroy(void* dns) { delete (dns_t*)dns; }

#include "test.h"
static void test1(cstring domain, bool expect)
{
	// some goofy tricks to test that the function does not read out of bounds
	size_t len = domain.length();
	autofree<uint8_t> domain_copy = xmalloc(len);
	memcpy(domain_copy, domain.bytes().ptr(), len);
	cstring domain_noterm = bytesr(domain_copy, len);
	
	uint8_t buf[256];
	uint16_t port;
	bytestreamw stream = bytesw(buf);
	assert_eq(encode_domain_name(domain_noterm, stream, &port), expect);
}
test("domain parsing", "", "")
{
	test1("muncher.se", true);
	test1("a", true);
	test1("", false);
	test1("muncher.se.", false); // trailing dot is theoretically legal, but I've never seen em outside RFCs and other network nerdery
	test1("muncher..se", false); // these two aren't legal even if you ask the nerds
	test1(".muncher.se", false);
	test1("floating_muncher.se", false); // only a-z0-9 and dash
	test1("Muncher.se", false); // case sensitive
	test1("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", true);
	test1("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", false);
	test1("a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a."
	      "a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a."
	      "a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a."
	      "a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a", true);
	test1("a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a."
	      "a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a."
	      "a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a."
	      "a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.ab", false);
	test1("127.0.0.1.com", true);
	test1("127.0.0.1", false); // must reject ip addresses
	test1("2001:0db8:85a3:0000:0000:8a2e:0370:7334", false); // including v6
	test1("abcd:1234", true); // looks a little like an ipv6 address, but it's just a TLD and a port
	test1("abcd:", false); // but any real ipv6s, as well as corrupt variants, must be rejected
	test1("abcd::", false);
	test1("abcd:65536", false); // port out of range (no need to list all corrupt ports, fromstring is already properly tested)
	test1("a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a." // I'll never see this one, but it's legal
	      "a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a."
	      "a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a."
	      "a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a:"
	      "0000000000000000000000000000000000000000000000000000000000012345", true);
}

test("dummy", "runloop", "udp") {} // there are no real udp tests, the dns test is enough. but something must provide udp
test("DNS", "udp,string,ipconv", "sockaddr")
{
	//test_skip("kinda slow");
	
	static int n_done;
	static int n_total;
	static bool is_sync;
	n_done = 0;
	n_total = 0;
	is_sync = true;
	
#define test1_port(domain, port, conds) \
	n_total++; \
	struct JOIN(wait_t,__LINE__) : public waiter_fn<socket2::address, JOIN(wait_t,__LINE__)> { \
		void complete(socket2::address addr) \
		{ \
			n_done++; \
			conds \
		} \
	} JOIN(wait,__LINE__); \
	socket2::dns(domain, port).then(&JOIN(wait,__LINE__))
#define test1(domain, conds) test1_port(domain, 0, conds)
	
#define sync assert(is_sync);
#define is_fail assert(!addr);
#define is_any assert(addr);
#define is_addr(expect) assert_eq(addr.as_str(), expect);
#define is_localhost if (addr.as_str() != "::1") assert_eq(addr.as_str(), "127.0.0.1");
#define port(n) assert_eq(addr.port(), n);
	
	// put this one outside test_nomalloc, first lookup initializes the allocatable_array (9th async grows it, there are currently 7)
	test1("google-public-dns-b.google.com", is_addr("8.8.4.4")); // use public-b only, to ensure IP isn't byteswapped
	test_nomalloc {
		test1("not-a-subdomain.google-public-dns-b.google.com", is_fail);
		test1("git.io", is_any); // this domain returns eight values in answer section
		test1("stacked.muncher.se", is_any); // this domain is a CNAME
		test1("devblogs.microsoft.com", is_any); // this domain is a CNAME to another CNAME to a third CNAME
		
		test1("", sync is_fail);
		test1("localhost", sync is_localhost);
		test1("127.0.0.1", sync is_addr("127.0.0.1"));
		test1("127.0.1", sync is_fail); // must reject corrupt ip addresses and not try to look them up
		test1("::1", sync is_addr("::1"));
		test1("[::1]", sync is_fail);
		
		test1_port("muncher.se", 80, is_any port(80));
		test1_port("localhost", 80, sync is_localhost port(80));
		test1_port("127.0.0.1", 80, sync is_addr("127.0.0.1") port(80));
		test1_port("[::1]", 80, sync is_addr("::1") port(80));
		test1_port("::1", 80, sync is_fail);
		
		test1_port("muncher.se:8080", 80, is_any port(8080));
		test1_port("localhost:8080", 80, sync is_localhost port(8080));
		test1_port("127.0.0.1:8080", 80, sync is_addr("127.0.0.1") port(8080));
		test1_port("[::1]:8080", 80, sync is_addr("::1") port(8080));
		
		is_sync = false;
		
		while (n_done != n_total)
			runloop2::step();
	}
}
#endif
