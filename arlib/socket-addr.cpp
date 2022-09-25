#ifdef ARLIB_SOCKET
#include "socket.h"
#ifdef __unix__
#include <arpa/inet.h>
// musl prints as ipv4 if the address matches 0:0:0:0:0:ffff:?:?
// glibc does the same, but also if the address matches 0:0:0:0:0:0:?:? but not 0:0:0:0:0:0:0:?
// variable behavior is useless, let's just roll our own
//#define USE_INET_NTOP
// this one is, to my knowledge, well behaved
#define USE_INET_PTON
#endif
#ifdef _WIN32
#include <winsock2.h> // sockaddr_in, sockaddr_storage
#include <ws2ipdef.h> // sockaddr_in6
#define INET6_ADDRSTRLEN 46
#endif

// testing glibc is uninteresting
#ifdef ARLIB_TEST
#undef USE_INET_NTOP
#undef USE_INET_PTON
#endif

cstring socket2::address::split_port(cstring host, uint16_t* port)
{
	if (!port)
		return host;
	
	if (host.startswith("["))
	{
		ssize_t split = host.lastindexof("]");
		if (split < 0)
			return "";
		if ((size_t)host.indexof(":") > (size_t)split)
			return "";
		
		if (split != host.length()-1)
		{
			if (host[split+1] != ':' || !fromstring(host.substr(split+2, ~0), *port))
				return "";
		}
		return host.substr(1, split);
	}
	
	ssize_t split = host.indexof(":");
	if (split < 0)
		return host;
	
	if (!fromstring(host.substr(split+1, ~0), *port))
		return "";
	return host.substr(0, split);
}

static bool parse_ipv4_raw(const char * addr, uint8_t* out)
{
#ifdef USE_INET_PTON
	return inet_pton(AF_INET, addr, out);
#else
	auto read_number = [](const char * & ptr, uint8_t& out) -> bool {
		if (!isdigit(*ptr))
			return false;
		out = 0;
		if (*ptr == '0')
		{
			ptr++;
			return true;
		}
		
		int ret = *(ptr++) - '0';
		while (isdigit(*ptr))
		{
			ret = (ret*10) + *(ptr++) - '0';
			if (ret >= 256)
				return false;
		}
		out = ret;
		return true;
	};
	
	return (read_number(addr, out[0]) && *addr++ == '.' &&
	        read_number(addr, out[1]) && *addr++ == '.' &&
	        read_number(addr, out[2]) && *addr++ == '.' &&
	        read_number(addr, out[3]) && *addr++ == '\0');
#endif
}

static bool parse_ipv6_raw(const char * addr, uint8_t* out)
{
#ifdef USE_INET_PTON
	return inet_pton(AF_INET6, addr, out);
#else
	auto read_segment_ipv6 = [](const char * start, const char * end, int max, bool allow_v4, uint8_t * out) -> int
	{
		if (start == end)
			return 0;
		int n_parts = 0;
		
		while (true)
		{
			size_t num_len = 0;
			while (isxdigit(start[num_len]))
				num_len++;
			if (num_len == 0 || num_len > 4)
				return -1;
			
			n_parts++;
			if (n_parts > max)
				return -1;
			
			uint16_t num;
			fromstringhex_ptr(start, num_len, num); // known to succeed
			out[0] = num>>8;
			out[1] = num&255;
			out += 2;
			
			start += num_len;
			if (start == end)
			{
				return n_parts;
			}
			else if (*start == ':')
			{
				start++;
				continue;
			}
			else if (*start == '.')
			{
				if (allow_v4 && n_parts+1 <= max && parse_ipv4_raw(start-num_len, out-2))
					return n_parts+1;
				else
					return -1;
			}
			else return -1;
		}
	};
	
	const char * end = strchr(addr, '\0');
	const char * middle = strstr(addr, "::");
	if (!middle)
		return (read_segment_ipv6(addr, end, 8, true, out) == 8);
	
	uint8_t right_bytes[16];
	
	int size_left = read_segment_ipv6(addr, middle, 7, false, out);
	if (size_left < 0)
		return false;
	int size_right = read_segment_ipv6(middle+2, end, 7-size_left, true, right_bytes);
	
	if (size_right >= 0)
	{
		memcpy(out+16-size_right*2, right_bytes, size_right*2);
		return true;
	}
	return false;
#endif
}

