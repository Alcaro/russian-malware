#ifdef ARLIB_SOCKET
#include "dns.h"
#include "bytestream.h"
#include "set.h"
#include "stringconv.h"
#include "random.h"

#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#endif

// could swap this entire object for DnsQueryEx, but that needs windows 8
string DNS::default_resolver()
{
#ifdef __unix__
	return ("\n"+file::readall("/etc/resolv.conf")).split<1>("\nnameserver ")[1].split<1>("\n")[0];
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
			return info2->DnsServerList.IpAddress.String;
		}
		else return "";
	}
	if (status == ERROR_SUCCESS) return info.DnsServerList.IpAddress.String;
	else return "";
	//*/
#endif
}

void DNS::init(cstring resolver, int port, runloop* loop)
{
	this->loop = loop;
	this->sock = socket::create_udp(resolver, port, loop);
	sock->callback(bind_this(&DNS::sock_cb), NULL);
	
#ifdef __unix__
	for (string line : file::readallt("/etc/hosts").split("\n"))
#else
	hosts_txt.insert("localhost", "127.0.0.1"); // commented out and hardcoded in default hosts file
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
			
			hosts_txt.insert(line.substr(domstart, i), addr);
		}
	}
}

uint16_t DNS::pick_trid()
{
	while (true)
	{
		uint16_t n = g_rand.rand32();
		if (!queries.contains(n)) return n;
	}
}

void DNS::resolve(cstring domain, unsigned timeout_ms, function<void(string domain, string ip)> callback)
{
	if (!domain) return callback(domain, "");
	
	uint8_t discard[16];
	if (socket::string_to_ip(discard, domain)) return callback(domain, domain);
	
	string* hosts_result = hosts_txt.get_or_null(domain);
	if (hosts_result) return callback(domain, *hosts_result);
	
#ifdef ARLIB_TEST
	assert_ne(domain, "localhost"); // must be resolved from hosts file
#endif
	
	bytestreamw packet;
	
	uint16_t trid = pick_trid();
	packet.u16b(trid);
	
	uint16_t flags = 0;
	flags |= 0<<15; // QR, 'is response' flag
	flags |= 0<<11; // OPCODE, 4 bits; 0 = normal query
	flags |= 0<<10; // AA, 'is authorative' flag
	flags |= 0<<9; // TC, 'answer truncated' flag
	flags |= 1<<8; // RD, recursion desired
	flags |= 0<<7; // RA, recursion available
	flags |= 0<<4; // Z, 3 bits, reserved
	flags |= 0<<0; // RCODE, 4 bits; 0 = no error
	packet.u16b(flags);
	
	packet.u16b(1); // QDCOUNT
	packet.u16b(0); // ANCOUNT
	packet.u16b(0); // NSCOUNT
	packet.u16b(0); // ARCOUNT
	
	for (cstring cs : domain.csplit("."))
	{
		packet.u8(cs.length());
		packet.text(cs);
	}
	packet.u8(0);
	
	packet.u16b(0x0001); // type A (could switch to 0x00FF Everything, but I can't test ipv6 so let's not ask for it)
	packet.u16b(0x0001); // class IN
	//judging by musl libc, there's no way to ask for both ipv4 and ipv6 but not everything else, it sends two separate queries
	
	sock->send(packet.finish());
	
	query& q = queries.get_create(trid);
	q.callback = callback;
	q.domain = domain;
	q.timeout_id = loop->raw_set_timer_once(timeout_ms, bind_lambda([this,trid]() { this->timeout(trid); }));
}

string DNS::read_name(bytestream& stream)
{
	string ret;
	size_t restorepos = 0;
	size_t maxpos = stream.tell();
	while (true)
	{
		if (stream.remaining() < 1) return "";
		uint8_t byte = stream.u8();
		if(0);
		else if ((byte & 0xC0) == 0x00)
		{
			if (!byte)
			{
				if (restorepos != 0) stream.seek(restorepos);
				return ret;
			}
			
			size_t partlen = byte;
			if (stream.remaining() < partlen) return "";
			if (ret != "") ret += ".";
			ret += stream.bytes(partlen);
			if (ret.length() >= 256) return "";
		}
		else if ((byte & 0xC0) == 0xC0)
		{
			if (stream.remaining() < 1) return "";
			size_t pos = (byte&0x3F) << 8 | stream.u8();
			
			if (restorepos == 0) restorepos = stream.tell();
			
			if (pos >= maxpos) return ""; // block infinite loops
			maxpos = pos;
			
			stream.seek(pos);
		}
		else return "";
	}
}

void DNS::timeout(uint16_t trid)
{
	query q = std::move(queries.get(trid));
	queries.remove(trid);
	loop->raw_timer_remove(q.timeout_id);
	q.callback(std::move(q.domain), ""); // don't move higher, callback could delete the dns object
}

