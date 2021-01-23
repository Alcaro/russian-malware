#pragma once
#include "global.h"
#include "json.h"
#include "bml.h"

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
	template<typename T> void items(cstring name, T& item, ...);
	
	// If serializing:
	void item(cstring name, T& inner);
	void item_compact(int newmax, T& inner); // JSON only.
	
	// If deserializing:
	bool has_item(); // Returns whether there are any objects left to deserialize. If yes, you may use these:
	cstring name(); // Returns the key of the object currently pointed to.
	cstring value(); // BML only. Returns the value corresponding to 'name'.
	void item(T& out); // Fills in 'out' with whatever name() refers to. Updates has_item() and name(), and may be called again.
	void each_item(T& out); // Takes a lambda (T& s, cstring key), and calls it for each child.
};
#endif

#define SERIALIZE_CORE(member) STR(member), member, // this extra comma is troublesome, but ppforeach is kinda limited
#define SERIALIZE(...) template<typename Ts> void serialize(Ts& s) { s.items(PPFOREACH(SERIALIZE_CORE, __VA_ARGS__) nullptr); }

template<typename T> struct ser_array_t { T& c; ser_array_t(T& c) : c(c) {} };
template<typename T> ser_array_t<T> ser_array(T& c) { return ser_array_t(c); }
template<typename T> ser_array_t<const T> ser_array(const T& c) { return ser_array_t(c); }

template<typename T> struct ser_hex_t { T& c; ser_hex_t(T& c) : c(c) {} };
template<typename T> ser_hex_t<T> ser_hex(T& c) { return ser_hex_t(c); }
template<typename T> ser_hex_t<const T> ser_hex(const T& c) { return ser_hex_t(c); }

template<typename T> struct ser_compact_t { T& c; int depth; ser_compact_t(T& c, int depth) : c(c), depth(depth) {} };
template<typename T> ser_compact_t<T> ser_compact(T& c, int depth = 0) { return ser_compact_t(c, depth); }
template<typename T> ser_compact_t<const T> ser_compact(const T& c, int depth = 0) { return ser_compact_t(c, depth); }

template<typename T> struct ser_include_if_t { bool cond; T& c; ser_include_if_t(bool cond, T& c) : cond(cond), c(c) {} };
template<typename T> ser_include_if_t<T> ser_include_if(bool cond, T& c) { return ser_include_if_t(cond, c); }
template<typename T> ser_include_if_t<const T> ser_include_if(bool cond, const T& c) { return ser_include_if_t(cond, c); }

