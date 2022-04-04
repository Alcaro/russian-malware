#pragma once
#include "socket.h"

class bytestream;
//This is a fairly primitive DNS client. It doesn't retry, it doesn't try multiple resolvers, it doesn't support recursion.
//But it works in practice on all plausible systems.
//(TODO: lift some of those limits maybe)
class DNS {
	autoptr<socket> sock;
	runloop* loop;
	
	struct query {
		string domain;
		uintptr_t timeout_id;
		
		function<void(string domain, bytesr ip)> callback;
	};
	map<uint16_t, query> queries;
	
	map<string, bytearray> hosts_txt;
	
	void init(bytesr resolver, int port, runloop* loop);
	
	uint16_t pick_trid();
	
public:
	static bytearray default_resolver();
	
	DNS(runloop* loop) { init(default_resolver(), 53, loop); }
	DNS(bytesr resolver, int port, runloop* loop) { init(resolver, port, loop); }
	
	void resolve(cstring domain, unsigned timeout_ms, function<void(string domain, bytesr ip)> callback);
	void resolve(cstring domain, function<void(string domain, bytesr ip)> callback)
	{
		resolve(domain, 2000, callback);
	}
	
	void resolve(cstring domain, unsigned timeout_ms, function<void(string domain, string ip)> callback) = delete;
	void resolve(cstring domain, function<void(string domain, string ip)> callback) = delete;
	
	~DNS()
	{
		for (auto& pair : queries)
			loop->raw_timer_remove(pair.value.timeout_id);
	}
	
private:
	static string read_name(bytestream& stream);
	
	void sock_cb();
	void timeout(uint16_t trid);
};