void DNS::sock_cb()
{
	if (!sock) return;
	uint8_t packet[512]; // max size of unextended DNS packet
	int nbytes = sock->recv(packet);
	if (nbytes < 0)
	{
		//don't inline, callback could delete the dns object
		map<uint16_t, query> l_queries = std::move(this->queries);
		//runloop* l_loop = this->loop; // not needed
		sock = NULL;
		
		for (auto& pair : l_queries)
		{
			pair.value.callback(pair.value.domain, "");
		}
		return;
	}
	if (nbytes == 0) return;
	arrayview<uint8_t> bytes(packet, nbytes);
	bytestream stream = bytes;
	
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
	
	if (stream.remaining() < 12) return; // can't fit dns header? fake packet, discard
	
	uint16_t trid = stream.u16b();
	if (!queries.contains(trid)) return; // possible if the timeout was hit already, or whatever
	query& q = queries.get(trid);
	
	string ret = "";
	
	{
	if ((stream.u16b()&~0x0080) != 0x8100) goto fail; // QR, RD, RA (RA optional)
	if (stream.u16b() != 0x0001) goto fail; // QDCOUNT
	uint16_t ancount = stream.u16b(); // git.io gives eight different IPs
	if (ancount < 0x0001) goto fail; // ANCOUNT
	uint16_t nscount = stream.u16b(); // NSCOUNT
	uint16_t arcount = stream.u16b(); // ARCOUNT
	
	//query
	if (read_name(stream) != q.domain) goto fail;
	if (stream.remaining() < 4) return;
	if (stream.u16b() != 0x0001) goto fail; // type A
	if (stream.u16b() != 0x0001) goto fail; // class IN
	
	if (read_name(stream) != q.domain) goto fail;
again:
	if (stream.remaining() < 2+2+4+2) return;
	
	uint16_t type = stream.u16b();
	if (stream.u16b() != 0x0001) goto fail; // class IN
	stream.u32b(); // TTL, ignore
	
	if (type == 0x0005) // type CNAME (cnames can be stacked, so this must be a loop)
	{
		size_t namelen;
		namelen = stream.u16b();
		if (stream.remaining() < namelen) goto fail;
		string cname = read_name(stream); // canonical name
		
		ancount--;
		string nextrecord = read_name(stream); // new relevant name
		if (nextrecord != cname) goto fail;
		goto again;
	}
	
	if (type != 0x0001) goto fail; // type A
	
	size_t iplen = stream.u16b();
	if (stream.remaining() < iplen) goto fail;
	if (ancount==1 && nscount==0 && arcount==0 && stream.remaining() != iplen) goto fail;
	
	ret = socket::ip_to_string(stream.bytes(iplen));
	
	//ignore remaining answers, as well as nscount and arcount
	
	}
fail:
	function<void(string domain, string ip)> callback = q.callback;
	string q_domain = std::move(q.domain);
	loop->raw_timer_remove(q.timeout_id);
	queries.remove(trid);
	
	callback(std::move(q_domain), std::move(ret)); // don't move higher, callback could delete the dns object
}

#include "test.h"
test("dummy", "runloop", "udp") {} // there are no real udp tests, the dns test is enough. but something must provide udp
test("DNS", "udp,string,ipconv", "dns")
{
	test_skip("kinda slow");
	
	autoptr<runloop> loop = runloop::create();
	
	assert(isdigit(DNS::default_resolver()[0])); // TODO: fails on IPv6 ::1
	
	DNS dns(loop);
	int n_done = 0;
	int n_total = 0;
	n_total++; // dummy addition to ensure n_done != n_total prior to loop->enter
	n_total++; dns.resolve("google-public-dns-b.google.com", bind_lambda([&](string domain, string ip)
		{
			n_done++; if (n_done == n_total) loop->exit(); // put this above assert, otherwise it deadlocks
			assert_eq(domain, "google-public-dns-b.google.com");
			assert_eq(ip, "8.8.4.4"); // use public-b only, to ensure IP isn't byteswapped
		}));
	n_total++; dns.resolve("not-a-subdomain.google-public-dns-a.google.com", bind_lambda([&](string domain, string ip)
		{
			n_done++; if (n_done == n_total) loop->exit();
			assert_eq(domain, "not-a-subdomain.google-public-dns-a.google.com");
			assert_eq(ip, "");
		}));
	n_total++; dns.resolve("git.io", bind_lambda([&](string domain, string ip)
		{
			n_done++; if (n_done == n_total) loop->exit();
			assert_eq(domain, "git.io");
			assert_ne(ip, ""); // this domain returns eight values in answer section, must be parsed properly
		}));
	n_total++; dns.resolve("stacked.muncher.se", bind_lambda([&](string domain, string ip)
		{
			n_done++; if (n_done == n_total) loop->exit();
			assert_eq(domain, "stacked.muncher.se");
			assert_ne(ip, ""); // this domain is a CNAME
		}));
	n_total++; dns.resolve("devblogs.microsoft.com", bind_lambda([&](string domain, string ip)
		{
			n_done++; if (n_done == n_total) loop->exit();
			assert_eq(domain, "devblogs.microsoft.com");
			assert_ne(ip, ""); // this domain is a CNAME to another CNAME to a third CNAME
		}));
	n_total++; dns.resolve("", bind_lambda([&](string domain, string ip) // this must fail
		{
			n_done++; if (n_done == n_total) loop->exit();
			assert_eq(domain, "");
			assert_eq(ip, "");
		}));
	n_total++; dns.resolve("localhost", bind_lambda([&](string domain, string ip)
		{
			n_done++; if (n_done == n_total) loop->exit();
			assert_eq(domain, "localhost");
			// silly way to say 'can be either of those, but must be one of them'
			if (ip != "::1") assert_eq(ip, "127.0.0.1");
		}));
	n_total++; dns.resolve("127.0.0.1", bind_lambda([&](string domain, string ip)
		{
			n_done++; if (n_done == n_total) loop->exit();
			assert_eq(domain, "127.0.0.1");
			//if it's already an IP, it must remain an IP
			assert_eq(ip, "127.0.0.1");
		}));
	//an invalid IP
	n_total++; dns.resolve("127.0.0.", bind_lambda([&](string domain, string ip)
		{
			n_done++; if (n_done == n_total) loop->exit();
			assert_eq(domain, "127.0.0.");
			assert_eq(ip, "");
		}));
	n_total--; // remove dummy addition
	
	if (n_done != n_total) loop->enter();
}
// TODO: use a single dns object per runloop, and add a cache
#endif
