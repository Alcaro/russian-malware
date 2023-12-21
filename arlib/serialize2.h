#include "json.h"

// A basic serialize() function looks like
/*
void serialize(auto& s)
{
	SER_ENTER(s)
	{
		s.item("a", a);
		s.item("b", b);
		s.item_hex("c", c);
		SER_IF(s, d > 0) s.item("d", d); // Omits the object in the serialized form if the condition is false. For deserializing, it's read.
		SER_NAME("e") { s.item(e); } // Same as s.item("e", e)
		SER_NAME("f") { f.serialize(s, this); } // s.item(f) expands to f.serialize(s) if that function exists.
	}
}
*/

// Members of the serialize(T& s) argument:
#if 0
class serializer {
public:
	static const bool serializing;
	
	void item(auto& item);
	void item(cstring name, auto& item);
	void item_hex(auto& item);
	void item_hex(cstring name, auto& item);
	
	// If serializing:
	void enter_array();
	void exit_array();
	void compress(); // JSON only.
	void uncompress(); // JSON only.
	string finish(); // Only call once.
	
	// If deserializing:
	bool has_item(); // Whether the current object has any more children.
	cstring get_name(); // Name of the current object, or blank string if none.
	bool valid(); // Whether there were any parse errors or type mismatches in the document.
};
#endif
// plus some private functions used by the SER_ macros.

#define SER_ENTER(s) for (bool _continue=(s)._begin();_continue;_continue=(s)._continue())
#define SER_ENTER_ARRAY(s) \
	for (bool _continue=(s)._begin_array(); _continue; (_continue && ((s)._cancel_array(), _continue=false))) \
		for (; _continue; _continue=(s)._continue_array())
#define SER_NAME(s, name) for (bool _step=(s)._name(name);_step;_step=(!(s).serializing && (s)._name(name)))
#define SER_IF(s, cond) if (!(s).serializing || cond)

#define SERIALIZE_LOOP(member) , STR(member), member
#define SERIALIZE(first, ...) \
	template<typename Ts> void serialize(Ts& s) { s.items(STR(first), first PPFOREACH(SERIALIZE_LOOP, __VA_ARGS__)); }

template<typename Tp>
class serializer_base : nocopy {
	Tp& self()
	{
		static_assert(std::is_base_of_v<serializer_base, Tp>);
		return *(Tp*)this;
	}
	
	template<typename T>
	void item_inner(T& val)
	{
		if constexpr (requires { val.serialize2(self()); })
			val.serialize2(self());
		else if constexpr (requires { val.serialize(self()); })
			val.serialize(self());
		else if constexpr (requires { typename T::serialize_as; })
		{
			if constexpr (Tp::serializing)
			{
				if constexpr (std::is_same_v<typename T::serialize_as, string>)
					item(tostring(val));
				else
					item((typename T::serialize_as)val);
			}
			else
			{
				typename T::serialize_as tmp{};
				item(tmp);
				if constexpr (std::is_same_v<typename T::serialize_as, string>)
					self()._valid &= fromstring(tmp, val);
				else
					val = std::move(tmp);
			}
		}
		else if constexpr (std::is_array_v<T> && !std::is_same_v<std::remove_cv_t<std::remove_extent_t<T>>, char>)
		{
			if constexpr (Tp::serializing)
			{
				self().enter_array();
				for (auto& i : val)
					item(i);
				self().exit_array();
			}
			else
			{
				size_t n = 0;
				SER_ENTER_ARRAY(self())
				{
					item(val[n++]);
					if (n == ARRAY_SIZE(val))
						break;
				}
			}
		}
		else self()._primitive(val);
	}
	
public:
	template<typename T> void item(T& val) { item_inner<T>(val); }
	template<typename T> void item(const T& val) { item_inner<const T>(val); }
	template<typename T> void item(const char * name, T& val) { SER_NAME(self(), name) { item(val); } }
	template<typename T> void item(const char * name, const T& val) { SER_NAME(self(), name) { item(val); } }
	template<typename T> void item_hex(const char * name, T& val) { SER_NAME(self(), name) { self().item_hex(val); } }
	template<typename T> void item_hex(const char * name, const T& val) { SER_NAME(self(), name) { self().item_hex(val); } }
	
private:
	void items_inner() {}
	template<typename Ti, typename... Ts>
	void items_inner(const char * name, Ti& inner, Ts&&... args)
	{
		item(name, inner);
		items_inner(std::forward<Ts>(args)...);
	}
	template<typename Ti, typename... Ts>
	void items_inner(const char * name, const Ti& inner, Ts&&... args)
	{
		item(name, inner);
		items_inner(std::forward<Ts>(args)...);
	}
public:
	// todo: delete this function, should either be SERIALIZE macro or expanded to SER_ENTER
	// (will need to delete serialize.h first, so SERIALIZE can be changed)
	template<typename... Ts>
	void items(Ts&&... args)
	{
		SER_ENTER(self()) { items_inner(std::forward<Ts>(args)...); }
	}
};


