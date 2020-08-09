#pragma once
#include "global.h"
#include "bml.h"
#include "json.h"
#include "stringconv.h"
#include "set.h"

#define SERIALIZE_CORE(member) s.item(STR(member), member);
#define SERIALIZE(...) template<typename T> void serialize(T& s) { PPFOREACH(SERIALIZE_CORE, __VA_ARGS__); }

#define ser_enter_1(s,x) for (bool serialize_first = true; s.enter(serialize_first);)
#define ser_enter_2(s,name) for (bool serialize_first = true; s.enter(serialize_first, name);)
#define ser_enter_pick(x1,x2,use,...) use(x1,x2)
#define ser_enter(...) ser_enter_pick(__VA_ARGS__, ser_enter_2, ser_enter_1)

// TODO: clean up this stuff, it's fairly nasty
// - teach item() to handle lambdas
// - teach item() to not be an overload handling mess
// - find a way to share the tricky parts of item() between the four classes
// - delete ser_enter and the enter() members
// much of the mess is caused by not properly accounting for children being named vs anonymous
//   {"a":1,"b":2,"c":3} is named, [1,2,3] is anonymous
// and/or by BML and JSON supporting different data types


//Interface:
//class serializer {
//public:
//	static const bool serializing;
//	
//	//Valid types:
//	//- Any integral type ('char' doesn't count as integral)
//	//- string (cstring allowed only when serializing)
//	//- array, set, map (if their contents are serializable)
//	//    map must use integer or string key, nothing funny
//	//- Any object with a serialize() function (see below)
//	//- Any object with a 'typedef uint64_t serialize_as;' member
//	//- Any object with a operator string() or operator=(cstring) function (only one needed if only one of serialize/deserialize needed)
//	//- A lambda, which will be called as the serialize() function
//	//The name can be any string.
//	template<typename T> void item(cstring name, T& item);
//	
//	//Similar to item(), but uses hex rather than decimal, if applicable. If not, identical to item, except the type restrictions.
//	//Valid types:
//	//- Any unsigned integral type
//	//- array<uint8_t>
//	//- arrayvieww<uint8_t>
//	template<typename T> void hex(cstring name, T& item);
//	void hex(cstring name, arrayvieww<uint8_t> item);
//	
//	//Makes serialized data look nicer. May be ignored. Ignored while unserializing.
//	void comment(cstring c);
//	
//	//These are valid only while unserializing. Check .serializing before calling.
//	//Returns the next child name the structure expects to process.
//	cstring next() const;
//	//item_next(foo) is like item(next(), foo), but won't do weird things
//	// due to next() being freed by item(), or due to two consecutive identical keys.
//	template<typename T> void item_next(T& item);
//	//(BML only) Returns the value corresponding to next().
//	cstring nextval() const;
//};
//
//struct serializable {
//	int a;
//	
//public:
//	template<typename T> // T is guaranteed to offer the serializer interface.
//	void serialize(T& s)
//	{
//		//If unserializing, this function can be called multiple (or zero) times if the document is
//		// corrupt. Be careful about changing any state, other than calling the serializer functions.
//		//For most items, .item() and .hex() are enough. For containers, do whatever.
//		s.item("a", a);
//	}
//	//or (expands to the above)
//public:
//	SERIALIZE(a);
//	//or
//public:
//	operator string() const;
//	serializable(cstring a);
//	//or
//public:
//	operator int() const;
//	serializable(int a);
//	typedef int serialize_as;
//	//If both serialize() and serialize_as exist, undefined behavior. If operator string() and another exist, latter is chosen.
//};


class bmlserializer {
	bmlwriter w;
	template<typename T> friend string bmlserialize(T& item);
	
	template<typename T>
	typename std::enable_if_t<
		!std::is_same_v<
			decltype(std::declval<T>().serialize(std::declval<bmlserializer&>())),
			void* // can't find a std::can_evaluate, so ensure that it doesn't yield <some arbitrary type it doesn't return>
		>>
	add_node(cstring name, T& item, int ov_resolut1) // ov_resolut1 is always 1, to enforce the desired overload resolution
	{
		w.enter(bmlwriter::escape(name), "");
		item.serialize(*this);
		w.exit();
	}
	