template<typename T>
static constexpr inline
std::enable_if_t<sizeof(T::serialize_as_array())!=0, bool>
serialize_as_array(int ov_resolut1)
{
	return T::serialize_as_array();
}
template<typename T> // I can't do 'enable if serialize_as_array member does not exist'
constexpr static inline bool serialize_as_array(float ov_resolut1) // instead, extra argument so the other is a better match
{
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
			if (delay_compact == 0) w.compress(true);
			delay_compact--;
		}
	}
	void exit_compact()
	{
		if (delay_compact != INT_MAX)
		{
			delay_compact++;
			if (delay_compact == 0) w.compress(false);
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
	std::enable_if_t<!std::is_same_v<decltype(
			std::declval<T>().serialize(std::declval<jsonserializer&>())
		), void*>> // std::is_invocable gives goofy results with template member functions; this is weird, but it works
	add_node(T& inner)
	{
		if constexpr (serialize_as_array<T>(123))
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
	std::enable_if_t<std::is_invocable_v<T, jsonserializer&>>
	add_node(const T& inner)
	{
		w.map_enter();
		enter_compact();
		inner(*this);
		exit_compact();
		w.map_exit();
	}
	
	template<typename T>
	std::enable_if_t<sizeof(typename T::serialize_as)!=0>
	add_node(T& inner)
	{
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
	void add_node(ser_compact_t<T> inner)
	{
		int prev = delay_compact;
		delay_compact = min(inner.depth, delay_compact);
		add_node(inner.c);
		delay_compact = prev;
	}
	
	
	template<typename T>
	std::enable_if_t<std::is_integral_v<T>>
	add_node_hex(T inner) { w.num(inner); }
	
	void add_node_hex(bytesr inner) { w.str(tostringhex(inner)); }
	
	
	void items_inner() {}
	void items_inner(nullptr_t) {}
	
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
		w.compress(true);
		item(std::forward<Args>(args)...);
		w.compress(false);
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
		if (ev.action == jsonparser::error)
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
			if (ev.action == jsonparser::enter_map || ev.action == jsonparser::enter_list) nest++;
			if (ev.action == jsonparser::exit_map || ev.action == jsonparser::exit_list) nest--;
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
		bool has_item() { return parent.ev.action != jsonparser::exit_list; }
	};
	friend class jsondeserializer_array;
	
	template<typename T>
	std::enable_if_t<!std::is_same_v<decltype(
			std::declval<T>().serialize(std::declval<jsondeserializer&>())
		), void*>>
	read_item_raw(T& out)
	{
		if constexpr (serialize_as_array<T>(123))
		{
			if (ev.action == jsonparser::enter_list)
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
			if (ev.action == jsonparser::enter_map)
			{
				next_ev();
				out.serialize(*this);
				finish_item(1);
			}
			else valid = false;
		}
	}
	
	template<typename T>
	std::enable_if_t<std::is_invocable_v<T, jsondeserializer&>>
	read_item_raw(const T& out)
	{
		if (ev.action == jsonparser::enter_map)
		{
			next_ev();
			out(*this);
			finish_item(1);
		}
		else valid = false;
	}
	
	template<typename T>
	std::enable_if_t<std::is_invocable_v<T, cstring>>
	read_item_raw(const T& out)
	{
		if (ev.action != jsonparser::str) { valid = false; return; }
		out(ev.str);
	}
	
	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T>>
	read_item_raw(T& out)
	{
		if (ev.action != jsonparser::num) { valid = false; return; }
		valid &= fromstring(ev.str, out);
	}
	
	void read_item_raw(string& out)
	{
		if (ev.action != jsonparser::str) { valid = false; return; }
		out = ev.str;
	}
	
	void read_item_raw(bool& out)
	{
		if (ev.action == jsonparser::jtrue) out = true;
		else if (ev.action == jsonparser::jfalse) out = false;
		else valid = false;
	}
	
	template<typename T>
	std::enable_if_t<sizeof(typename T::serialize_as)!=0>
	read_item_raw(T& inner)
	{
		typename T::serialize_as tmp{};
		read_item_raw(tmp);
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
	void read_item_raw(ser_array_t<T> inner)
	{
		if (ev.action == jsonparser::enter_list)
		{
			next_ev();
			while (ev.action != jsonparser::exit_list)
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
	std::enable_if_t<std::is_integral_v<T>>
	read_item_hex(T& inner) { read_item_raw(inner); }
	
	void read_item_hex(bytesw inner)
	{
		if (ev.action != jsonparser::str) { valid = false; return; }
		valid &= fromstringhex(ev.str, inner);
	}
	void read_item_hex(bytearray& inner)
	{
		if (ev.action != jsonparser::str) { valid = false; return; }
		valid &= fromstringhex(ev.str, inner);
	}
	
	
	template<typename T>
	void read_item(T& inner)
	{
		if (ev.action == jsonparser::exit_list || ev.action == jsonparser::exit_map)
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
	void items_inner(nullptr_t) {}
	
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
		while (ev.action != jsonparser::exit_map)
		{
			if (ev.action != jsonparser::map_key) { valid = false; return; }
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
		if (ev.action == jsonparser::map_key) return ev.str;
		else return "";
	}
	
	bool has_item() { return ev.action == jsonparser::map_key; }
	
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
	void each_item(const T& out)
	{
		while (ev.action == jsonparser::map_key)
		{
			string key = std::move(ev.str);
			next_ev();
			if (ev.action == jsonparser::enter_map)
			{
				next_ev();
				out(*this, key);
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



template<typename T> string bmlserialize(T& item);
template<typename T> string bmlserialize(const T& item);

class bmlserializer {
	bmlwriter w;
	
	template<typename T> friend string bmlserialize(T& item);
	template<typename T> friend string bmlserialize(const T& item);
	
	
	class bmlserializer_array {
		bmlserializer& parent;
		cstring name;
	public:
		static const bool serializing = true;
		
		bmlserializer_array(bmlserializer& parent, cstring name) : parent(parent), name(name) {}
		template<typename T> void item(T& inner) { parent.item_inner(name, inner); }
		template<typename T> void item(const T& inner) { parent.item_inner(name, inner); }
	};
	friend class bmlserializer_array;
	
	template<typename T>
	std::enable_if_t<!std::is_same_v<decltype(
			std::declval<T>().serialize(std::declval<bmlserializer&>())
		), void*>>
	item_inner(cstring name, T& inner)
	{
		if constexpr (serialize_as_array<T>(123))
		{
			bmlserializer_array tmp(*this, name);
			inner.serialize(tmp);
		}
		else
		{
			w.enter(name, "");
			inner.serialize(*this);
			w.exit();
		}
	}
	
	template<typename T>
	std::enable_if_t<sizeof(tostring(std::declval<T>()))!=0>
	item_inner(cstring name, const T& inner)
	{
		w.node(name, tostring(inner));
	}
	
	template<typename T>
	std::enable_if_t<sizeof(typename T::serialize_as)!=0>
	item_inner(cstring name, T& inner)
	{
		item_inner(name, (typename T::serialize_as)inner);
	}
	
	template<typename T>
	void item_inner(cstring name, ser_hex_t<T> inner)
	{
		w.node(name, tostringhex(inner.c));
	}
	
	
	void items_inner() {}
	void items_inner(nullptr_t) {}
	
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
	
	static const bool serializing = true;
	
	template<typename T> void item(cstring name, T& inner) { item_inner(bmlwriter::escape(name), inner); }
	template<typename T> void item(cstring name, const T& inner) { item_inner(bmlwriter::escape(name), inner); }
	
	template<typename... Ts>
	void items(Ts&&... args)
	{
		items_inner(std::forward<Ts>(args)...);
	}
};

template<typename T> string bmlserialize(T& item)
{
	bmlserializer s;
	item.serialize(s);
	return s.w.finish();
}

template<typename T> string bmlserialize(const T& item)
{
	bmlserializer s;
	item.serialize(s);
	return s.w.finish();
}


template<typename T> T bmldeserialize(cstring bml);
template<typename T> bool bmldeserialize(cstring bml, T& out);
template<typename T> bool bmldeserialize(cstring bml, const T& out);

class bmldeserializer {
	bmlparser p;
	bmlparser::event ev;
	bool valid = true;
	bool did_anything;
	
	bmldeserializer(cstring bml) : p(bml) {}
	
	template<typename T> friend T bmldeserialize(cstring bml);
	template<typename T> friend bool bmldeserialize(cstring bml, T& out);
	template<typename T> friend bool bmldeserialize(cstring bml, const T& out);
	
	void next_ev()
	{
	again:
		ev = p.next();
		if (ev.action == bmlparser::error) goto again;
	}
	
	// input: ev points to anything
	// output: ev points to the closest exit or finish
	void finish_item()
	{
		size_t depth = 1;
		while (true)
		{
			if (ev.action == bmlparser::enter) depth++;
			else depth--;
			if (!depth) break;
			next_ev();
		}
	}
	
	
	class bmldeserializer_array {
		bmldeserializer& parent;
		bool active = true;
	public:
		static const bool serializing = false;
		
		bmldeserializer_array(bmldeserializer& parent) : parent(parent) {}
		template<typename T> void item(T& inner) { active = false; parent.read_item(inner); }
		template<typename T> void item(const T& inner) { active = false; parent.read_item(inner); }
		bool has_item() { return active; }
	};
	friend class bmlserializer_array;
	
	
	
	//item_inner:
	// input: ev points to enter
	// output: ev points to the next node; each implementation must end with finish_item() next_ev()
	
	template<typename T>
	std::enable_if_t<sizeof(fromstring(string(), std::declval<T&>()))!=0>
	read_item(T& out)
	{
		valid &= fromstring(ev.value, out);
		next_ev();
		finish_item();
		next_ev();
	}
	
	template<typename T>
	std::enable_if_t<!std::is_same_v<decltype(
			std::declval<T>().serialize(std::declval<bmldeserializer&>())
		), void*>>
	read_item(T& out)
	{
		if constexpr (serialize_as_array<T>(123))
		{
			bmldeserializer_array tmp(*this);
			out.serialize(tmp);
		}
		else
		{
			next_ev();
			out.serialize(*this);
			finish_item();
			next_ev();
		}
	}
	
	template<typename T>
	std::enable_if_t<std::is_invocable_v<T, bmldeserializer&>>
	read_item(const T& out)
	{
		next_ev();
		out(*this);
		finish_item();
		next_ev();
	}
	
	template<typename T>
	std::enable_if_t<std::is_invocable_v<T, bmldeserializer&, cstring>>
	read_item(const T& out)
	{
		string name = std::move(ev.value);
		next_ev();
		out(*this, name);
		finish_item();
		next_ev();
	}
	
	template<typename T>
	void read_item(ser_hex_t<T> out)
	{
		valid &= fromstringhex(ev.value, out.c);
		next_ev();
		finish_item();
		next_ev();
	}
	
	template<typename T>
	std::enable_if_t<sizeof(typename T::serialize_as)!=0>
	read_item(T& out)
	{
		typename T::serialize_as tmp{};
		read_item(tmp);
		out = std::move(tmp);
	}
	
	
	template<typename T> void item_inner(const char * name, T& inner)
	{
		while (ev.name == name)
		{
			read_item(inner);
			did_anything = true;
		}
	}
	
	void items_inner() {}
	void items_inner(nullptr_t) {}
	
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
		while (ev.action == bmlparser::enter)
		{
			did_anything = false;
			items_inner(std::forward<Ts>(args)...);
			if (!did_anything)
			{
				// a{ } >x{< y{ } } b{ }
				next_ev(); // a{ } x{ >y{< } } b{ }
				finish_item(); // a{ } x{ y{ } >}< b{ }
				next_ev(); // a{ } x{ y{ } } >b{< }
			}
		}
	}
	
	
	cstring name() const
	{
		if (ev.action == bmlparser::enter) return bmlwriter::unescape(ev.name);
		else return "";
	}
	cstring value() const
	{
		return ev.value;
	}
	
	bool has_item() { return ev.action == bmlparser::enter; }
	
	template<typename T>
	void item(T& out)
	{
		read_item(out);
	}
};

template<typename T> T bmldeserialize(cstring bml)
{
	T out{};
	bmldeserializer d(bml);
	d.read_item(out);
	return out;
}

template<typename T> bool bmldeserialize(cstring bml, T& out)
{
	bmldeserializer d(bml);
	d.read_item(out);
	return d.valid;
}

template<typename T> bool bmldeserialize(cstring bml, const T& out)
{
	bmldeserializer d(bml);
	d.read_item(out);
	return d.valid;
}