class jsonserializer2 : public serializer_base<jsonserializer2> {
public:
	static const bool serializing = true;
private:
	jsonwriter w;
	
public:
	jsonserializer2(int indent = 0) : w(indent) {}
	
	bool _begin() { w.map_enter(); return true; }
	bool _continue() { w.map_exit(); return false; }
	bool _name(cstring name) { w.map_key(name); return true; }
	
	template<typename T>
	void _primitive(T& val)
	{
		if constexpr (std::is_same_v<T, bool>)
			w.boolean(val);
		else if constexpr (std::is_integral_v<T>)
			w.num(val);
		else if constexpr (requires { tostring(val); })
			w.str(tostring(val));
		else
			static_assert(sizeof(T) < 0);
	}
	
	template<size_t n>
	void item_hex(const uint8_t val[n])
	{
		item_hex(bytesr(val));
	}
	void item_hex(bytesr val)
	{
		w.str(tostringhex(val));
	}
	
	void enter_array() { w.list_enter(); }
	void exit_array() { w.list_exit(); }
	
	void compress() { w.compress(); }
	void uncompress() { w.uncompress(); }
	string finish() { return w.finish(); }
};

template<int indent = 0, typename T> string jsonserialize2(T& item)
{
	jsonserializer2 s(indent);
	s.item(item);
	return s.finish();
}
template<int indent = 0, typename T> string jsonserialize2(const T& item)
{
	jsonserializer2 s(indent);
	s.item(item);
	return s.finish();
}

template<size_t N, typename Ts>
class compacting_serializer : public serializer_base<compacting_serializer<N, Ts>> {
public:
	static const bool serializing = true;
	Ts& inner;
	compacting_serializer(Ts& inner) : inner(inner) {}
	
	template<typename T>
	void item(T& i)
	{
		if constexpr (N > 0)
		{
			compacting_serializer<N-1, Ts> next(inner);
			next.serializer_base<compacting_serializer<N-1, Ts>>::item(i);
		}
		else
		{
			inner.item(i);
		}
	}
	
	// need to copy those two, since I overrode item()
	template<typename T> void item(const char * name, T& val) { SER_NAME(*this, name) { item(val); } }
	template<typename T> void item(const char * name, const T& val) { SER_NAME(*this, name) { item(val); } }
	
	bool _begin()
	{
		inner._begin();
		if (N == 0)
			inner.compress();
		return true;
	}
	bool _continue()
	{
		inner._continue();
		if (N == 0)
			inner.uncompress();
		return false;
	}
	bool _name(cstring name) { return inner._name(name); }
	
	void enter_array()
	{
		inner.enter_array();
		if (N == 0)
			inner.compress();
	}
	void exit_array()
	{
		inner.exit_array();
		if (N == 0)
			inner.uncompress();
	}
};
template<size_t N, typename Ts>
auto ser_compact2(Ts& s)
{
	if constexpr (Ts::serializing)
		return compacting_serializer<N+1, Ts>(s);
	else
		return s;
}


template<typename Tparser>
class jsondeserializer2_base : public serializer_base<jsondeserializer2_base<Tparser>> {
public:
	static const bool serializing = false;
private:
	Tparser p;
	jsonparser::event ev;
	
	cstring current_name;
	bool has_current_name;
	
	bool did_anything;
public:
	bool _valid = true;
private:
	
	// serialize() is entered with ev pointing to the first object after map/list_enter (often a map_key)
	
	void next_ev_raw()
	{
	again:
		ev = p.next();
		if (ev.type == jsonparser::error)
		{
			_valid = false;
			has_current_name = false;
			goto again;
		}
	}
	