	template<typename T>
	typename std::enable_if_t<
		!std::is_same_v<
			typename T::serialize_as,
			void*
		>>
	add_node(cstring name, T& item, int ov_resolut1)
	{
		w.node(bmlwriter::escape(name), tostring((typename T::serialize_as)item));
	}
	
	template<typename T>
	typename std::enable_if_t<
		std::is_same_v<
			decltype(tostring(std::declval<T>())),
			string
		>>
	add_node(cstring name, T& item, float ov_resolut1)
	{
		w.node(bmlwriter::escape(name), tostring(item));
	}
	
	template<typename T> void add_node(cstring name, array<T>& item, int ov_resolut1)
	{
		for (auto& child : item)
		{
			add_node(name, child, 1);
		}
	}
	
	template<typename T> void add_node(cstring name, set<T>& item, int ov_resolut1)
	{
		for (auto const& child : item)
		{
			add_node(name, child, 1);
		}
	}
	
	template<typename T> void add_node(cstring name, array<array<T>>& item, int ov_resolut1) = delete;
	
public:
	
	static const bool serializing = true;
	
	void comment(cstring c)
	{
		w.comment(c);
	}
	
	template<typename T> void item(cstring name, T& item) { add_node(name, item, 1); }
	
	template<typename T> void hex(cstring name, T& item)
	{
		w.node(bmlwriter::escape(name), tostringhex(item));
	}
	void hex(cstring name, arrayview<uint8_t> item)
	{
		w.node(bmlwriter::escape(name), tostringhex(item));
	}
	
	template<typename T> void item_next(T& out) { abort(); } // only legal for deserializing
	cstring next() const { abort(); }
};

template<typename T> string bmlserialize(T& item)
{
	bmlserializer s;
	item.serialize(s);
	return s.w.finish();
}



class bmldeserializer {
	bmlparser p;
	int pdepth = 0;
	
	int thisdepth = 0;
	string thisnode;
	string thisval;
	bool matchagain;
	
	bmlparser::event event()
	{
		bmlparser::event ret = p.next();
		if (ret.action == bmlparser::enter) pdepth++;
		if (ret.action == bmlparser::exit) pdepth--;
		if (ret.action == bmlparser::finish) pdepth = -2;
		return ret;
	}
	
	void skipchildren()
	{
		while (pdepth > thisdepth) event();
	}
	
	template<typename T> friend T bmldeserialize(cstring bml);
	template<typename T> friend void bmldeserialize_to(cstring bml, T& to);
	
	template<typename T>
	typename std::enable_if_t<
		!std::is_same_v<
			decltype(std::declval<T>().serialize(std::declval<bmlserializer&>())),
			void*
		>>
	read_item(T& out, int ov_resolut1)
	{
		while (pdepth >= thisdepth)
		{
			bmlparser::event ev = event();
			if (ev.action == bmlparser::enter)
			{
				thisdepth++;
				thisnode = bmlwriter::unescape(ev.name);
				thisval = ev.value;
				do {
					matchagain = false;
					out.serialize(*this);
				} while (matchagain);
				thisdepth--;
				skipchildren();
			}
		}
	}
	
	template<typename T>
	typename std::enable_if_t<
		!std::is_same_v<
			typename T::serialize_as,
			void*
		>>
	read_item(T& out, int ov_resolut1)
	{
		out = try_fromstring<typename T::serialize_as>(thisval);
	}
	
	template<typename T>
	typename std::enable_if_t<
		!std::is_same_v<
			decltype(try_fromstring<T>(std::declval<cstring>())),
			void*
		>>
	read_item(T& out, float ov_resolut1)
	{
		out = try_fromstring<T>(thisval);
	}
	
	template<typename T> void read_item(array<T>& out, int ov_resolut1)
	{
		read_item(out.append(), 1);
	}
	
	template<typename T> void read_item(set<T>& out, int ov_resolut1)
	{
		T tmp;
		read_item(tmp, 1);
		out.add(std::move(tmp));
	}
	
	template<typename T> void read_item(array<array<T>>& item, int ov_resolut1) = delete;
	
