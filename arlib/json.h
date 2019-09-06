#pragma once
#include "global.h"
#include "array.h"
#include "string.h"
#include "stringconv.h"
#include "set.h"

//This is a streaming parser. It returns a sequence of event objects.
//For example, the document
/*
{ "foo": [ 1, 2, 3 ] }
*/
//would yield { enter_map } { map_key, "foo" } { enter_list } { num, 1 } { num, 2 } { num, 3 } { exit_list } { exit_map }
//The parser keeps trying after an { error }, giving you a partial view of the damaged document; however,
// there are no guarantees on how much you can see, and it is likely for one error to cause many more, or yield misplaced nodes.
//enter/exit types are always paired, even in the presense of errors. However, they may be anywhere;
// don't place any expectations on event order inside a map.
//After the document ends, { finish } will be returned forever until the object is deleted.
class jsonparser : nocopy {
public:
	enum {
		unset      = 0,
		jtrue      = 1,
		jfalse     = 2,
		jnull      = 3,
		str        = 4,
		num        = 5,
		enter_list = 6,
		exit_list  = 7,
		enter_map  = 8,
		map_key    = 9,
		exit_map   = 10,
		error      = 11,
		finish     = 12,
	};
	struct event {
		int action;
		string str; // or error message [TODO: actual error messages]
		double num;
		
		event() : action(unset) {}
		event(int action) : action(action) {}
		event(int action, cstring str) : action(action), str(str) {}
		event(int action, double num) : action(action), num(num) {}
	};
	
	//You can't stream data into this object.
	jsonparser(cstring json) : m_data(json) {}
	event next();
	bool errored() { return m_errored; }
	
private:
	cstring m_data;
	size_t m_pos = 0;
	bool m_want_key = false; // true if inside {}
	bool m_need_value = true; // true after a comma or at the start of the object
	
	bool m_unexpected_end = false; // used to avoid sending the same error again after the document "{" whines
	bool m_errored = false;
	event do_error() { m_errored=true; return { error }; }
	
	array<bool> m_nesting; // an entry is false for list, true for map; after [[{, this is false,false,true
	
	uint8_t nextch();
	bool skipcomma(size_t depth = 1);
	
	string getstr();
};


//This is also streaming.
//Calling exit() without a matching enter(), or finish() without closing every enter(), is undefined behavior.
class jsonwriter {
	string m_data;
	bool m_comma = false;
	
	void comma() { if (m_comma) m_data += ','; m_comma = true; }
	
public:
	static string strwrap(cstring s)
	{
		string out = "\"";
		for (size_t i=0;i<s.length();i++)
		{
			uint8_t c = s[i];
			if(0);
			else if (c=='\n') out += "\\n";
			else if (c=='\r') out += "\\r";
			else if (c=='\t') out += "\\t";
			else if (c=='\b') out += "\\b";
			else if (c=='\f') out += "\\f";
			else if (c=='\"') out += "\\\"";
			else if (c=='\\') out += "\\\\";
			else if (c < 32 || c == 0x7F) out += "\\u"+tostringhex<4>(c);
			else out += (char)c;
		}
		return out+"\"";
	}
	
	void null() { comma(); m_data += "null"; }
	void boolean(bool b) { comma(); m_data += b ? "true" : "false"; }
	void str(cstring s) { comma(); m_data += strwrap(s); }
	void num(int n)    { comma(); m_data += tostring(n); }
	void num(size_t n) { comma(); m_data += tostring(n); }
	void num(double n) { comma(); m_data += tostring(n); }
	void list_enter() { comma(); m_data += "["; m_comma = false; }
	void list_exit() { m_data += "]"; m_comma = true; }
	void map_enter() { comma(); m_data += "{"; m_comma = false; }
	void map_key(cstring s) { comma(); m_data += strwrap(s); m_data += ":"; m_comma = false; }
	void map_exit() { m_data += "}"; m_comma = true; }
	
	string finish() { return std::move(m_data); }
};



class JSON : nocopy {
	friend class JSONw;
	
	jsonparser::event ev;
	array<JSON> chld_list;
	map<string,JSON> chld_map;
	
	void construct(jsonparser& p, bool* ok, size_t maxdepth);
	template<bool sort> void serialize(jsonwriter& w) const;
	
	static const JSON c_null;
	
public:
	JSON() : ev(jsonparser::jnull) {}
	explicit JSON(cstring s) { parse(s); }
	
	bool parse(cstring s);
	string serialize() const;
	string serialize_sorted() const;
	
	int type() const { return ev.action; }
	
