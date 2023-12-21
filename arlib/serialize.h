#pragma once
#include "global.h"
#include "json.h"
#include "base64.h"

//Members of the serialize(T& s) argument:
#if 0
class serializer {
public:
	static const bool serializing;
	
	// Takes most builtin and Arlib types, as well as any object with a serialize(T& s) member or serialize_as member typedef.
	// Consumes all of the object's contents; only call it once, with all expected members as arguments.
	// The item may optionally be wrapped in
	//  ser_array() - takes a lambda and an array in its input, and calls the lambda once per array member. Deserializing only.
	//  ser_hex() - takes an unsigned integer or uint8_t array. For JSON, integers are encoded as integers, not strings.
	//  ser_compact(val, depth) - (JSON only) deindents val's N-deep children (if depth is 1, val's children are one line each).
	//  ser_include_if(cond, val) - when serializing, emits val only if cond is true. When deserializing, acts like val.
	//  ser_include_if_true(val) - when serializing, emits val only if it's truthy. When deserializing, acts like val.
	template<typename T> void items(cstring name, T& item, ...);
	
	// If serializing:
	void item(cstring name, T& inner);
	void item_compact(int newmax, T& inner); // JSON only.
	
	// If deserializing:
	bool has_item(); // Returns whether there are any objects left to deserialize. If yes, you may use these:
	cstring name(); // Returns the key of the object currently pointed to.
	void item(T& out); // Fills in 'out' with whatever name() refers to. Updates has_item() and name(), and may be called again.
	void each_item(T& out); // Takes a lambda (T& s, cstring key), and calls it for each child.
};
#endif

#define SERIALIZE_LOOP(member) , STR(member), member
#define SERIALIZE(first, ...) \
	template<typename Ts> void serialize(Ts& s) { s.items(STR(first), first PPFOREACH(SERIALIZE_LOOP, __VA_ARGS__)); }

template<typename T> struct ser_array_t { T& c; ser_array_t(T& c) : c(c) {} };
template<typename T> ser_array_t<T> ser_array(T& c) { return ser_array_t(c); }
template<typename T> ser_array_t<const T> ser_array(const T& c) { return ser_array_t(c); }

template<typename T> struct ser_hex_t { T& c; ser_hex_t(T& c) : c(c) {} };
template<typename T> ser_hex_t<T> ser_hex(T& c) { return ser_hex_t(c); }
template<typename T> ser_hex_t<const T> ser_hex(const T& c) { return ser_hex_t(c); }

template<typename T> struct ser_base64_t { T& c; ser_base64_t(T& c) : c(c) {} };
template<typename T> ser_base64_t<T> ser_base64(T& c) { return ser_base64_t(c); }
template<typename T> ser_base64_t<const T> ser_base64(const T& c) { return ser_base64_t(c); }

template<typename T> struct ser_compact_t { T& c; int depth; ser_compact_t(T& c, int depth) : c(c), depth(depth) {} };
template<typename T> ser_compact_t<T> ser_compact(T& c, int depth = 0) { return ser_compact_t(c, depth); }
template<typename T> ser_compact_t<const T> ser_compact(const T& c, int depth = 0) { return ser_compact_t(c, depth); }

template<typename T> struct ser_include_if_t { bool cond; T& c; ser_include_if_t(bool cond, T& c) : cond(cond), c(c) {} };
template<typename T> ser_include_if_t<T> ser_include_if(bool cond, T& c) { return ser_include_if_t(cond, c); }
template<typename T> ser_include_if_t<const T> ser_include_if(bool cond, const T& c) { return ser_include_if_t(cond, c); }
template<typename T> ser_include_if_t<T> ser_include_if_true(T& c) { return ser_include_if_t((bool)c, c); }

template<typename T>
static constexpr inline
bool serialize_as_array()
{
	if constexpr (requires { T::serialize_as_array(); })
		return T::serialize_as_array();
	else
		return false;
}




template<int indent = 0, typename T> string jsonserialize(T& item);
template<int indent = 0, typename T> string jsonserialize(const T& item);

class jsonserializer {
	jsonwriter w;
	int delay_compact = INT_MAX;
	
	template<int indent, typename T> friend string jsonserialize(T& item);
	template<int indent, typename T> friend string jsonserialize(const T& item);
	
	jsonserializer(int indent) : w(indent) {}
	
