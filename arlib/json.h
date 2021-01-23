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
		int action = unset;
		cstring str; // string, stringified number, or error message (TODO: actual error messages)
	};
	
	//You can't stream data into this object.
	jsonparser(string json)
	{
		m_data_holder = std::move(json);
		m_data = m_data_holder.bytes().ptr();
		m_data_end = m_data + m_data_holder.length();
	}
	event next();
	bool errored() const { return m_errored; }
	
private:
	string m_data_holder;
	uint8_t * m_data;
	uint8_t * m_data_end;
	
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
//It is caller's responsibility to match every enter() to the right exit() before calling finish(), and to call map_key() as appropriate.
class jsonwriter {
	string m_data;
	bool m_comma = false;
	
	bool m_indent_is_value;
	uint8_t m_indent_block;
	uint8_t m_indent_size;
	int m_indent_depth;
	
	void comma()
	{
		if (m_comma) m_data += ',';
		m_comma = true;
		
		if (UNLIKELY(m_indent_block == 0))
		{
			if (m_indent_is_value)
			{
				m_data += ' ';
				m_indent_is_value = false;
			}
			else if (m_indent_depth)
			{
				cstring indent_str = ("        "+8-m_indent_size);
				m_data += '\n';
				for (int i=0;i<m_indent_depth;i++)
				{
					m_data += indent_str;
				}
			}
		}
	}
	
public:
	jsonwriter() { m_indent_block = 1; m_indent_size = 0; }
	// Max supported indentation depth is 8.
	jsonwriter(int indent) { m_indent_block = (indent == 0); m_indent_size = indent; m_indent_depth = 0; m_indent_is_value = false; }
	static string strwrap(cstring s);
	
	void null() { comma(); m_data += "null"; }
	void boolean(bool b) { comma(); m_data += b ? "true" : "false"; }
	void str(cstring s) { comma(); m_data += strwrap(s); }
	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T,bool>>
	num(T n) { comma(); m_data += tostring(n); }
	void list_enter() { comma(); m_data += "["; m_comma = false; if (m_indent_size) m_indent_depth++; }
	void list_exit() { m_data += "]"; m_comma = true; m_indent_depth--; }
	void map_enter() { comma(); m_data += "{"; m_comma = false; m_indent_depth++; }
	void map_key(cstring s) { str(s); m_data += ":"; m_comma = false; if (m_indent_size) m_indent_is_value = true; }
	void map_exit() { m_data += "}"; m_comma = true; m_indent_depth--; }
	
	// If compress(true) has been called more times than compress(false), indentation is removed.
	// If unindented, does nothing.
	void compress(bool enable)
	{
		if (!enable)
		{
			m_indent_block--;
			m_indent_is_value = false;
		}
		else m_indent_block++;
	}
	
	// Can only be called once.
	string finish() { return std::move(m_data); }
};



class JSON : nocopy {
	friend class JSONw;
	
	int m_action;
	string m_str;
	double m_num = 0;
	array<JSON> m_chld_list;
	map<string,JSON> m_chld_map;
	
	void construct(jsonparser& p, jsonparser::event& ev, bool* ok, size_t maxdepth);
	template<bool sort> void serialize(jsonwriter& w) const;
	
	static JSON c_null;
	
	const JSON& get_from_list(size_t idx) const
	{
		if (idx >= m_chld_list.size()) return c_null;
		return m_chld_list[idx];
	}
	JSON& get_from_list(size_t idx)
	{
		if (idx >= m_chld_list.size()) return c_null;
		return m_chld_list[idx];
	}
	
public:
	JSON() : m_action(jsonparser::jnull) {}
	explicit JSON(string s) { parse(s); }
	
	bool parse(string s);
	string serialize(int indent = 0) const;
	string serialize_sorted(int indent = 0) const;
	
	int type() const { return m_action; }
	
