#pragma once
#include "global.h"
#include "array.h"
#include "string.h"
#include "stringconv.h"
#include "set.h"

//This is a streaming parser. It returns a sequence of event objects.
//For example, the document
/*
{ "foo": [ 1, 2, 3.0 ] }
*/
//would yield { enter_map } { map_key, "foo" } { enter_list } { num, "1" } { num, "2" } { num, "3.0" } { exit_list } { exit_map } { finish }
//The parser keeps trying after an { error }, giving you a partial view of the damaged document; however,
// there are no guarantees on how much you can see, and it is likely for one error to cause many more, or yield misplaced nodes.
//enter/exit types are always paired, even in the presense of errors. However, they may be anywhere;
// don't place any expectations on event order inside a map.
//After the document ends, { finish } will be returned forever until the object is deleted.
//The cstrings in the event are valid as long as the jsonparser itself.
class jsonparser {
	string m_data_holder;
	uint8_t * m_at;
	bool m_want_key = false;
	bool m_errored = false;
	bool m_need_error = false;
	bitarray m_nesting; // an entry is false for list, true for map; after [[{, this is false,false,true
	
	friend class json5parser;
	
	jsonparser() {}
	
	void init(string json)
	{
		m_data_holder = std::move(json);
		m_at = m_data_holder.bytes().ptr();
		
		skip_spaces_json();
		if (!*m_at)
			m_need_error = true;
	}
	void init5(string json)
	{
		m_data_holder = std::move(json);
		m_at = m_data_holder.bytes().ptr();
		
		skip_spaces_json5();
		if (!*m_at)
			m_need_error = true;
	}
	
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
		int type = unset;
		cstring str; // string, stringified number, or error message (TODO: actual error messages)
	};
	
	// You can't stream data into this object.
	jsonparser(string json) { init(std::move(json)); }
	
	event next();
	bool errored() const { return m_errored; }
	
	// Meaningless helper, in case something is templated on the JSON parser type.
	template<typename T>
	static bool parse_num(const event& ev, T& out) requires std::is_arithmetic_v<T>
	{
		return fromstring(ev.str, out);
	}
	
private:
	template<bool json5> cstring decode_backslashes(bytesw by);
	template<bool json5> event next_inner();
	template<bool json5> void skip_spaces();
	template<bool json5> event prepare_next(event ev, size_t forward = 0);
	
	void skip_spaces_json();
	void skip_spaces_json5();
	
	event next5();
};

class json5parser {
	jsonparser inner;
public:
	using event = jsonparser::event;
	
	// You can't stream data into this object.
	json5parser(string json) { inner.init5(std::move(json)); }
	
	event next() { return inner.next5(); }
	bool errored() const { return inner.errored(); }
	
	// JSON5 numbers support a few formats that normal JSON does not, so a separate decoder function is needed.
	// May return unexpected answers if the input is not valid JSON5.
	template<typename T>
	static bool parse_num(const event& ev, T& out) requires std::is_arithmetic_v<T>
	{
		if constexpr (std::is_floating_point_v<T>)
		{
			double v;
			bool ret = parse_num_as_double(ev.str, v);
			out = v;
			return ret;
		}
		else if constexpr (std::is_unsigned_v<T>)
		{
			uintmax_t v;
			bool ret = parse_num_as_unsigned(ev.str, v);
			out = v;
			return (ret && ((uintmax_t)out == v));
		}
		else
		{
			intmax_t v;
			bool ret = parse_num_as_signed(ev.str, v);
			out = v;
			return (ret && ((intmax_t)out == v));
		}
	}
	
private:
	static bool parse_num_as_double(cstring str, double& out);
	static bool parse_num_as_unsigned(cstring str, uintmax_t& out);
	static bool parse_num_as_signed(cstring str, intmax_t& out);
};



//This is also streaming.
//It is caller's responsibility to match every enter() to the right exit() before calling finish(), and to call map_key() as appropriate.
class jsonwriter {
	string m_data;
	bool m_comma = false;
	
	bool m_indent_is_value = false;
	uint8_t m_indent_disable = false;
	uint8_t m_indent_size = 0;
	uint16_t m_indent_depth = 0;
	
	void comma();
	
public:
	jsonwriter() {}
	// Max supported indentation depth is 8.
	jsonwriter(int indent) { m_indent_disable = (indent == 0); m_indent_size = indent; }
	static string strwrap(cstring s);
	
	void null();
	void boolean(bool b);
	void str(cstring s);
	template<typename T>
	void num(T n) requires (std::is_arithmetic_v<T> && !std::is_same_v<T,bool>) { comma(); m_data += tostring(n); }
	void num_unsafe(cstring n) { comma(); m_data += n; } // The string must be a valid output from an appropriate tostring function.
	void list_enter();
	void list_exit();
	void map_enter();
	void map_key(cstring s);
	void map_exit();
	
	// If compress() has been called, indentation is removed. To reenable, use uncompress().
	// Should be called only immediately after {list,map}_{enter,exit}. 
	// Do not call compress() while compressed already. If indentation wasn't configured, does nothing.
	void compress() { m_indent_disable = true; }
	void uncompress() { m_indent_disable = false; m_indent_is_value = false; }
	