	void to_next()
	{
		matchagain = false;
		
		if (pdepth >= thisdepth)
		{
			thisdepth--;
			skipchildren();
			
			bmlparser::event ev = event();
			if (ev.action == bmlparser::enter)
			{
				matchagain = true;
				thisnode = bmlwriter::unescape(ev.name);
				thisval = ev.value;
			}
			
			thisdepth++;
		}
	}
	
public:
	bmldeserializer(cstring bml) : p(bml) {}
	
	static const bool serializing = false;
	
	template<typename T> void item(cstring name, T& out)
	{
		while (thisnode == name) // this should be a loop, in case of documents like 'foo bar=1 bar=2 bar=3'
		{
			read_item(out, 1);
			thisnode = "";
			to_next();
		}
	}
	
	template<typename T> void item_next(T& out)
	{
		read_item(out, 1);
		thisnode = "";
		to_next();
	}
	
	bool enter(bool& first)
	{
		bmlparser::event ev(0);
		if (!first) goto l_matchagain;
		first = false;
		
		while (pdepth >= thisdepth)
		{
			ev = event();
			if (ev.action == bmlparser::enter)
			{
				thisdepth++;
				thisnode = bmlwriter::unescape(ev.name);
				thisval = ev.value;
				do {
					matchagain = false;
					return true;
				l_matchagain: ;
				} while (matchagain);
				thisdepth--;
				skipchildren();
			}
		}
		thisnode = "";
		to_next();
		return false;
	}
	
	bool enter(bool& first, cstring name)
	{
		if (first && name != thisnode) return false;
		return enter(first);
	}
	
	template<typename T> void hex(cstring name, T& out)
	{
		while (thisnode == name)
		{
			fromstringhex(thisval, out);
			thisnode = "";
			to_next();
		}
	}
	
	void hex(cstring name, arrayvieww<uint8_t> out)
	{
		while (thisnode == name)
		{
			fromstringhex(thisval, out);
			thisnode = "";
			to_next();
		}
	}
	
	cstring next() const { return thisnode; }
	cstring nextval() const { return thisval; }
	
	void comment(cstring c) {}
};

template<typename T> T bmldeserialize(cstring bml)
{
	T out{};
	bmldeserializer s(bml);
	s.read_item(out, 1);
	return out;
}

template<typename T> void bmldeserialize_to(cstring bml, T& to)
{
	bmldeserializer s(bml);
	s.read_item(to, 1);
}



template<int indent = 0, typename T> string jsonserialize(T& item);
template<int indent = 0, typename T> string jsonserialize(const T& item);

class jsonserializer {
	jsonwriter w;
	template<int indent, typename T> friend string jsonserialize(T& item);
	template<int indent, typename T> friend string jsonserialize(const T& item);
	
	int indent_max = 999999;
	
	jsonserializer(int indent) : w(indent) {}
	
	void list_enter()
	{
		w.list_enter();
		indent_max--;
		if (indent_max == -1) w.compress(true);
	}
	void list_exit()
	{
		if (indent_max == -1) w.compress(false);
		indent_max++;
		w.list_exit();
	}
	void map_enter()
	{
		w.map_enter();
		indent_max--;
		if (indent_max == -1) w.compress(true);
	}
	void map_exit()
	{
		if (indent_max == -1) w.compress(false);
		indent_max++;
		w.map_exit();
	}
	
	template<typename T>
	typename std::enable_if_t<
		!std::is_same_v<
			decltype(std::declval<T>().serialize(std::declval<jsonserializer&>())),
			void*
		>>
	add_node(T& inner, int ov_resolut1)
	{
		map_enter();
		inner.serialize(*this);
		map_exit();
	}
	
	template<typename T>
	typename std::enable_if_t<
		!std::is_same_v<
			typename T::serialize_as,
			void*
		>>
	add_node(const T& inner, int ov_resolut1)
	{
		add_node((typename T::serialize_as)inner, 1);
	}
	
	template<typename T>
	typename std::enable_if_t<
		!std::is_same_v<
			decltype(std::declval<T>()(std::declval<jsonserializer&>())),
			void*
		>>
	add_node(const T& inner, int ov_resolut1)
	{
		map_enter();
		inner(*this);
		map_exit();
	}
	