	double num() const { return ev.num; }
	const string& str() const { return ev.str; }
	arrayview<JSON> list() const { return chld_list; }
	const map<string,JSON>& assoc() const { return chld_map; }
	//pointless overloads to allow for (JSON& item : json.list()) without an extra const
	arrayvieww<JSON> list() { return chld_list; }
	map<string,JSON>& assoc() { return chld_map; }
	
	bool boolean() const
	{
		switch (ev.action)
		{
		case jsonparser::unset: return false;
		case jsonparser::jtrue: return true;
		case jsonparser::jfalse: return false;
		case jsonparser::jnull: return false;
		case jsonparser::str: return ev.str;
		case jsonparser::num: return ev.num;
		case jsonparser::enter_list: return chld_list.size();
		case jsonparser::enter_map: return chld_map.size();
		case jsonparser::error: return false;
		default: abort(); // unreachable
		}
	}
	
	operator bool() const { return boolean(); }
	operator double() const { return num(); }
	operator const string&() const { return ev.str; }
	operator cstring() const { return str(); }
	
	bool operator==(double right) const { return num()==right; }
	bool operator==(const char * right) const { return str()==right; }
	bool operator==(cstring right) const { return str()==right; }
	
	bool operator!=(double right) const { return num()!=right; }
	bool operator!=(const char * right) const { return str()!=right; }
	bool operator!=(cstring right) const { return str()!=right; }
	bool operator!() const { return !boolean(); }
	
#define JSONOPS(T) \
		operator T() const { return num(); } \
		bool operator==(T right) const { return num()==right; } \
		bool operator!=(T right) const { return num()!=right; }
	ALLINTS(JSONOPS)
#undef JSONOPS
	
	JSON& operator[](int n) { return list()[n]; }
	JSON& operator[](size_t n) { return list()[n]; }
	JSON& operator[](const char * s) { return assoc().get_create(s); }
	JSON& operator[](cstring s) { return assoc().get_create(s); }
	const JSON& operator[](int n) const { return list()[n]; }
	const JSON& operator[](size_t n) const { return list()[n]; }
	const JSON& operator[](const char * s) const { return assoc().get_or(s, c_null); }
	const JSON& operator[](cstring s) const { return assoc().get_or(s, c_null); }
	
	JSON& operator[](const JSON& right)
	{
		if (right.ev.action == jsonparser::str) return assoc().get_create(right.ev.str);
		else return list()[right.ev.num];
	}
	const JSON& operator[](const JSON& right) const
	{
		if (right.ev.action == jsonparser::str) return assoc().get_or(right.ev.str, c_null);
		else return list()[right.ev.num];
	}
};

class JSONw : public JSON {
public:
	double& num() { ev.action = jsonparser::num; return ev.num; }
	string& str() { ev.action = jsonparser::str; return ev.str; }
	array<JSON>& list() { ev.action = jsonparser::enter_list; return chld_list; }
	map<string,JSON>& assoc() { ev.action = jsonparser::enter_map; return chld_map; } // 'map' is taken
	
	operator double() { return num(); }
	operator string&() { return str(); }
	operator cstring() { return str(); }
	
	JSONw& operator=(nullptr_t) { ev.action = jsonparser::jnull; return *this; }
	JSONw& operator=(bool b) { ev.action = b ? jsonparser::jtrue : jsonparser::jfalse; return *this; }
	JSONw& operator=(double n) { ev.action = jsonparser::num; ev.num = n; return *this; }
	JSONw& operator=(cstring s) { ev.action = jsonparser::str; ev.str = s; return *this; }
	JSONw& operator=(string s) { ev.action = jsonparser::str; ev.str = std::move(s); return *this; }
	JSONw& operator=(const char * s) { ev.action = jsonparser::str; ev.str = s; return *this; }
	
#define JSONOPS(T) \
		JSONw& operator=(T n) { return operator=((double)n); }
	ALLINTS(JSONOPS)
#undef JSONOPS
	
	//these aren't JSONw, but it works
	JSONw& operator[](int n) { return *(JSONw*)&(list()[n]); }
	JSONw& operator[](size_t n) { return *(JSONw*)&(list()[n]); }
	JSONw& operator[](const char * s) { return *(JSONw*)&(assoc().get_create(s)); }
	JSONw& operator[](cstring s) { return *(JSONw*)&(assoc().get_create(s)); }
	
	JSONw& operator[](const JSON& right)
	{
		if (right.ev.action == jsonparser::str) return *(JSONw*)&(assoc().get_create(right.ev.str));
		else return *(JSONw*)&(list()[right.ev.num]);
	}
};