	void enter_compact()
	{
		if (delay_compact != INT_MAX)
		{
			if (delay_compact == 0) w.compress();
			delay_compact--;
		}
	}
	void exit_compact()
	{
		if (delay_compact != INT_MAX)
		{
			delay_compact++;
			if (delay_compact == 0) w.uncompress();
		}
	}
	
	
	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T>>
	add_node(T inner)
	{
		if constexpr (std::is_same_v<T, bool>) w.boolean(inner);
		else w.num(inner);
	}
	
	void add_node(cstring inner) { w.str(inner); }
	
	
	class jsonserializer_array {
		jsonserializer& parent;
	public:
		static const bool serializing = true;
		
		jsonserializer_array(jsonserializer& parent) : parent(parent) {}
		template<typename T> void item(T& inner) { parent.add_node(inner); }
		template<typename T> void item(const T& inner) { parent.add_node(inner); }
	};
	friend class jsonserializer_array;
	
	template<typename T>
	void add_node(T& inner) requires requires { inner.serialize(*this); }
	{
		if constexpr (serialize_as_array<T>())
		{
			w.list_enter();
			enter_compact();
			jsonserializer_array tmp(*this);
			inner.serialize(tmp);
			exit_compact();
			w.list_exit();
		}
		else
		{
			w.map_enter();
			enter_compact();
			inner.serialize(*this);
			exit_compact();
			w.map_exit();
		}
	}
	
	template<typename T>
	void add_node(const T& inner) requires requires { inner(*this); }
	{
		w.map_enter();
		enter_compact();
		inner(*this);
		exit_compact();
		w.map_exit();
	}
	
	template<typename T>
	void add_node(T& inner) requires requires { typename T::serialize_as; }
	{
		if constexpr (std::is_same_v<typename T::serialize_as, string>)
			add_node(tostring(inner));
		else
			add_node((typename T::serialize_as)inner);
	}
	
	template<typename T, size_t size>
	void add_node(T(&inner)[size])
	{
		arrayview<T> tmp(inner);
		add_node(tmp);
	}
	
	template<typename T>
	void add_node(ser_hex_t<T> inner)
	{
		add_node_hex(inner.c);
	}
	
	template<typename T>
	void add_node(ser_base64_t<T> inner)
	{
		add_node_base64(inner.c);
	}
	
	template<typename T>
	void add_node(ser_compact_t<T> inner)
	{
		int prev = delay_compact;
		delay_compact = min(inner.depth, delay_compact);
		add_node(inner.c);
		delay_compact = prev;
	}
	
	
	template<typename T>
	void add_node_hex(T inner) requires (std::is_integral_v<T>) { w.num(inner); }
	
	void add_node_hex(bytesr inner) { w.str(tostringhex(inner)); }
	void add_node_base64(bytesr inner) { w.str(base64_enc(inner)); }
	
	
	void items_inner() {}
	
	template<typename Ti, typename... Ts>
	void items_inner(const char * name, Ti& inner, Ts&&... args)
	{
		w.map_key(name);
		add_node(inner);
		items_inner(std::forward<Ts>(args)...);
	}
	
	template<typename Ti, typename... Ts>
	void items_inner(const char * name, const Ti& inner, Ts&&... args)
	{
		w.map_key(name);
		add_node(inner);
		items_inner(std::forward<Ts>(args)...);
	}
	
	template<typename Ti, typename... Ts>
	void items_inner(const char * name, const ser_include_if_t<Ti>& inner, Ts&&... args)
	{
		if (inner.cond)
		{
			w.map_key(name);
			add_node(inner.c);
		}
		items_inner(std::forward<Ts>(args)...);
	}
	
public:
	
	static const bool serializing = true;
	
	template<typename T> void item(cstring name, T& inner) { w.map_key(name); add_node(inner); }
	template<typename T> void item(cstring name, const T& inner) { w.map_key(name); add_node(inner); }
	
	template<typename... Args> void item_compact(int newmax, Args&&... args)
	{
		w.compress();
		item(std::forward<Args>(args)...);
		w.uncompress();
	}
	
	template<typename... Ts>
	void items(Ts&&... args)
	{
		items_inner(std::forward<Ts>(args)...);
	}
};

template<int indent /* = 0 */, typename T> string jsonserialize(T& item)
{
	jsonserializer s(indent);
	s.add_node(item);
	return s.w.finish();
}

template<int indent /* = 0 */, typename T> string jsonserialize(const T& item)
{
	jsonserializer s(indent);
	s.add_node(item);
	return s.w.finish();
}


template<typename T> T jsondeserialize(cstring json, bool* valid = nullptr);
class jsondeserializer {
	jsonparser p;
	jsonparser::event ev;
	bool did_anything;
	bool valid = true;
	