	void next_ev()
	{
		has_current_name = false;
		current_name = "";
		next_ev_raw();
		if (ev.type == jsonparser::map_key)
		{
			has_current_name = true;
			current_name = ev.str;
			next_ev_raw();
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
	
public:
	jsondeserializer2_base(cstring json) : p(json) { next_ev(); }
	
	bool _begin()
	{
		if (ev.type != jsonparser::enter_map)
		{
			_valid = false;
			finish_item();
			next_ev();
			did_anything = true;
			return false;
		}
		next_ev();
		if (ev.type == jsonparser::exit_map)
		{
			did_anything = true;
			next_ev();
			return false;
		}
		did_anything = false;
		return true;
	}
	bool _continue()
	{
		if (!did_anything)
		{
			// points to list_enter
			finish_item(); // points to list_exit
			next_ev(); // points to whatever's after map_key
		}
		if (ev.type == jsonparser::exit_map)
		{
			did_anything = true;
			next_ev();
			return false;
		}
		did_anything = false;
		return true;
	}
	cstring get_name()
	{
		return current_name;
	}
	bool _name(cstring name)
	{
		return (has_current_name && current_name == name);
	}
	
	bool _begin_array()
	{
		if (ev.type != jsonparser::enter_list)
		{
			_valid = false;
			finish_item();
			next_ev();
			did_anything = true;
			return false;
		}
		did_anything = false;
		next_ev();
		if (ev.type == jsonparser::exit_list)
		{
			did_anything = true;
			next_ev();
			return false;
		}
		return true;
	}
	bool _continue_array()
	{
		if (!did_anything)
		{
			// points to list_enter
			finish_item(); // points to list_exit
			next_ev(); // points to next item
		}
		if (ev.type != jsonparser::exit_list)
		{
			did_anything = false;
			return true;
		}
		else
		{
			did_anything = true;
			next_ev();
			return false;
		}
	}
	void _cancel_array()
	{
		// points to something inside the list
		finish_item(1); // points to list_exit
		next_ev(); // points to map_key
	}
	bool has_item()
	{
		return (ev.type != jsonparser::exit_list && ev.type != jsonparser::exit_map);
	}
	
	template<typename T>
	void _primitive(T& val)
	{
		if constexpr (std::is_same_v<T, bool>)
		{
			if (ev.type == jsonparser::jtrue)
				val = true;
			else if (ev.type == jsonparser::jfalse)
				val = false;
			else
				_valid = false;
		}
		else if constexpr (std::is_integral_v<T>)
		{
			if (ev.type == jsonparser::num) _valid &= fromstring(ev.str, val);
			else _valid = false;
		}
		else if constexpr (requires { fromstring("", val); })
		{
			if (ev.type == jsonparser::str) _valid &= fromstring(ev.str, val);
			else _valid = false;
		}
		else
			static_assert(sizeof(T) < 0);
		
		did_anything = true;
		finish_item();
		next_ev();
	}
	
	template<size_t n>
	void item_hex(uint8_t (&val)[n])
	{
		item_hex(bytesw(val));
	}
	void item_hex(bytesw val)
	{
		if (ev.type == jsonparser::str) _valid &= fromstringhex(ev.str, val);
		else _valid = false;
		did_anything = true;
		finish_item();
		next_ev();
	}
	void item_hex(bytearray& val)
	{
		if (ev.type == jsonparser::str) _valid &= fromstringhex(ev.str, val);
		else _valid = false;
		did_anything = true;
		finish_item();
		next_ev();
	}
	
	bool valid() { return _valid && (ev.type == jsonparser::finish); }
};
using jsondeserializer2 = jsondeserializer2_base<jsonparser>;
using json5deserializer2 = jsondeserializer2_base<json5parser>;

template<typename T> T jsondeserialize2(cstring json, bool* valid = nullptr)
{
	T out{};
	jsondeserializer2 s(json);
	s.item(out);
	if (valid) *valid = s.valid();
	return out;
}
template<typename T> T json5deserialize2(cstring json, bool* valid = nullptr)
{
	T out{};
	json5deserializer2 s(json);
	s.item(out);
	if (valid) *valid = s.valid();
	return out;
}