bool socket2::address::parse_ipv4(cstring host, bytesw out, uint16_t* port)
{
	if (host.contains_nul())
		return false;
	return parse_ipv4_raw(socket2::address::split_port(host, port).c_str(), out.ptr());
}
bool socket2::address::parse_ipv6(cstring host, bytesw out, uint16_t* port)
{
	if (host.contains_nul())
		return false;
	return parse_ipv6_raw(socket2::address::split_port(host, port).c_str(), out.ptr());
}

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
cstring socket2::address::parse_domain(cstring host, uint16_t* port)
{
	bytesr by = host.bytes();
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
			return "";
		
		size_t labellen = it-labelstart;
		if (labellen == 0 || labellen > 63)
			return "";
		
		if (it == end)
		{
			if (isalpha(*labelstart))
				return host;
			return "";
		}
		
		char sep = *it++;
		if (sep == '.')
		{
			labelstart = it;
		}
		else if (sep == ':')
		{
			if (isalpha(*labelstart) && port && fromstring(cstring(bytesr(it, end-it)), *port))
				return bytesr(start, it-1-start);
			return "";
		}
		else return "";
	}
}

socket2::address::address(bytesr by, uint16_t port)
{
	sockaddr* sa = as_native();
	if (by.size() == 0)
	{
		sa->sa_family = AF_UNSPEC;
		return;
	}
	if (by.size() == 4)
	{
		sockaddr_in* sin = (sockaddr_in*)sa;
		memset(sin, 0, sizeof(sockaddr_in));
		sin->sin_family = AF_INET;
		sin->sin_port = htons(port);
		memcpy(&sin->sin_addr.s_addr, by.ptr(), 4);
		return;
	}
	if (by.size() == 16)
	{
		sockaddr_in6* sin6 = (sockaddr_in6*)sa;
		memset(sin6, 0, sizeof(sockaddr_in6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = htons(port);
		memcpy(&sin6->sin6_addr.s6_addr, by.ptr(), 16);
		return;
	}
	__builtin_trap();
}
socket2::address::address(cstring str, uint16_t port)
{
	as_native()->sa_family = AF_UNSPEC;
	
	if (str.contains_nul())
		return;
	
	str = socket2::address::split_port(str, port ? &port : nullptr);
	
	char str_nul[INET6_ADDRSTRLEN];
	if (str.length() >= INET6_ADDRSTRLEN)
		return;
	memcpy(str_nul, str.bytes().ptr(), str.length());
	str_nul[str.length()] = '\0';
	
	sockaddr_in* sin = (sockaddr_in*)as_native();
	memset(sin, 0, sizeof(sockaddr_in));
	if (parse_ipv4_raw(str_nul, (uint8_t*)&sin->sin_addr))
	{
		sin->sin_family = AF_INET;
		sin->sin_port = htons(port);
		return;
	}
	
	sockaddr_in6* sin6 = (sockaddr_in6*)as_native();
	memset(sin6, 0, sizeof(sockaddr_in6));
	if (parse_ipv6_raw(str_nul, sin6->sin6_addr.s6_addr))
	{
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = htons(port);
		return;
	}
}

socket2::address::operator bool()
{
	sockaddr* sa = as_native();
	return (sa->sa_family != PF_UNSPEC);
}

bytesr socket2::address::as_bytes()
{
	sockaddr* sa = as_native();
	if (sa->sa_family == AF_UNSPEC)
	{
		return nullptr;
	}
	if (sa->sa_family == AF_INET)
	{
		sockaddr_in* sin = (sockaddr_in*)sa;
		return { (uint8_t*)&sin->sin_addr.s_addr, 4 };
	}
	if (sa->sa_family == AF_INET6)
	{
		sockaddr_in6* sin6 = (sockaddr_in6*)sa;
		return sin6->sin6_addr.s6_addr;
	}
	__builtin_trap();
}

#ifdef USE_INET_NTOP
string socket2::address::as_str()
{
	char ret[INET6_ADDRSTRLEN];
	sockaddr* sa = as_native();
	if (sa->sa_family == AF_UNSPEC)
	{
		return "";
	}
	if (sa->sa_family == AF_INET)
	{
		sockaddr_in* sin = (sockaddr_in*)sa;
		return inet_ntop(AF_INET, (uint8_t*)&sin->sin_addr.s_addr, ret, INET6_ADDRSTRLEN);
	}
	if (sa->sa_family == AF_INET6)
	{
		sockaddr_in6* sin6 = (sockaddr_in6*)sa;
		return inet_ntop(AF_INET6, sin6->sin6_addr.s6_addr, ret, INET6_ADDRSTRLEN);
	}
	__builtin_trap();
}
#else
string socket2::address::as_str()
{
	sockaddr* sa = as_native();
	if (sa->sa_family == AF_UNSPEC)
	{
		return "";
	}
	if (sa->sa_family == AF_INET)
	{
		sockaddr_in* sin = (sockaddr_in*)sa;
		bytesr bytes = { (uint8_t*)&sin->sin_addr.s_addr, 4 };
		return format(bytes[0],".",bytes[1],".",bytes[2],".",bytes[3]);
	}
	if (sa->sa_family == AF_INET6)
	{
		sockaddr_in6* sin6 = (sockaddr_in6*)sa;
		bytesr bytes = sin6->sin6_addr.s6_addr;
		
		if (bytes.slice(0,12) == bytesr((uint8_t*)"\0\0\0\0\0\0\0\0\0\0\xFF\xFF", 12))
		{
			return format("::ffff:",bytes[12],".",bytes[13],".",bytes[14],".",bytes[15]);
		}
		
		char ret[40]; // INET6_ADDRSTRLEN is 46, but max output from this function is 39 (but we need an extra byte of buffer space)
		size_t pos = sizeof(ret);
		
		size_t best_start = 0;
		size_t best_end = 0;
		size_t cur_end = pos;
		
		for (int i=7;i>=0;i--)
		{
			static const char hexdigits_lower[] = { '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f' };
			uint16_t val = bytes[i*2]<<8 | bytes[i*2+1];
			uint16_t val_orig = val;
			do {
				ret[--pos] = hexdigits_lower[val&15];
				val >>= 4;
			} while (val);
			ret[--pos] = ':';
			if (val_orig == 0 && cur_end-pos >= best_end-best_start)
			{
				best_start = pos;
				best_end = cur_end;
			}
			if (val_orig != 0)
				cur_end = pos;
		}
		pos++;
		
		if (best_end-best_start >= 3)
		{
			if (best_start < pos)
				best_start++;
			if (best_end < sizeof(ret)-1)
				best_end++;
			
			ret[best_start+0] = ':';
			ret[best_start+1] = ':';
			memmove(ret+best_start+2, ret+best_end, sizeof(ret)-best_end);
			return bytesr((uint8_t*)ret+pos, (best_start-pos)+2+(sizeof(ret)-best_end));
		}
		return bytesr((uint8_t*)ret+pos, sizeof(ret)-pos);
	}
	__builtin_trap();
}
#endif


uint16_t socket2::address::port()
{
	sockaddr* sa = as_native();
	if (sa->sa_family == AF_INET)
	{
		sockaddr_in* sin = (sockaddr_in*)sa;
		return ntohs(sin->sin_port);
	}
	if (sa->sa_family == AF_INET6)
	{
		sockaddr_in6* sin6 = (sockaddr_in6*)sa;
		return ntohs(sin6->sin6_port);
	}
	return 0;
}

void socket2::address::set_port(uint16_t port)
{
	sockaddr* sa = as_native();
	if (sa->sa_family == AF_INET)
	{
		sockaddr_in* sin = (sockaddr_in*)sa;
		sin->sin_port = htons(port);
	}
	if (sa->sa_family == AF_INET6)
	{
		sockaddr_in6* sin6 = (sockaddr_in6*)sa;
		sin6->sin6_port = htons(port);
	}
}

#include "test.h"
static void test1(cstrnul in, uint16_t port_in, cstrnul expect, uint16_t port_expect)
{
	socket2::address addr { in, port_in };
	assert_eq(addr.as_str(), expect);
	if (expect)
		assert_eq(addr.port(), port_expect);
}
static void test1(cstrnul in, cstrnul expect)
{
	test1(in, 0, expect, 0);
}
test("socket address", "", "sockaddr")
{
	testcall(test1("", ""));
	testcall(test1("127.0.0.1", "127.0.0.1"));
	testcall(test1("127.0.0.", "")); // corrupt addresses
	testcall(test1("127.0.0.1.2", ""));
	
	testcall(test1("127.0.1", "")); // goofy legacy formats, should also be considered corrupt in this year
	testcall(test1("127.1", ""));
	testcall(test1("2130706433", ""));
	testcall(test1("0127.0.0.1", ""));
	testcall(test1("127.00.0.1", ""));
	testcall(test1("127.0.00.1", ""));
	testcall(test1("127.0.0.01", ""));
	testcall(test1("0x7f.0.0.1", ""));
	
	testcall(test1("2001:0db8:85a3:0000:0000:8a2e:0370:7334", "2001:db8:85a3::8a2e:370:7334")); // common example address, where's it from?
	testcall(test1("2001:db8::1:0:0:0:1", "2001:db8:0:1::1"));
	testcall(test1("2001:db8:0::1:0:0:0:1", ""));
	testcall(test1("1:2:3:4:5:6:7:8", "1:2:3:4:5:6:7:8"));
	testcall(test1("0:2:3:4:5:6:7:8", "0:2:3:4:5:6:7:8"));
	testcall(test1("0:0:3:4:5:6:7:8", "::3:4:5:6:7:8"));
	testcall(test1("0:0:0:4:5:6:7:8", "::4:5:6:7:8"));
	testcall(test1("0:0:0:0:0:0:0:0", "::"));
	testcall(test1("1:2:3:4:5:6:7:0", "1:2:3:4:5:6:7:0"));
	testcall(test1("1:2:3:4:5:6:0:0", "1:2:3:4:5:6::"));
	testcall(test1("1:2:3:4:5:0:0:0", "1:2:3:4:5::"));
	testcall(test1("1:2:3:0:5:6:7:8", "1:2:3:0:5:6:7:8"));
	testcall(test1("1:2:0:0:5:6:7:8", "1:2::5:6:7:8"));
	testcall(test1("1:2:0:0:5:0:0:8", "1:2::5:0:0:8"));
	testcall(test1("1:0:0:0:5:0:0:8", "1::5:0:0:8"));
	testcall(test1("1:2:0:0:5:0:0:0", "1:2:0:0:5::"));
	testcall(test1("::", "::"));
	testcall(test1("::1", "::1"));
	testcall(test1(":::1", ""));
	testcall(test1("1:::", ""));
	testcall(test1("1:::1", ""));
	testcall(test1(":::", ""));
	testcall(test1("1::", "1::"));
	testcall(test1("::0001", "::1"));
	testcall(test1("::00001", ""));
	testcall(test1("::1:", ""));
	testcall(test1("1::2::3", ""));
	testcall(test1("1:2:3:4:5:6:7::", "1:2:3:4:5:6:7:0"));
	testcall(test1("::1:2:3:4:5:6:7", "0:1:2:3:4:5:6:7"));
	testcall(test1("1111:2222:3333:4444:5555:6666:7777", ""));
	testcall(test1("1111:2222:3333:4444:5555:6666:7777:8888", "1111:2222:3333:4444:5555:6666:7777:8888"));
	testcall(test1("1111:2222:3333:4444:5555:6666:7777:8888:9999", ""));
	testcall(test1("2001:0db8:85a3:00000:0000:8a2e:0370:7334", "")); // https://datatracker.ietf.org/doc/html/rfc4291#section-2.2
	testcall(test1("2001:0db8:85a3:0000:0000:08a2e:0370:7334", "")); // says 1 to 4 digits per segment
	testcall(test1("2001:0db8:85a3:0000:0000:8a2e:00370:7334", ""));
	testcall(test1("::ffff:127.0.0.1", "::ffff:127.0.0.1"));
	testcall(test1("::ffff:0:127.0.0.1", "::ffff:0:7f00:1"));
	testcall(test1("::fffe:127.0.0.1", "::fffe:7f00:1"));
	testcall(test1("::1:127.0.0.1", "::1:7f00:1"));
	testcall(test1("::127.0.0.1", "::7f00:1"));
	testcall(test1("::0.1.0.0", "::1:0"));
	testcall(test1("::0.0.255.255", "::ffff"));
	testcall(test1("1::127.0.0.1", "1::7f00:1"));
	testcall(test1("ffff::127.0.0.1", "ffff::7f00:1"));
	testcall(test1("ffff::ffff:127.0.0.1", "ffff::ffff:7f00:1"));
	testcall(test1("1111:2222:3333:4444:5555:6666:123.45.67.89", "1111:2222:3333:4444:5555:6666:7b2d:4359"));
	testcall(test1("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
	testcall(test1("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
	testcall(test1("ffff:ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255", ""));
	testcall(test1("ffff:ffff:ffff:ffff:ffff:ffff::255.255.255.255", ""));
	testcall(test1("ffff:ffff::ffff:ffff:ffff:ffff:255.255.255.255", ""));
	testcall(test1("::ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255", ""));
	testcall(test1("::ffff:ffff:ffff:ffff:ffff:255.255.255.255", "0:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
	testcall(test1("ffff:ffff:ffff:ffff:ffff::255.255.255.255", "ffff:ffff:ffff:ffff:ffff:0:ffff:ffff"));
	testcall(test1("ffff:ffff:255.255.255.255::ffff", ""));
	// fe80 addresses can take an optional flow info, for link-local addresses.
	// As far as I can determine, flow info, and the entire concept of link-local address,
	//  is useful only for implementing DHCP and similar low-level stuff, and has no place in Arlib.
	testcall(test1("fe80::%lo", ""));
	testcall(test1("::1"+string::nul(), ""));
	
	testcall(test1("127.0.0.1:8080", 80, "127.0.0.1", 8080));
	testcall(test1("2001:0db8:85a3:0000:0000:8a2e:0370:7334", 80, "", 0)); // ipv6 addresses without brackets are illegal if port is expected
	testcall(test1("[2001:0db8:85a3:0000:0000:8a2e:0370:7334]", 80, "2001:db8:85a3::8a2e:370:7334", 80));
	testcall(test1("[2001:0db8:85a3:0000:0000:8a2e:0370:7334]:8080", 80, "2001:db8:85a3::8a2e:370:7334", 8080));
	testcall(test1("[ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255]", 80, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 80));
	testcall(test1("[ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255]:12345", 80, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 12345));
	testcall(test1("127.0.0.1:0000000000000000000000000000000000000012345", 80, "127.0.0.1", 12345));
	testcall(test1("[127.0.0.1]", 80, "", -1));
	testcall(test1("1:2:3:4:5:6:7:8:0", 80, "", -1));
}
#endif
