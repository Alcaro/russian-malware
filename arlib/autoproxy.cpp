#ifdef ARLIB_SOCKET
#ifdef __linux__
# if __has_include(<gio/gio.h>)
#  include <gio/gio.h>
#  define BACKEND_GSETTINGS
# else
#  define BACKEND_GVDB
#  define NEED_RELOAD
# endif
#endif
#include "autoproxy.h"
#include "file.h"
#include "bytestream.h"
#include "socks5.h"
#include "inotify.h"
#include "thread.h"

namespace {
#ifdef BACKEND_GVDB
// this object trusts the input database, and does not protect against buffer overflows and other misbehavior
class gvdb {
	file2::mmap_t map;
	
	struct gvdb_item {
		uint32_t hash;
		uint32_t parent;
		uint32_t key_pos;
		uint16_t key_len;
		char type;
		char padding;
		uint32_t val_pos;
		uint32_t val_end; // why is this one not length
	};
	arrayview<gvdb_item> items;
	
public:
	gvdb(cstrnul fn)
	{
		map = file2::mmap(fn);
		
		bytestream by = map;
		if (!by.signature("GVariant"))
			return;
		if (by.u32l() != 0)
			return;
		if (by.u32l() != 0)
			return;
		if (by.u32l() != 0x18)
			return;
		
		uint32_t items_end = by.u32l();
		uint32_t bloom = by.u32l() - 0x28000000; // no clue what this offset is
		uint32_t buckets = by.u32l();
		
		size_t items_start = 0x20 + sizeof(uint32_t)*bloom + sizeof(uint32_t)*buckets;
		static_assert(END_LITTLE); // transmuting stuff like this is endian dependent
		items = map.slice(items_start, items_end-items_start).transmute<gvdb_item>();
	}
	
private:
	// for type 'v', returns a G_VARIANT_TYPE_VARIANT serialized object
	// for 'L', it seems to list the key's children in some format I didn't investigate
	const gvdb_item * get_raw(cstring key, char type)
	{
		uint32_t exp_hash = 5381;
		for (int8_t b : key.bytes())
			exp_hash = exp_hash*33 + b;
		
		for (size_t n=0;n<items.size();n++)
		{
			const gvdb_item & item = items[n];
			
			if (item.hash != exp_hash)
				continue;
			
			bytesr key_remain = key.bytes();
			uint32_t key_at = n;
			while (key_remain)
			{
				const gvdb_item & item = items[key_at];
				bytesr fragment = map.slice(item.key_pos, item.key_len);
				size_t key_remain_n = key_remain.size() - fragment.size();
				if (fragment.size() > key_remain.size() || fragment != key_remain.skip(key_remain_n))
					goto not_this;
				key_at = item.parent;
				key_remain = key_remain.slice(0, key_remain_n);
			}
			if (key_at != 0xFFFFFFFF)
				goto not_this;
			if (item.type != type)
				goto not_this;
			
			return &item;
			
		not_this: ;
		}
		
		return nullptr;
	}
public:
	
	// returns a G_VARIANT_TYPE_VARIANT type serialized object
	bytesr get(cstring key)
	{
		const gvdb_item * item = get_raw(key, 'v');
		if (!item) return nullptr;
		return map.slice(item->val_pos, item->val_end - item->val_pos);
	}
	
	// parses various GVariant types
	static int32_t parse_int_variant(bytesr by, int32_t fallback = 0)
	{
		if (by.size() == 6 && by[by.size()-2] == '\0' && by[by.size()-1] == 'i')
			return readu_le32(by.ptr()+0);
		return fallback;
	}
	static cstring parse_str_variant(bytesr by, cstring fallback = "")
	{
		if (by.size() >= 3 && by[by.size()-2] == '\0' && by[by.size()-1] == 's')
			return by.slice(0, by.size()-3); // -3 because it's nul terminated
		else return fallback;
	}
	static void parse_str_array(bytesr by, array<cstring>& out)
	{
		// string arrays are serialized in a quite complicated way
		// first, all contents are concatenated, with no separator (except the strings' nul terminators)
		// then, the end position of each element is stored, using u8 u16 u32 or u64
		//   smallest possible that can store full array size, including said end positions
		// the array length and pointer type are not stored directly
		// former can be found by checking size of array object
		// latter by reading last element of the pointer array, subtracting from full object size, and dividing by pointer size
		// an empty array is serialized to an empty byte sequence
		
		if (!by) return;
		
		auto read_int = [](const uint8_t * ptr, size_t size) -> size_t {
			if (size == 0) return readu_le8(ptr);
			if (size == 1) return readu_le16(ptr);
			if (size == 2) return readu_le32(ptr);
			return readu_le64(ptr);
		};
		
		size_t ptr_size = ilog2(by.size()|128) - 7;
		size_t ptrs_start = read_int(by.skip(by.size() - (1<<ptr_size)).ptr(), ptr_size);
		size_t n = (by.size()-ptrs_start) >> ptr_size;
		
		size_t at = 0;
		for (size_t i=0;i<n;i++)
		{
			size_t next = read_int(by.skip(ptrs_start + (i << ptr_size)).ptr(), ptr_size);
			out.append(by.slice(at, next-at-1));
			at = next;
		}
	}
	static bool parse_str_array_variant(bytesr by, array<cstring>& out)
	{
		if (by.size() >= 3 && by[by.size()-3] == '\0' && by[by.size()-2] == 'a' && by[by.size()-1] == 's')
		{
			parse_str_array(by.slice(0, by.size()-3), out);
			return true;
		}
		return false;
	}
	