	template<typename T>
	typename std::enable_if_t<
		std::is_same_v<
			decltype(tostring(std::declval<T>())),
			string
		>>
	add_node(const T& inner, float ov_resolut1)
	{
		add_node(tostring(inner), 1);
	}
	
	void add_node(cstring inner, int ov_resolut1) { w.str(inner); }
	void add_node(const string& inner, int ov_resolut1) { w.str(inner); }
	void add_node(string& inner, int ov_resolut1) { w.str(inner); }
	
	template<typename T, size_t size>
	void add_node(T(&inner)[size], int ov_resolut1)
	{
		list_enter();
		for (auto& child : inner) add_node(child, 1);
		list_exit();
	}
	
	template<typename T> void add_node(array<T>& inner, int ov_resolut1)
	{
		list_enter();
		for (auto& child : inner) add_node(child, 1);
		list_exit();
	}
	
	template<typename T> void add_node(set<T>& inner, int ov_resolut1)
	{
		list_enter();
		for (auto& child : inner) add_node(child, 1);
		list_exit();
	}
	
	void add_node(bool inner, int ov_resolut1) { w.boolean(inner); }
	
	void add_node_hex(arrayvieww<uint8_t> inner) { w.str(tostringhex(inner)); }
	
#define LEAF(T) \
		void add_node(T inner, int ov_resolut1) { w.num((double)inner); } \
		void add_node_hex(T inner) { w.num((double)inner); }
	ALLNUMS(LEAF);
#undef LEAF
	
	template<typename T, typename Tc> void add_node(array<T>& inner, Tc& conv)
	{
		list_enter();
		for (auto& child : inner) add_node(conv(child), 1);
		list_exit();
	}
	
	template<typename T, typename Tc> void add_node(set<T>& inner, Tc& conv)
	{
		list_enter();
		for (auto& child : inner) add_node(conv(child), 1);
		list_exit();
	}
	
	template<typename T, typename Ts> void add_node(T& inner, Ts& ser)
	{
		map_enter();
		ser(*this);
		map_exit();
	}
	
public:
	
	static const bool serializing = true;
	
	template<typename T> void item(cstring name, T& inner) { w.map_key(name); add_node(inner, 1); }
	template<typename T> void hex( cstring name, T& inner) { w.map_key(name); add_node_hex(inner); }
	template<typename T> void item(cstring name, const T& inner) { w.map_key(name); add_node(inner, 1); }
	template<typename T> void hex( cstring name, const T& inner) { w.map_key(name); add_node_hex(inner); }
	
	template<typename T, typename Tc> void item(cstring name, T& inner, Tc& conv)
	{
		w.map_key(name);
		add_node(inner, conv);
	}
	
	template<typename... Args> void item_compact(int newmax, Args&&... args)
	{
		int prev_depth = indent_max;
		indent_max = min(newmax, indent_max);
		item(std::forward<Args>(args)...);
		indent_max = prev_depth;
	}
	
	template<typename T> void item_next(T& out) { abort(); } // illegal
	cstring next() const { abort(); }
	
	void comment(cstring c) {}
};

template<int indent /* = 0 */, typename T> string jsonserialize(T& item)
{
	jsonserializer s(indent);
	s.add_node(item, 1);
	return s.w.finish();
}

template<int indent /* = 0 */, typename T> string jsonserialize(const T& item)
{
	jsonserializer s(indent);
	s.add_node(item, 1);
	return s.w.finish();
}



template<typename T> T jsondeserialize(cstring json, bool* valid = nullptr);
class jsondeserializer {
	jsonparser p;
	jsonparser::event ev;
	bool matchagain;
	bool valid = true;
	
	jsondeserializer(cstring json) : p(json) {}
	template<typename T> friend T jsondeserialize(cstring json, bool* valid);
	template<typename T> friend bool jsondeserialize(cstring json, T& out);
	template<typename T> friend bool jsondeserialize(cstring json, const T& out); // this is super dumb, but lambdas need it somehow
	
	void next_ev()
	{
		ev = p.next();
		if (ev.action == jsonparser::error)
			valid = false;
	}
	
