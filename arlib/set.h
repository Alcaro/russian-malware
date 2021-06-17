#pragma once
#include "global.h"
#include "array.h"
#include "hash.h"
#include "linqbase.h"
#include "string.h"

template<typename T>
class set : public linqbase<set<T>> {
	//this is a hashtable, using open addressing and linear probing
	
	enum { i_empty, i_deleted };
	// char can alias anything, so use that for tag type; the above two are the only valid tags
	char& tag(size_t id) { return *(char*)(m_data+id); }
	char tag(size_t id) const { return *(char*)(m_data+id); }
	
	T* m_data; // length is always same as m_valid, no need to duplicate its length
	bitarray m_valid;
	size_t m_count;
	
	void rehash(size_t newsize)
	{
//debug("rehash pre");
		T* prev_data = m_data;
		bitarray prev_valid = std::move(m_valid);
		
		m_data = xcalloc(newsize, sizeof(T));
		static_assert(sizeof(T) >= 1); // otherwise the tags mess up (zero-size objects are useless in sets, 
		m_valid.reset();               // and I'm not sure if they're expressible in standard C++, but no reason not to check)
		m_valid.resize(newsize);
		
		for (size_t i=0;i<prev_valid.size();i++)
		{
			if (!prev_valid[i]) continue;
			
			size_t pos = find_pos_full<true, false>(prev_data[i]);
			//this is known to not overwrite any existing object; if it does, someone screwed up
			memcpy((void*)&m_data[pos], (void*)&prev_data[i], sizeof(T));
			m_valid[pos] = true;
		}
		free(prev_data);
//debug("rehash post");
	}
	
	void grow()
	{
		// half full -> rehash
		if (m_count >= m_valid.size()/2) rehash(m_valid.size()*2);
	}
	
	bool slot_empty(size_t pos) const
	{
		return !m_valid[pos];
	}
	
	//If the object exists, returns the index where it can be found.
	//If not, and want_empty is true, returns a suitable empty slot to insert it in, or -1 if the object should rehash.
	//If no such object and want_empty is false, returns -1.
	template<bool want_empty, bool want_used = true, typename T2>
	size_t find_pos_full(const T2& item) const
	{
		if (!m_data) return -1;
		
		size_t hashv = hash_shuffle(hash(item));
		size_t i = 0;
		
		size_t emptyslot = -1;
		
		while (true)
		{
			//I could use hashv + i+(i+1)/2 <http://stackoverflow.com/a/15770445>
			//but due to hash_shuffle, it hurts as much as it helps.
			size_t pos = (hashv + i) & (m_valid.size()-1);
			if (want_used && m_valid[pos] && m_data[pos] == item) return pos;
			if (!m_valid[pos])
			{
				if (emptyslot == (size_t)-1) emptyslot = pos;
				if (tag(pos) == i_empty)
				{
					if (want_empty) return emptyslot;
					else return -1;
				}
			}
			i++;
			if (i == m_valid.size())
			{
				//happens if all empty slots are i_deleted, no i_empty
				return -1;
			}
		}
	}
	
	template<typename T2>
	size_t find_pos_const(const T2& item) const
	{
		return find_pos_full<false>(item);
	}
	
	template<typename T2>
	size_t find_pos_insert(const T2& item)
	{
		size_t pos = find_pos_full<true>(item);
		if (pos == (size_t)-1)
		{
			rehash(m_valid.size());
			//after rehashing, there is always a suitable empty slot
			return find_pos_full<true, false>(item);
		}
		return pos;
	}
	
	template<typename,typename>
	friend class map;
	//used by map
	//if the item doesn't exist, NULL
	template<typename T2>
	T* get_or_null(const T2& item) const
	{
		size_t pos = find_pos_const(item);
		if (pos != (size_t)-1) return &m_data[pos];
		else return NULL;
	}
	//also used by map
	template<typename T2>
	T& get_create(const T2& item, bool known_new = false)
	{
		size_t pos = known_new ? -1 : find_pos_insert(item);
		
		if (pos == (size_t)-1 || !m_valid[pos])
		{
			grow();
			pos = find_pos_insert(item); // recalculate this, grow() may have moved it
			//do not move grow() earlier; it invalidates references, get_create(item_that_exists) is not allowed to do that
			
			new(&m_data[pos]) T(item);
			m_valid[pos] = true;
			m_count++;
		}
		
		return m_data[pos];
	}
	
	void construct()
	{
		m_data = NULL;
		m_valid.resize(8);
		m_count = 0;
	}
	void construct(const set& other)
	{
		m_data = xcalloc(other.m_valid.size(), sizeof(T));
		m_valid = other.m_valid;
		m_count = other.m_count;
		
		for (size_t i=0;i<m_valid.size();i++)
		{
			if (m_valid[i])
			{
				new(&m_data[i]) T(other.m_data[i]);
			}
		}
	}
	void construct(set&& other)
	{
		m_data = std::move(other.m_data);
		m_valid = std::move(other.m_valid);
		m_count = std::move(other.m_count);
		
		other.construct();
	}
	