	// just combines the above
	int32_t get_int(cstring key, int32_t fallback = 0) { return parse_int_variant(get(key), fallback); }
	cstring get_str(cstring key, cstring fallback = "") { return parse_str_variant(get(key), fallback); }
	bool get_str_array(cstring key, array<cstring>& out) { return parse_str_array_variant(get(key), out); }
};
#endif

class proxy_config {
public:
	array<string> ignore_proxy_domains;
	array<socket2::netmask> ignore_proxy_masks;
	
	string socks_addr;
	uint16_t socks_port;
	
	mutex mut;
#ifdef BACKEND_GSETTINGS
	GSettings* gs_proxy;
	GSettings* gs_socks;
#endif
#ifdef BACKEND_GVDB
	inotify ino;
#endif
	
	proxy_config()
	{
#ifdef BACKEND_GSETTINGS
		gs_proxy = g_settings_new("org.gnome.system.proxy");
		gs_socks = g_settings_new("org.gnome.system.proxy.socks");
		auto change_event = decompose_lambda([this](char* key, GSettings* self) -> gboolean {
			// no point worrying about what exactly changed, just reload everything
			mutexlocker lk(this->mut);
			this->reload();
			return false;
		});
		g_signal_connect_swapped(gs_proxy, "changed", G_CALLBACK(change_event.fp), change_event.ctx);
		g_signal_connect_swapped(gs_socks, "changed", G_CALLBACK(change_event.fp), change_event.ctx);
#endif
#ifdef BACKEND_GVDB
		ino.add(file2::dir_config()+"dconf/user", [this](cstring fn){ mutexlocker lk(this->mut); this->reload(); });
#endif
		reload();
	}
	
	// Must be called with the mutex locked.
	void reload()
	{
		socks_addr = "";
		ignore_proxy_domains.reset();
		ignore_proxy_masks.reset();
		
#ifdef BACKEND_GSETTINGS
		// half of the rules aren't implemented, they're marked with ?
		// if 'use the X proxy' is attempted, but that proxy type isn't configured, just continue to the next step
		// proxies are only skipped if unconfigured; configured but misbehaving proxy shall cause connection failure
		
		//?if mode is auto:
		//?   read and use /system/proxy/autoconfig-url
		// if the target is in /system/proxy/ignore-hosts:
		//    connect directly
		// if mode is manual:
		//?  if protocol is ftp:
		//?    use the ftp proxy (no, I don't know what an ftp proxy is)
		//?  if protocol is http:
		//?    use the http proxy
		//?  if protocol is https:
		//?    use the https proxy, with the authentication config from the http proxy
		//?    use the http proxy
		//   use the socks proxy
		// connect directly
		
		string proxy_mode = string::create_usurp(g_settings_get_string(gs_proxy, "mode"));
		
		char** ignore_proxy_raw = g_settings_get_strv(gs_proxy, "ignore-hosts");
		for (char** iter = ignore_proxy_raw;*iter;iter++)
		{
			cstring str = *iter;
			
			socket2::netmask mask;
			if (mask.parse(str))
				ignore_proxy_masks.append(mask);
			else
				ignore_proxy_domains.append(str);
		}
		g_strfreev(ignore_proxy_raw);
		
		if (proxy_mode == "manual")
		{
			socks_addr = string::create_usurp(g_settings_get_string(gs_socks, "host"));
			socks_port = g_settings_get_int(gs_socks, "port");
		}
#endif
#ifdef BACKEND_GVDB
		gvdb settings(file2::dir_config()+"dconf/user");
		// no clue why the gsettings keys are so different (. to /, _ to -, last component is special cased, missing org.gnome)
		// gsettings allegedly also supports some system-global database too; this is not implemented
		cstring proxy_mode = settings.get_str("/system/proxy/mode", "none");
		
		array<cstring> ignore_proxy_raw;
		if (!settings.get_str_array("/system/proxy/ignore-hosts", ignore_proxy_raw))
		{
			ignore_proxy_raw.append("localhost");
			ignore_proxy_raw.append("127.0.0.0/8");
			ignore_proxy_raw.append("::1");
		}
		
		for (cstring str : ignore_proxy_raw)
		{
			socket2::netmask mask;
			if (mask.parse(str))
				ignore_proxy_masks.append(mask);
			else
				ignore_proxy_domains.append(str);
		}
		if (proxy_mode == "manual")
		{
			socks_addr = settings.get_str("/system/proxy/socks/host");
			socks_port = settings.get_int("/system/proxy/socks/port");
		}
#endif
		// for windows, use WinHttpGetIEProxyConfigForCurrentUser
	}
	
	async<autoptr<socket2>> create_socket(cstring domain, uint16_t port)
	{
		mutexlocker lk(mut);
		
		socket2::address addr;
		if (addr.parse(domain))
		{
			for (socket2::netmask mask : ignore_proxy_masks)
			{
				if (mask.matches(addr))
					return socket2::create(domain, port);
			}
		}
		else
		{
			for (cstring addr : ignore_proxy_domains)
			{
				if (domain == addr)
					return socket2::create(domain, port);
			}
		}
		if (socks_addr && socks_port)
			return socks5::create(socks_addr, socks_port, domain, port);
		return socket2::create(domain, port);
	}
	
	~proxy_config()
	{
#ifdef BACKEND_GSETTINGS
		g_object_unref(gs_proxy);
		g_object_unref(gs_socks);
#endif
	}
};
}

static proxy_config conf;

async<autoptr<socket2>> autoproxy::socket_create(cstring domain, uint16_t port)
{
	return conf.create_socket(domain, port);
}
#endif