	// Can only be called once.
	string finish() { return std::move(m_data); }
};



class JSONw : nocopy {
	variant_idx<void, void, void, void, string, string, array<JSONw>, void, map<string,JSONw>, void, void, void, void> content;
	
	template<int idx, typename... Ts> void set_to(Ts... args)
	{
		content.destruct_any();
		content.construct<idx>(args...);
	}
	
	void construct(jsonparser& p, jsonparser::event& ev, bool* ok, size_t maxdepth);
	template<bool sort> void serialize(jsonwriter& w) const;
	
	static JSONw c_null;
	static map<string,JSONw> c_null_map;
	
public:
	JSONw() { content.construct<jsonparser::unset>(); }
	explicit JSONw(string s) { parse(s); }
	
	bool parse(string s);
	string serialize(int indent = 0) const;
	string serialize_sorted(int indent = 0) const;
	
	int type() const { return content.type(); }
	
	template<typename T = double>
	T num() const { return content.contains<jsonparser::num>() ? try_fromstring<T>(content.get<jsonparser::num>()) : (T)0; }
	cstrnul str() const { return content.contains<jsonparser::str>() ? (cstrnul)content.get<jsonparser::str>() : (cstrnul)""; }
	arrayview<JSONw> list() const
	{
		return content.contains<jsonparser::enter_list>() ? (arrayview<JSONw>)content.get<jsonparser::enter_list>() : nullptr;
	}
	const map<string,JSONw>& assoc() const // the name 'map' is taken, have to use something else
	{
		return content.contains<jsonparser::enter_map>() ? content.get<jsonparser::enter_map>() : c_null_map;
	}
	
	bool boolean() const
	{
		switch (content.type())
		{
		case jsonparser::unset: return false;
		case jsonparser::jtrue: return true;
		case jsonparser::jfalse: return false;
		case jsonparser::jnull: return false;
		case jsonparser::str: return content.get<jsonparser::str>();
		case jsonparser::num: return try_fromstring<double>(content.get<jsonparser::num>());
		case jsonparser::enter_list: return content.get<jsonparser::enter_list>().size();
		case jsonparser::enter_map: return content.get<jsonparser::enter_map>().size();
		case jsonparser::error: return false;
		default: abort(); // unreachable
		}
	}
	
	operator bool() const { return boolean(); }
	operator double() const { return num(); }
	//operator const string&() const { return str(); }
	operator cstrnul() const { return str(); }
	
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
	
	const JSONw& operator[](int idx) const { return operator[]((size_t)idx); }
	const JSONw& operator[](size_t idx) const
	{
		const array<JSONw>* list = content.try_get<jsonparser::enter_list>();
		if (list && idx < list->size())
			return list[0][idx];
		else
			return c_null;
	}
	const JSONw& operator[](const char * s) const { return assoc().get_or(s, c_null); }
	const JSONw& operator[](cstring s) const { return assoc().get_or(s, c_null); }
	
	const JSONw& operator[](const JSONw& right) const
	{
		if (right.type() == jsonparser::str) return assoc().get_or(right.str(), c_null);
		else return list()[right.num()];
	}
	
	array<JSONw>& list()
	{
		if (!content.contains<jsonparser::enter_list>())
			set_to<jsonparser::enter_list>();
		return content.get<jsonparser::enter_list>();
	}
	map<string,JSONw>& assoc()
	{
		if (!content.contains<jsonparser::enter_map>())
			set_to<jsonparser::enter_map>();
		return content.get<jsonparser::enter_map>();
	}
	
	//operator double() { return num(); }
	//operator string&() { return str(); }
	//operator cstring() { return str(); }
	
	JSONw& operator=(nullptr_t) { set_to<jsonparser::jnull>(); return *this; }
	JSONw& operator=(bool b) { if (b) set_to<jsonparser::jtrue>(); else set_to<jsonparser::jfalse>(); return *this; }
	JSONw& operator=(double n) { set_to<jsonparser::num>(tostring(n)); return *this; }
	JSONw& operator=(cstring s) { set_to<jsonparser::str>(s); return *this; }
	JSONw& operator=(string s) { set_to<jsonparser::str>(std::move(s)); return *this; }
	JSONw& operator=(const char * s) { set_to<jsonparser::str>(s); return *this; }
	
#define JSONOPS(T) \
		JSONw& operator=(T n) { set_to<jsonparser::num>(tostring(n)); return *this; }
	ALLINTS(JSONOPS)
#undef JSONOPS
	
	JSONw& operator[](int idx) { return operator[]((size_t)idx); }
	JSONw& operator[](size_t idx)
	{
		array<JSONw>& children = list();
		if (idx == children.size()) return *(JSONw*)&children.append();
		if (idx < children.size()) return *(JSONw*)&(children[idx]);
		abort();
	}
	JSONw& operator[](const char * s) { return *(JSONw*)&(assoc().get_create(s)); }
	JSONw& operator[](cstring s) { return *(JSONw*)&(assoc().get_create(s)); }
	
	JSONw& operator[](const JSONw& right)
	{
		if (right.type() == jsonparser::str) return *(JSONw*)&(assoc().get_create(right.str()));
		else return *(JSONw*)&(list()[right.num()]);
	}
};

using JSON = const JSONw;