	double num() const { return m_num; } // This struct handles double only. If you need int64, use serialize.h.
	const string& str() const { return m_str; }
	arrayview<JSON> list() const { return m_chld_list; }
	const map<string,JSON>& assoc() const { return m_chld_map; }
	//pointless overloads to allow for (JSON& item : json.list()) without an extra const
	arrayvieww<JSON> list() { return m_chld_list; }
	map<string,JSON>& assoc() { return m_chld_map; } // the name 'map' is taken, have to use something else
	
	bool boolean() const
	{
		switch (m_action)
		{
		case jsonparser::unset: return false;
		case jsonparser::jtrue: return true;
		case jsonparser::jfalse: return false;
		case jsonparser::jnull: return false;
		case jsonparser::str: return m_str;
		case jsonparser::num: return m_num;
		case jsonparser::enter_list: return m_chld_list.size();
		case jsonparser::enter_map: return m_chld_map.size();
		case jsonparser::error: return false;
		default: abort(); // unreachable
		}
	}
	
	operator bool() const { return boolean(); }
	operator double() const { return num(); }
	operator const string&() const { return str(); }
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
	
	const JSON& operator[](int n) const { return get_from_list(n); }
	const JSON& operator[](size_t n) const { return get_from_list(n); }
	const JSON& operator[](const char * s) const { return assoc().get_or(s, c_null); }
	const JSON& operator[](cstring s) const { return assoc().get_or(s, c_null); }
	
	JSON& operator[](int n) { return get_from_list(n); }
	JSON& operator[](size_t n) { return get_from_list(n); }
	JSON& operator[](const char * s) { return assoc().get_or(s, c_null); }
	JSON& operator[](cstring s) { return assoc().get_or(s, c_null); }
	
	const JSON& operator[](const JSON& right) const
	{
		if (right.type() == jsonparser::str) return assoc().get_or(right.str(), c_null);
		else return list()[right.num()];
	}
	JSON& operator[](const JSON& right)
	{
		if (right.type() == jsonparser::str) return assoc().get_or(right.str(), c_null);
		else return list()[right.num()];
	}
};

class JSONw : public JSON {
public:
	double& num() { m_action = jsonparser::num; return m_num; }
	string& str() { m_action = jsonparser::str; return m_str; }
	array<JSON>& list() { m_action = jsonparser::enter_list; return m_chld_list; }
	map<string,JSON>& assoc() { m_action = jsonparser::enter_map; return m_chld_map; }
	
	operator double() { return num(); }
	operator string&() { return str(); }
	operator cstring() { return str(); }
	
	JSONw& operator=(nullptr_t) { m_action = jsonparser::jnull; return *this; }
	JSONw& operator=(bool b) { m_action = b ? jsonparser::jtrue : jsonparser::jfalse; return *this; }
	JSONw& operator=(double n) { m_action = jsonparser::num; m_num = n; return *this; }
	JSONw& operator=(cstring s) { m_action = jsonparser::str; m_str = s; return *this; }
	JSONw& operator=(string s) { m_action = jsonparser::str; m_str = std::move(s); return *this; }
	JSONw& operator=(const char * s) { m_action = jsonparser::str; m_str = s; return *this; }
	
#define JSONOPS(T) \
		JSONw& operator=(T n) { return operator=((double)n); }
	ALLINTS(JSONOPS)
#undef JSONOPS
	
	//these technically aren't JSONw, but they have same binary representation so close enough
	JSONw& operator[](int n) { return operator[]((size_t)n); }
	JSONw& operator[](size_t n)
	{
		m_action = jsonparser::enter_list;
		if (n == m_chld_list.size()) return *(JSONw*)&m_chld_list.append();
		if (n < m_chld_list.size()) return *(JSONw*)&(list()[n]);
		abort();
	}
	JSONw& operator[](const char * s) { return *(JSONw*)&(assoc().get_create(s)); }
	JSONw& operator[](cstring s) { return *(JSONw*)&(assoc().get_create(s)); }
	
	JSONw& operator[](const JSON& right)
	{
		if (right.type() == jsonparser::str) return *(JSONw*)&(assoc().get_create(right.str()));
		else return *(JSONw*)&(list()[right.num()]);
	}
};