	void destruct()
	{
		for (size_t i=0;i<m_valid.size();i++)
		{
			if (m_valid[i])
			{
				m_data[i].~T();
			}
		}
		m_count = 0;
		free(m_data);
		m_valid.reset();
	}
	
public:
	set() { construct(); }
	set(const set& other) { construct(other); }
	set(set&& other) { construct(std::move(other)); }
	set(std::initializer_list<T> c)
	{
		construct();
		for (const T& item : c) add(item);
	}
	set& operator=(const set& other) { destruct(); construct(other); return *this; }
	set& operator=(set&& other) { destruct(); construct(std::move(other)); return *this; }
	~set() { destruct(); }
	
	template<typename T2>
	void add(const T2& item)
	{
		get_create(item);
	}
	
	template<typename T2>
	bool contains(const T2& item) const
	{
		size_t pos = find_pos_const(item);
		return pos != (size_t)-1;
	}
	
	template<typename T2>
	void remove(const T2& item)
	{
		size_t pos = find_pos_const(item);
		if (pos == (size_t)-1) return;
		
		m_data[pos].~T();
		tag(pos) = i_deleted;
		m_valid[pos] = false;
		m_count--;
		if (m_count < m_valid.size()/4 && m_valid.size() > 8) rehash(m_valid.size()/2);
	}
	
	size_t size() const { return m_count; }
	
	void reset() { destruct(); construct(); }
	
	class iterator {
		friend class set;
		
		const set* parent;
		size_t pos;
		
		void to_valid()
		{
			while (pos < parent->m_valid.size() && !parent->m_valid[pos]) pos++;
			if (pos == parent->m_valid.size()) pos = -1;
		}
		
		iterator(const set<T>* parent, size_t pos) : parent(parent), pos(pos)
		{
			if (pos != (size_t)-1) to_valid();
		}
		
	public:
		
		const T& operator*()
		{
			return parent->m_data[pos];
		}
		
		iterator& operator++()
		{
			pos++;
			to_valid();
			return *this;
		}
		
		bool operator!=(const iterator& other)
		{
			return (this->parent != other.parent || this->pos != other.pos);
		}
	};
	
	//messing with the set during iteration half-invalidates all iterators
	//a half-invalid iterator may return values you've already seen and may skip values, but will not crash or loop forever
	//exception: you may not dereference a half-invalid iterator, use operator++ first
	//'for (T i : my_set) { my_set.remove(i); }' is safe, but is not guaranteed to remove everything
	iterator begin() const { return iterator(this, 0); }
	iterator end() const { return iterator(this, -1); }

//string debug_node(int n) { return tostring(n); }
//string debug_node(string& n) { return n; }
//template<typename T2> string debug_node(T2& n) { return "?"; }
//void debug(const char * why)
//{
//puts("---");
//for (size_t i=0;i<m_data.size();i++)
//{
//	printf("%s %lu: valid %d, tag %d, data %s, found slot %lu\n",
//		why, i, (bool)m_valid[i], m_data[i].tag(), (const char*)debug_node(m_data[i].member()), find_pos(m_data[i].member()));
//}
//puts("---");
//}
	
	static constexpr bool serialize_as_array() { return true; }
	template<typename Ts> void serialize(Ts& s)
	{
		if constexpr (Ts::serializing)
		{
			for (const T& child : *this)
				s.item(child);
		}
		else
		{
			while (s.has_item())
			{
				T tmp;
				s.item(tmp);
				add(std::move(tmp));
			}
		}
	}
};



template<typename Tkey, typename Tvalue>
class map : public linqbase<map<Tkey,Tvalue>> {
public:
	struct node {
		const Tkey key;
		Tvalue value;
		
		node() : key(), value() {}
		node(const Tkey& key) : key(key), value() {}
		node(const Tkey& key, const Tvalue& value) : key(key), value(value) {}
		//these silly things won't work
		//node(Tkey&& key) : key(std::move(key)), value() {}
		//node(Tkey&& key, const Tvalue& value) : key(std::move(key)), value(value) {}
		//node(const Tkey& key, Tvalue&& value) : key(key), value(std::move(value)) {}
		//node(Tkey&& key, Tvalue&& value) : key(std::move(key)), value(std::move(value)) {}
		//node(node other) : key(other.key), value(other.value) {}
		
		size_t hash() const { return ::hash(key); }
		bool operator==(const Tkey& other) { return key == other; }
		bool operator==(const node& other) { return key == other.key; }
	};
private:
	set<node> items;
	
public:
	//map() {}
	//map(const map& other) : items(other.items) {}
	//map(map&& other) : items(std::move(other.items)) {}
	//map& operator=(const map& other) { items = other.items; }
	//map& operator=(map&& other) { items = std::move(other.items); }
	//~map() { destruct(); }
	
	//can't call it set(), conflict with set<>
	void insert(const Tkey& key, const Tvalue& value)
	{
		node* item = items.get_or_null(key);
		if (item) item->value = value;
		else items.get_create(node(key, value), true);
	}
	