	//input: ev points to any node
	//output: if ev pointed to enter_map or enter_list, ev now points to corresponding exit; if not, no change
	void finish_item()
	{
		size_t nest = 0;
		while (true)
		{
			if (ev.action == jsonparser::enter_map || ev.action == jsonparser::enter_list) nest++;
			if (ev.action == jsonparser::exit_map || ev.action == jsonparser::exit_list) nest--;
			if (!nest) break;
			next_ev();
		}
	}
	
#define LEAF(T) void read_item(T& out, int ov_resolut1) { if (ev.action == jsonparser::num) out = ev.num; finish_item(); next_ev(); }
	ALLNUMS(LEAF);
#undef LEAF
	
	void read_item(string& out, int ov_resolut1)
	{
		if (ev.action == jsonparser::str) out = ev.str;
		finish_item();
		next_ev();
	}
	
	void read_item(bool& out, int ov_resolut1)
	{
		if (ev.action == jsonparser::jtrue) out = true;
		if (ev.action == jsonparser::jfalse) out = false;
		finish_item();
		next_ev();
	}
	
	template<typename T> void read_item(arrayvieww<T>& out, int ov_resolut1)
	{
		size_t pos = 0;
		if (ev.action == jsonparser::enter_list)
		{
			next_ev();
			while (ev.action != jsonparser::exit_list)
			{
				if (pos < out.size())
					read_item(out[pos++], 1);
				else
					finish_item();
			}
		}
		else finish_item();
		next_ev();
	}
	
	template<typename T, size_t size>
	void read_item(T(&out)[size], int ov_resolut1)
	{
		arrayvieww<T> outw = out;
		read_item(outw, 1);
	}
	
	template<typename T> void read_item(array<T>& out, int ov_resolut1)
	{
		out.reset();
		if (ev.action == jsonparser::enter_list)
		{
			next_ev();
			while (ev.action != jsonparser::exit_list)
			{
				read_item(out.append(), 1);
			}
		}
		else finish_item();
		next_ev();
	}
	
	template<typename T> void read_item(set<T>& out, int ov_resolut1)
	{
		out.reset();
		if (ev.action == jsonparser::enter_list)
		{
			next_ev();
			while (ev.action != jsonparser::exit_list)
			{
				T tmp;
				read_item(tmp, 1);
				out.add(std::move(tmp));
			}
		}
		else finish_item();
		next_ev();
	}
	
	template<typename T>
	typename std::enable_if_t<
		std::is_same_v<
			decltype(std::declval<T>().serialize(std::declval<jsondeserializer&>())),
			void
		>>
	read_item(T& out, int ov_resolut1)
	{
		if (ev.action == jsonparser::enter_map)
		{
			next_ev();
			while (ev.action != jsonparser::exit_map)
			{
				matchagain = false;
				//ev = map_key
				out.serialize(*this);
				if (!matchagain)
				{
					if (ev.action == jsonparser::map_key) // always true unless document is broken
						next_ev();
					if (ev.action == jsonparser::exit_map) break; // can happen if document is broken
					//ev = enter_map or whatever
					finish_item();
					//ev = exit_map or whatever
					next_ev();
					//ev = map_key or exit_map
				}
			}
		}
		else finish_item();
		next_ev();
	}
	
	template<typename T>
	typename std::enable_if_t<
		!std::is_same_v<
			typename T::serialize_as,
			void*
		>>
	read_item(T& out, int ov_resolut1)
	{
		typename T::serialize_as tmp{};
		read_item(tmp, 1);
		out = std::move(tmp);
	}
	
	template<typename T>
	typename std::enable_if_t<
		!std::is_same_v<
			decltype(std::declval<T&>() = std::declval<cstring>()),
			void*
		>>
	read_item(T& out, float ov_resolut1)
	{
		if (ev.action == jsonparser::str) out = ev.str;
		finish_item();
		next_ev();
	}
	