	// serialize() is entered with ev pointing to the first object after map/list_enter (often a map_key)
	
	jsondeserializer(cstring json) : p(json) {}
	template<typename T> friend T jsondeserialize(cstring json, bool* valid);
	template<typename T> friend bool jsondeserialize(cstring json, T& out);
	template<typename T> friend bool jsondeserialize(cstring json, const T& out); // lambdas are const by default
	
	void next_ev()
	{
	again:
		ev = p.next();
		if (ev.type == jsonparser::error)
		{
			valid = false;
			goto again;
		}
	}
	
	//if nest=0:
	// input: ev points to any node
	// output: if ev pointed to enter_map or enter_list, ev now points to corresponding exit; if not, no change
	//if nest=1:
	// input: ev points to any node
	// output: if ev pointed to exit_map or _list, no change; if not, it now does
	void finish_item(int nest = 0)
	{
		while (true)
		{
			if (ev.type == jsonparser::enter_map || ev.type == jsonparser::enter_list) nest++;
			if (ev.type == jsonparser::exit_map || ev.type == jsonparser::exit_list) nest--;
			if (nest <= 0) break;
			next_ev();
		}
	}
	
	
	// read_item_raw:
	// input: ev must point to a value item (jtrue, jfalse, jnull, str, num, enter_list, enter_map)
	// output: ev points to the corresponding exit_list/map, or same item as before if it's not a list/map
	
	// read_item:
	// input: ev must point to a value item (jtrue, jfalse, jnull, str, num, enter_list, enter_map)
	// output: ev points to whatever comes after that item (after the exit_, if appropriate)
	
	class jsondeserializer_array {
		jsondeserializer& parent;
	public:
		static const bool serializing = false;
		
		jsondeserializer_array(jsondeserializer& parent) : parent(parent) {}
		template<typename T> void item(T& inner) { parent.read_item(inner); }
		template<typename T> void item(const T& inner) { parent.read_item(inner); }
		bool has_item() { return parent.ev.type != jsonparser::exit_list; }
	};
	friend class jsondeserializer_array;
	
	template<typename T>
	void read_item_raw(T& out) requires requires { out.serialize(*this); }
	{
		if constexpr (serialize_as_array<T>())
		{
			if (ev.type == jsonparser::enter_list)
			{
				next_ev();
				jsondeserializer_array tmp(*this);
				out.serialize(tmp);
				finish_item(1);
			}
			else valid = false;
		}
		else
		{
			if (ev.type == jsonparser::enter_map)
			{
				next_ev();
				out.serialize(*this);
				finish_item(1);
			}
			else valid = false;
		}
	}
	
	template<typename T>
	void read_item_raw(const T& out) requires requires { out(*this); }
	{
		if (ev.type == jsonparser::enter_map)
		{
			next_ev();
			out(*this);
			finish_item(1);
		}
		else valid = false;
	}
	
	template<typename T>
	void read_item_raw(const T& out) requires requires { out(""); }
	{
		if (ev.type != jsonparser::str) { valid = false; return; }
		out(ev.str);
	}
	
	template<typename T>
	void read_item_raw(T& out) requires (std::is_arithmetic_v<T>)
	{
		if (ev.type != jsonparser::num) { valid = false; return; }
		valid &= fromstring(ev.str, out);
	}
	
	void read_item_raw(string& out)
	{
		if (ev.type != jsonparser::str) { valid = false; return; }
		out = ev.str;
	}
	
	void read_item_raw(bool& out)
	{
		if (ev.type == jsonparser::jtrue) out = true;
		else if (ev.type == jsonparser::jfalse) out = false;
		else valid = false;
	}
	
	template<typename T>
	void read_item_raw(T& inner) requires requires { typename T::serialize_as; }
	{
		typename T::serialize_as tmp{};
		read_item_raw(tmp);
		if constexpr (std::is_same_v<typename T::serialize_as, string>)
			valid &= fromstring(tmp, inner);
		else
			inner = std::move(tmp);
	}
	
	template<typename T, size_t size>
	void read_item_raw(T(&inner)[size])
	{
		arrayvieww<T> tmp(inner);
		read_item_raw(tmp);
	}
	
	template<typename T>
	void read_item_raw(ser_hex_t<T> inner)
	{
		read_item_hex(inner.c);
	}
	
	template<typename T>
	void read_item_raw(ser_base64_t<T> inner)
	{
		read_item_base64(inner.c);
	}
	
	template<typename T>
	void read_item_raw(ser_array_t<T> inner)
	{
		if (ev.type == jsonparser::enter_list)
		{
			next_ev();
			while (ev.type != jsonparser::exit_list)
				read_item(inner.c);
		}
		else valid = false;
	}
	