	//if nonexistent, null deref (undefined behavior, segfault in practice)
	template<typename Tk2>
	Tvalue& get(const Tk2& key)
	{
		return items.get_or_null(key)->value;
	}
	template<typename Tk2>
	const Tvalue& get(const Tk2& key) const
	{
		return items.get_or_null(key)->value;
	}
	
	//if nonexistent, returns 'def'
	template<typename Tk2>
	Tvalue& get_or(const Tk2& key, Tvalue& def)
	{
		node* ret = items.get_or_null(key);
		if (ret) return ret->value;
		else return def;
	}
	template<typename Tk2>
	const Tvalue& get_or(const Tk2& key, const Tvalue& def) const
	{
		node* ret = items.get_or_null(key);
		if (ret) return ret->value;
		else return def;
	}
	template<typename Tk2>
	Tvalue get_or(const Tk2& key, Tvalue&& def) const
	{
		node* ret = items.get_or_null(key);
		if (ret) return ret->value;
		else return def;
	}
	template<typename Tk2> // sizeof && because not using Tk2 is a hard error, not a SFINAE
	std::enable_if_t<sizeof(Tk2) && std::is_same_v<Tvalue, string>, cstring>
	get_or(const Tk2& key, const char * def) const
	{
		node* ret = items.get_or_null(key);
		if (ret) return ret->value;
		else return def;
	}
	template<typename Tk2>
	Tvalue* get_or_null(const Tk2& key)
	{
		node* ret = items.get_or_null(key);
		if (ret) return &ret->value;
		else return NULL;
	}
	template<typename Tk2>
	const Tvalue* get_or_null(const Tk2& key) const
	{
		node* ret = items.get_or_null(key);
		if (ret) return &ret->value;
		else return NULL;
	}
	Tvalue& get_create(const Tkey& key)
	{
		return items.get_create(key).value;
	}
	Tvalue& operator[](const Tkey& key) // C# does this better...
	{
		return get(key);
	}
	
	Tvalue& insert(const Tkey& key)
	{
		return get_create(key);
	}
	
	template<typename Tk2>
	bool contains(const Tk2& item) const
	{
		return items.contains(item);
	}
	
	template<typename Tk2>
	void remove(const Tk2& item)
	{
		items.remove(item);
	}
	
	void reset()
	{
		items.reset();
	}
	
	size_t size() const { return items.size(); }
	
private:
	class iterator {
		typename set<node>::iterator it;
	public:
		iterator(typename set<node>::iterator it) : it(it) {}
		
		node& operator*() { return const_cast<node&>(*it); }
		iterator& operator++() { ++it; return *this; }
		bool operator!=(const iterator& other) { return it != other.it; }
	};
	
	class c_iterator {
		typename set<node>::iterator it;
	public:
		c_iterator(typename set<node>::iterator it) : it(it) {}
		
		const node& operator*() { return *it; }
		c_iterator& operator++() { ++it; return *this; }
		bool operator!=(const c_iterator& other) { return it != other.it; }
	};
	
	class k_iterator {
		typename set<node>::iterator it;
	public:
		k_iterator(typename set<node>::iterator it) : it(it) {}
		
		const Tkey& operator*() { return (*it).key; }
		k_iterator& operator++() { ++it; return *this; }
		bool operator!=(const k_iterator& other) { return it != other.it; }
	};
	
	class v_iterator {
		typename set<node>::iterator it;
	public:
		v_iterator(typename set<node>::iterator it) : it(it) {}
		
		Tvalue& operator*() { return const_cast<Tvalue&>((*it).value); }
		v_iterator& operator++() { ++it; return *this; }
		bool operator!=(const v_iterator& other) { return it != other.it; }
	};
	
	class cv_iterator {
		typename set<node>::iterator it;
	public:
		cv_iterator(typename set<node>::iterator it) : it(it) {}
		
		const Tvalue& operator*() { return (*it).value; }
		cv_iterator& operator++() { ++it; return *this; }
		bool operator!=(const cv_iterator& other) { return it != other.it; }
	};
	
public:
	//messing with the map during iteration half-invalidates all iterators
	//a half-invalid iterator may return values you've already seen and may skip values, but will not crash or loop forever
	//exception: you may not dereference a half-invalid iterator, use operator++ first
	
	iterator begin() { return items.begin(); }
	iterator end() { return items.end(); }
	c_iterator begin() const { return items.begin(); }
	c_iterator end() const { return items.end(); }
	
	iterwrap<k_iterator> keys() const { return items; }
	iterwrap<v_iterator> values() { return items; }
	iterwrap<cv_iterator> values() const { return items; }
	
	template<typename Ts>
	void serialize(Ts& s)
	{
		if constexpr (Ts::serializing)
		{
			for (node& p : *this)
			{
				s.item(tostring(p.key), p.value); // stringconv.h isn't included at this point, but somehow it works
			}
		}
		else
		{
			while (s.has_item())
			{
				Tkey tmpk;
				if (fromstring(s.name(), tmpk))
					s.item(get_create(tmpk));
			}
		}
	}
};