	template<typename T>
	typename std::enable_if_t<
		std::is_same_v<
			decltype(std::declval<T>()(std::declval<jsondeserializer&>())),
			void
		>>
	read_item(const T& inner, int ov_resolut1)
	{
		if (ev.action == jsonparser::enter_map)
		{
			next_ev();
			while (ev.action != jsonparser::exit_map)
			{
				matchagain = false;
				//ev = map_key
				inner(*this);
				if (!matchagain)
				{
					next_ev();
					if (ev.action == jsonparser::exit_map) break; // can happen if document is broken
					//ev = enter_map or whatever
					finish_item();
					//ev = exit_map or whatever
					next_ev();
					//ev = map_key or exit_map
				}
//if(ev.action==jsonparser::finish)*(char*)0=0;
			}
		}
		else finish_item();
		next_ev();
	}
	
public:
	
	bool enter(bool& first)
	{
		if (!first)
			goto l_matchagain;
		first = false;
		
		//ev = map_key
		next_ev();
		if (ev.action == jsonparser::enter_map)
		{
			next_ev();
			while (ev.action != jsonparser::exit_map)
			{
				matchagain = false;
				//ev = map_key
				return true;
			l_matchagain: ;
				if (!matchagain)
				{
					next_ev();
					if (ev.action == jsonparser::exit_map) break; // can happen if document is broken
					//ev = enter_map or whatever
					finish_item();
					//ev = exit_map or whatever
					next_ev();
					//ev = map_key or exit_map
				}
//if(ev.action==jsonparser::finish)*(char*)0=0;
			}
		}
		else finish_item();
		
		next_ev();
		matchagain = true;
		
		return false;
	}
	
	bool enter(bool& first, cstring name)
	{
		if (first && name != next()) return false;
		return enter(first);
	}
	
	static const bool serializing = false;
	
	template<typename T> void item(cstring name, T& out)
	{
//puts(tostring(ev.action)+","+tostring(jsonparser::map_key)+" "+ev.str+","+name);
		//this should be a loop, in case of documents like '{ "foo": 1, "foo": 2, "foo": 3 }'
		while (ev.action == jsonparser::map_key && ev.str == name)
		{
			next_ev();
			read_item(out, 1);
			matchagain = true;
//puts("::"+tostring(ev.action)+": "+tostring(jsonparser::map_key)+","+tostring(jsonparser::exit_map));
		}
	}
	
	template<typename... Ts> void item_compact(int newmax, Ts&&... args) { item(std::forward<Ts>(args)...); }
	
	template<typename T> void item_next(T& out)
	{
		if (ev.action != jsonparser::map_key)
			abort();
		next_ev();
		read_item(out, 1);
		matchagain = true;
	}
	
	template<typename T>
	typename std::enable_if_t<std::is_integral_v<T>>
	hex(cstring name, T& out)
	{
		while (ev.action == jsonparser::map_key && ev.str == name)
		{
			next_ev();
			if (ev.action == jsonparser::num) out = ev.num;
			finish_item();
			next_ev();
			matchagain = true;
		}
	}
	
	void hex(cstring name, arrayvieww<uint8_t> out)
	{
		while (ev.action == jsonparser::map_key && ev.str == name)
		{
			next_ev();
			if (ev.action == jsonparser::str)
			{
				fromstringhex(ev.str, out);
			}
			finish_item();
			next_ev();
			matchagain = true;
		}
	}
	
	void hex(cstring name, array<uint8_t>& out)
	{
		while (ev.action == jsonparser::map_key && ev.str == name)
		{
			next_ev();
			if (ev.action == jsonparser::str)
			{
				fromstringhex(ev.str, out);
			}
			finish_item();
			next_ev();
			matchagain = true;
		}
	}
	
	cstring next() const
	{
		if (ev.action == jsonparser::map_key) return ev.str;
		else return "";
	}
	
	void comment(cstring c) {}
};

template<typename T> T jsondeserialize(cstring json, bool* valid /*= nullptr*/)
{
	T out{};
	jsondeserializer s(json);
	s.ev = s.p.next();
	s.read_item(out, 1);
	if (valid) *valid = s.valid;
	return out;
}

template<typename T> bool jsondeserialize(cstring json, T& out)
{
	jsondeserializer s(json);
	s.ev = s.p.next();
	s.read_item(out, 1);
	return s.valid;
}

template<typename T> bool jsondeserialize(cstring json, const T& out)
{
	jsondeserializer s(json);
	s.ev = s.p.next();
	s.read_item(out, 1);
	return s.valid;
}