	template<typename T>
	void read_item_raw(ser_compact_t<T> inner)
	{
		read_item_raw(inner.c);
	}
	
	template<typename T>
	void read_item_raw(ser_include_if_t<T> inner)
	{
		read_item_raw(inner.c);
	}
	
	
	template<typename T>
	void read_item_hex(T& inner) requires (std::is_integral_v<T>) { read_item_raw(inner); }
	
	void read_item_hex(bytesw inner)
	{
		if (ev.type != jsonparser::str) { valid = false; return; }
		valid &= fromstringhex(ev.str, inner);
	}
	void read_item_hex(bytearray& inner)
	{
		if (ev.type != jsonparser::str) { valid = false; return; }
		valid &= fromstringhex(ev.str, inner);
	}
	
	void read_item_base64(bytearray& inner)
	{
		if (ev.type != jsonparser::str) { valid = false; return; }
		inner = base64_dec(ev.str);
		valid &= (inner || !ev.str);
	}
	
	
	template<typename T>
	void read_item(T& inner)
	{
		if (ev.type == jsonparser::exit_list || ev.type == jsonparser::exit_map)
			return; // do nothing - don't even next_ev, our caller needs this event
		read_item_raw(inner);
		finish_item(0); // should do nothing, but may happen if the handler was wrong type and didn't consume the data
		next_ev();
	}
	
	
	template<typename Ti> void item_inner(const char * name, Ti& inner)
	{
		while (name == ev.str)
		{
			next_ev();
			read_item(inner);
			did_anything = true; // do this after read_item(), it may reset it
		}
	}
	
	void items_inner() {}
	
	template<typename Ti, typename... Ts>
	void items_inner(const char * name, Ti& inner, Ts&&... args)
	{
		item_inner(name, inner);
		items_inner(std::forward<Ts>(args)...);
	}
	
	template<typename Ti, typename... Ts>
	void items_inner(const char * name, const Ti& inner, Ts&&... args)
	{
		item_inner(name, inner);
		items_inner(std::forward<Ts>(args)...);
	}
	
public:
	
	static const bool serializing = false;
	
	template<typename... Ts>
	void items(Ts&&... args)
	{
		while (ev.type != jsonparser::exit_map)
		{
			if (ev.type != jsonparser::map_key) { valid = false; return; }
			did_anything = false;
			items_inner(std::forward<Ts>(args)...);
			if (!did_anything)
			{
				// points to map_key
				next_ev(); // points to list_enter
				finish_item(); // points to list_exit
				next_ev(); // points to map_key
			}
		}
	}
	
	cstring name() const
	{
		if (ev.type == jsonparser::map_key) return ev.str;
		else return "";
	}
	
	bool has_item() { return ev.type == jsonparser::map_key; }
	
	template<typename T>
	void item(T& out)
	{
		next_ev();
		read_item(out);
	}
	
	template<typename T>
	void item(const T& out) // lambdas are const for some reason
	{
		next_ev();
		read_item(out);
	}
	
	template<typename T>
	void each_item(const T& out) requires requires { out("", *this); }
	{
		while (ev.type == jsonparser::map_key)
		{
			string key = std::move(ev.str);
			next_ev();
			if (ev.type == jsonparser::enter_map)
			{
				next_ev();
				out(key, *this);
				finish_item(1);
			}
			else
			{
				valid = false;
				finish_item(0); // should do nothing, but may happen if the handler was wrong type and didn't consume the data
			}
			next_ev();
		}
	}
	
	template<typename T>
	void each_item(const T& out) requires requires { out("", ""); }
	{
		while (ev.type == jsonparser::map_key)
		{
			string key = std::move(ev.str);
			next_ev();
			if (ev.type == jsonparser::str)
			{
				out(key, ev.str);
			}
			else
			{
				valid = false;
				finish_item(0);
			}
			next_ev();
		}
	}
};

template<typename T> T jsondeserialize(cstring json, bool* valid /*= nullptr*/)
{
	T out{};
	jsondeserializer d(json);
	d.next_ev();
	d.read_item(out);
	if (valid) *valid = d.valid;
	return out;
}

template<typename T> bool jsondeserialize(cstring json, T& out)
{
	jsondeserializer d(json);
	d.next_ev();
	d.read_item(out);
	return d.valid;
}

template<typename T> bool jsondeserialize(cstring json, const T& out)
{
	jsondeserializer d(json);
	d.next_ev();
	d.read_item(out);
	return d.valid;
}
