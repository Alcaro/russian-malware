#pragma once
#include "global.h"
#include <new>
#include <string.h>
#include <type_traits>
#include "linqbase.h"

template<typename T> class arrayview;
template<typename T> class arrayvieww;
template<typename T> class array;
class cstring;

//size: two pointers
//this object does not own its storage, it's just a pointer wrapper
template<typename T> class arrayview : public linqbase<arrayview<T>> {
protected:
	T * items = NULL; // not const, despite not necessarily being writable; this makes arrayvieww/array a lot simpler
	size_t count = 0;
	
protected:
	// static variables clog gdb output, so extra () it is
	static constexpr bool trivial_cons() { return std::is_trivial_v<T>; } // constructor is memset(0)
	static constexpr bool trivial_copy() { return std::is_trivially_copyable_v<T>; } // copy constructor is memcpy
	static constexpr bool trivial_comp() { return std::has_unique_object_representations_v<T>; } // equality comparison is memcmp
	static constexpr bool trivial_dtor() { return std::is_trivially_destructible_v<T>; } // destructor does nothing
	
public:
	const T& operator[](size_t n) const { return items[n]; }
	
	//if out of range, returns 'def'
	const T& get_or(size_t n, const T& def) const
	{
		if (n < count) return items[n];
		else return def;
	}
	T get_or(size_t n, T&& def) const
	{
		if (n < count) return items[n];
		else return def;
	}
	inline cstring get_or(size_t n, const char * def) const; // implemented in string.h because circular dependency
	
	const T* ptr() const { return items; }
	size_t size() const { return count; }
	
	explicit operator bool() const { return count; }
	
	arrayview() = default;
	
	arrayview(nullptr_t)
	{
		this->items = NULL;
		this->count = 0;
	}
	
	arrayview(const T * ptr, size_t count)
	{
		this->items = (T*)ptr;
		this->count = count;
	}
	
	template<size_t N> arrayview(const T (&ptr)[N])
	{
		this->items = (T*)ptr;
		this->count = N;
	}
	
	arrayview<T> slice(size_t first, size_t count) const { return arrayview<T>(this->items+first, count); }
	arrayview<T> skip(size_t n) const { return slice(n, this->count-n); }
	
	T join() const
	{
		if (!this->count) return T();
		
		T out = this->items[0];
		for (size_t n=1;n<this->count;n++)
		{
			out += this->items[n];
		}
		return out;
	}
	
	template<typename T2>
	decltype(std::declval<T>() + std::declval<T2>()) join(T2 between) const
	{
		if (!this->count) return decltype(T() + T2())();
		
		decltype(T() + T2()) out = this->items[0];
		for (size_t n=1;n < this->count;n++)
		{
			out += between;
			out += this->items[n];
		}
		return out;
	}
	
	template<typename T2, typename Tc>
	T2 join(T2 between, Tc conv) const
	{
		if (!this->count) return T2();
		
		T2 out = conv(this->items[0]);
		for (size_t n=1;n < this->count;n++)
		{
			out += between;
			out += conv(this->items[n]);
		}
		return out;
	}
	
	//WARNING: Keep track of endianness if using this.
	template<typename T2> arrayview<T2> reinterpret() const
	{
		//reject reinterpret<string>() and other tricky stuff
		static_assert(std::is_fundamental_v<T>);
		static_assert(std::is_fundamental_v<T2>);
		
		size_t newsize = this->count*sizeof(T)/sizeof(T2);
		return arrayview<T2>((T2*)this->items, newsize);
	}
	
	template<typename T2> inline array<T2> cast() const;
	
private:
	const T* find_inner(const T& item) const
	{
		if constexpr (std::is_same_v<T, char> || std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t>)
		{
			uint8_t* buf = (uint8_t*)this->items;
			return (T*)memchr(buf, (uint8_t)item, this->count);
		}
		for (size_t n=0;n<this->count;n++)
		{
			if (this->items[n] == item) return &this->items[n];
		}
		return nullptr;
	}
public:
	size_t find(const T& item) const
	{
		const T * found = find_inner(item);
		if (found) return found - this->items;
		else return -1;
	}
	bool contains(const T& item) const
	{
		return find_inner(item) != nullptr;
	}
	
	bool operator==(arrayview<T> other) const
	{
		if (size() != other.size()) return false;
		if (this->trivial_comp())
		{
			return memeq(ptr(), other.ptr(), sizeof(T)*size());
		}
		else
		{
			for (size_t i=0;i<size();i++)
			{
				if (!(items[i]==other[i])) return false;
			}
			return true;
		}
	}
	
	bool operator!=(arrayview<T> other) const
	{
		return !(*this == other);
	}
	
	const T* begin() const { return this->items; }
	const T* end() const { return this->items+this->count; }
	
	
	static constexpr bool serialize_as_array() { return true; }
	template<typename Ts>
	std::enable_if_t<Ts::serializing>
	serialize(Ts& s)
	{
		for (size_t i=0;i<size();i++)
		{
			s.item(items[i]);
		}
	}
};

//size: two pointers
//this one can write its storage, but doesn't own the storage itself
template<typename T> class arrayvieww : public arrayview<T> {
	//T * items;
	//size_t count;
public:
	
	T& operator[](size_t n) { return this->items[n]; }
	const T& operator[](size_t n) const { return this->items[n]; }
	
	T* ptr() { return this->items; }
	const T* ptr() const { return this->items; }
	
	arrayvieww() = default;
	
	arrayvieww(nullptr_t)
	{
		this->items = NULL;
		this->count = 0;
	}
	
	arrayvieww(T * ptr, size_t count)
	{
		this->items = ptr;
		this->count = count;
	}
	
	template<size_t N> arrayvieww(T (&ptr)[N])
	{
		this->items = ptr;
		this->count = N;
	}
	
	arrayvieww(const arrayvieww<T>& other)
	{
		this->items = other.items;
		this->count = other.count;
	}
	
	arrayvieww<T> operator=(arrayvieww<T> other)
	{
		this->items = other.items;
		this->count = other.count;
		return *this;
	}
	
	arrayview<T> slice(size_t first, size_t count) const { return arrayview<T>(this->items+first, count); }
	arrayview<T> skip(size_t n) const { return slice(n, this->count-n); }
	arrayvieww<T> slice(size_t first, size_t count) { return arrayvieww<T>(this->items+first, count); }
	arrayvieww<T> skip(size_t n) { return slice(n, this->count-n); }
	
	void swap(size_t a, size_t b)
	{
		if (a == b) return;
		
		char tmp[sizeof(T)];
		memcpy(tmp, (void*)(this->items+a), sizeof(T));
		memcpy((void*)(this->items+a), (void*)(this->items+b), sizeof(T));
		memcpy((void*)(this->items+b), tmp, sizeof(T));
	}
	
	//unstable sort - default because equivalent but nonidentical states are rare
	template<typename Tless>
	void sort(const Tless& less)
	{
		// TODO: less lazy
		ssort(less);
	}
	
	void sort()
	{
		// TODO: if integer, use radix sort
		sort([](const T& a, const T& b) { return a < b; });
	}
	
	//stable sort
	//less() is guaranteed to only be called with a later than b in the input
	template<typename Tless>
	void ssort(const Tless& less)
	{
		// insertion sort; not the fastest, but it's adaptive, it's simple, and its constant factors are good
		// no real point upgrading to something faster, stable sort is rare
		for (size_t a=1;a<this->count;a++)
		{
			if (!less(this->items[a], this->items[a-1]))
				continue;
			
			size_t probe = 1;
			while (probe < a && less(this->items[a], this->items[a-probe]))
				probe *= 2;
			
			size_t min = a-probe;
			probe /= 2;
			while (probe)
			{
				if (min+probe > a || !less(this->items[a], this->items[min+probe]))
					min += probe;
				probe /= 2;
			}
			
			if (min > a || !less(this->items[a], this->items[min]))
				min++;
			
			size_t newpos = min;
			
			char tmp[sizeof(T)];
			memcpy((void*)tmp, (void*)&this->items[a], sizeof(T));
			memmove((void*)&this->items[newpos+1], (void*)&this->items[newpos], sizeof(T)*(a-newpos));
			memcpy((void*)&this->items[newpos], tmp, sizeof(T));
		}
	}
	
	void ssort()
	{
		ssort([](const T& a, const T& b) { return a < b; });
	}
	
	const T* begin() const { return this->items; }
	const T* end() const { return this->items+this->count; }
	T* begin() { return this->items; }
	T* end() { return this->items+this->count; }
	
	
	template<typename Ts>
	void serialize(Ts& s)
	{
		for (size_t i=0;i<this->count;i++)
		{
			s.item(this->items[i]);
		}
	}
};

//size: two pointers, plus one T per item
//this one owns its storage and manages its memory
template<typename T> class array : public arrayvieww<T> {
	//T * items;
	//size_t count;
	
	static size_t capacity_for(size_t n)
	{
		// don't allocate enough space for 1 entry, just go for 4 or 8 directly - fewer mallocs means faster
		// TODO: are these numbers reasonable?
		size_t min = (sizeof(T) <= 16 ? 8 : 4);
		if (n < min) return min;
		else return bitround(n);
	}
	
	void clone(const arrayview<T>& other)
	{
		this->count = other.size(); // I can't access non-this instances of my base class, so let's just use the public interface
		this->items = xmalloc(sizeof(T)*capacity_for(this->count));
		if (this->trivial_copy())
		{
			memcpy((void*)this->items, (void*)other.ptr(), sizeof(T)*this->count);
		}
		else
		{
			for (size_t i=0;i<this->count;i++)
			{
				new(&this->items[i]) T(other.ptr()[i]);
			}
		}
	}
	
public:
	void swap(array<T>& other)
	{
		T* newitems = other.items;
		size_t newcount = other.count;
		other.items = this->items;
		other.count = this->count;
		this->items = newitems;
		this->count = newcount;
	}
	
	void swap(size_t a, size_t b)
	{
		arrayvieww<T>::swap(a, b);
	}
	
private:
	void resize_grow_noinit(size_t newcount)
	{
		if (this->count >= newcount) return;
		if (newcount > capacity_for(this->count) || !this->items) this->items = xrealloc(this->items, sizeof(T)*capacity_for(newcount));
		this->count = newcount;
	}
	
	// it would be better if this thing didn't reallocate until it's a quarter of the original size
	// but I don't store the allocated size, so that's hard
	// there is malloc_usable_size (and similar), but it may or may not exist depending on the libc used
	void resize_shrink_noinit(size_t newcount)
	{
		if (this->count <= newcount) return;
		size_t new_bufsize = capacity_for(newcount);
		if (this->count > new_bufsize) this->items = xrealloc(this->items, sizeof(T)*new_bufsize);
		this->count = newcount;
	}
	
	void resize_grow(size_t newcount)
	{
		if (this->count >= newcount) return;
		size_t prevcount = this->count;
		resize_grow_noinit(newcount);
		if constexpr (arrayview<T>::trivial_cons())
		{
			memset(this->items+prevcount, 0, sizeof(T)*(newcount-prevcount));
		}
		else
		{
			for (size_t i=prevcount;i<newcount;i++)
			{
				new(&this->items[i]) T();
			}
		}
	}
	
	void resize_shrink(size_t newcount)
	{
		if (this->count <= newcount) return;
		for (size_t i=newcount;i<this->count;i++)
		{
			this->items[i].~T();
		}
		resize_shrink_noinit(newcount);
	}
	
	void resize_to(size_t newcount)
	{
		if (newcount > this->count) resize_grow(newcount);
		else resize_shrink(newcount);
	}
	
public:
	T& operator[](size_t n) { return this->items[n]; }
	const T& operator[](size_t n) const { return this->items[n]; }
	
	void resize(size_t len) { resize_to(len); }
	void reserve(size_t len) { resize_grow(len); }
	void reserve_noinit(size_t len)
	{
		if (this->trivial_cons()) resize_grow_noinit(len);
		else resize_grow(len);
	}
	
	T& insert(size_t index, T&& item)
	{
		char tmp[sizeof(T)]; // in case 'item' points into the current array
		new(&tmp) T(std::move(item));
		
		resize_grow_noinit(this->count+1);
		memmove((void*)(this->items+index+1), (void*)(this->items+index), sizeof(T)*(this->count-1-index));
		memcpy((void*)(&this->items[index]), tmp, sizeof(T));
		return this->items[index];
	}
	T& insert(size_t index, const T& item)
	{
		char tmp[sizeof(T)];
		new(&tmp) T(item);
		
		resize_grow_noinit(this->count+1);
		memmove((void*)(this->items+index+1), (void*)(this->items+index), sizeof(T)*(this->count-1-index));
		memcpy((void*)(&this->items[index]), tmp, sizeof(T));
		return this->items[index];
	}
	T& insert(size_t index)
	{
		resize_grow_noinit(this->count+1);
		memmove((void*)(this->items+index+1), (void*)(this->items+index), sizeof(T)*(this->count-1-index));
		new(&this->items[index]) T();
		return this->items[index];
	}
	
	void append(const arrayview<T>& item) = delete; // use += instead
	T& append(T&& item) { return insert(this->count, std::move(item)); }
	T& append(const T& item) { return insert(this->count, item); }
	T& append() { return insert(this->count); }
	
	void remove(size_t index)
	{
		this->items[index].~T();
		memmove((void*)(this->items+index), (void*)(this->items+index+1), sizeof(T)*(this->count-1-index));
		resize_shrink_noinit(this->count-1);
	}
	
	void remove_range(size_t start, size_t end)
	{
		for (size_t n=start;n<end;n++)
			this->items[n].~T();
		memmove((void*)(this->items+start), (void*)(this->items+end), sizeof(T)*(this->count-end));
		resize_shrink_noinit(this->count - (end-start));
	}
	
	void reset() { resize_shrink(0); }
	
	T pop(size_t index)
	{
		T ret(std::move(this->items[index]));
		remove(index);
		return ret;
	}
	
	T pop_tail() { return pop(this->count - 1); }
	
	array() = default;
	
	array(nullptr_t)
	{
		this->items = NULL;
		this->count = 0;
	}
	
	array(const array<T>& other)
	{
		clone(other);
	}
	
	array(const arrayview<T>& other)
	{
		clone(other);
	}
	
	array(array<T>&& other)
	{
		swap(other);
	}
	
	array(std::initializer_list<T> c)
	{
		clone(arrayview<T>(c.begin(), c.size()));
	}
	
	array(const T * ptr, size_t count)
	{
		clone(arrayview<T>(ptr, count));
	}
	
	array<T>& operator=(array<T> other)
	{
		swap(other);
		return *this;
	}
	
	array<T>& operator=(arrayview<T> other)
	{
		if (other.ptr() >= this->ptr() && other.ptr() < this->ptr()+this->size())
		{
			size_t start = other.ptr()-this->ptr();
			size_t len = other.size();
			
			for (size_t i=0;i<start;i++) this->items[i].~T();
			memmove((void*)this->ptr(), (void*)(this->ptr()+start), sizeof(T)*len);
			for (size_t i=start+len;i<this->count;i++) this->items[i].~T();
			
			resize_shrink_noinit(len);
		}
		else
		{
			for (size_t i=0;i<this->count;i++) this->items[i].~T();
			free(this->items);
			clone(other);
		}
		return *this;
	}
	
	array<T>& operator+=(arrayview<T> other)
	{
		size_t prevcount = this->size();
		size_t othercount = other.size();
		
		const T* src;
		T* dst;
		
		if (other.ptr() >= this->ptr() && other.ptr() < this->ptr()+this->size())
		{
			size_t start = other.ptr()-this->ptr();
			
			resize_grow_noinit(prevcount + othercount);
			src = this->items+start;
			dst = this->items+prevcount;
		}
		else
		{
			resize_grow_noinit(prevcount + othercount);
			src = other.ptr();
			dst = this->items+prevcount;
		}
		
		if constexpr (arrayview<T>::trivial_copy())
		{
			memcpy(dst, src, sizeof(T)*othercount);
		}
		else
		{
			for (size_t i=0;i<othercount;i++)
			{
				new(&dst[i]) T(src[i]);
			}
		}
		
		return *this;
	}
	
	~array()
	{
		if (!this->trivial_dtor()) // not destructing a million uint8s speeds up debug builds
		{
			for (size_t i=0;i<this->count;i++) this->items[i].~T();
		}
		free(this->items);
	}
	
	//takes ownership of the given data
	static array<T> create_usurp(arrayvieww<T> data)
	{
		array<T> ret;
		ret.items = xrealloc(data.ptr(), sizeof(T)*capacity_for(data.size()));
		ret.count = data.size();
		return ret;
	}
	
	//remember to call all applicable destructors if using this
	arrayvieww<T> release()
	{
		arrayvieww<T> ret = *this;
		this->items = NULL;
		this->count = 0;
		return ret;
	}
	
	
	template<typename Ts>
	void serialize(Ts& s)
	{
		if constexpr (Ts::serializing)
		{
			arrayview<T>::serialize(s);
		}
		else
		{
			while (s.has_item())
				s.item(append());
		}
	}
};

template<typename T> template<typename T2>
inline array<T2> arrayview<T>::cast() const
{
	array<T2> ret;
	for (const T& tmp : *this) ret.append(tmp);
	return ret;
}

template<typename T> inline array<T> operator+(array<T>&& left, arrayview<T> right) { left += right; return left; }
template<typename T> inline array<T> operator+(arrayview<T> left, arrayview<T> right) { array<T> ret = left; ret += right; return ret; }



//Sized (or static) array - saves an allocation when returning fixed-size arrays, like string.split<N> or pack_le32
template<typename T, size_t N> class sarray {
	T storage[N];
public:
	sarray() = default;
	// One argument per member - useful for parameter packs and fixed-size items.
	template<typename... Ts> sarray(Ts... args) : storage{ (T)args... }
	{
		static_assert(sizeof...(Ts) == N);
	}
	// Undefined behavior if the input is too small.
	sarray(arrayview<T> ptr) { memcpy(storage, ptr.ptr(), sizeof(storage)); }
	
	T& operator[](size_t n) { return storage[n]; }
	const T& operator[](size_t n) const { return storage[n]; }
	operator arrayvieww<T>() { return storage; }
	operator arrayview<T>() const { return storage; }
	size_t size() const { return N; }
	const T* ptr() const { return storage; }
	T* ptr() { return storage; }
	//TODO: implement more missing features
};


//A refarray acts mostly like a normal array. The difference is that it stores pointers rather than the elements themselves;
//as such, you can't cast to arrayview or pointer, but you can keep pointers or references to the elements, or insert something virtual.
template<typename T> class refarray {
	array<autoptr<T>> items;
public:
	explicit operator bool() const { return (bool)items; }
	T& operator[](size_t n) { return *items[n]; }
	template<typename... Ts>
	T& append(Ts... args)
	{
		T* ret = new T(std::move(args)...);
		items.append(ret);
		return *ret;
	}
	void append_take(T& item) { items.append(&item); }
	void append_take(T* item) { items.append(item); }
	void append_take(autoptr<T> item) { items.append(std::move(item)); }
	void remove(size_t index) { items.remove(index); }
	void reset() { items.reset(); }
	size_t size() { return items.size(); }
	
private:
	class enumerator {
		autoptr<T>* ptr;
	public:
		enumerator(autoptr<T>* ptr) : ptr(ptr) {}
		
		T& operator*() { return **ptr; }
		enumerator& operator++() { ++ptr; return *this; }
		bool operator!=(const enumerator& other) { return ptr != other.ptr; }
	};
public:
	enumerator begin() { return enumerator(items.ptr()); }
	enumerator end() { return enumerator(items.ptr() + items.size()); }
};


template<typename T>
class fifo {
	T* items = nullptr;
	size_t capacity = 0;
	size_t rd = 0;
	size_t wr = 0;
public:
	fifo() = default;
	fifo(const fifo&) = delete;
	void push(T item)
	{
		if (wr == capacity)
		{
			if (rd <= capacity/2)
			{
				if (capacity == 0)
					capacity = 8;
				else
					capacity *= 2;
				items = xrealloc(items, sizeof(T)*capacity);
			}
			for (size_t n=0;n<rd;n++)
				items[n].~T();
			memmove(items, items+rd, sizeof(T)*(wr-rd));
			wr -= rd;
			rd = 0;
		}
		new(&items[wr]) T(std::move(item));
		wr++;
	}
	// The popped ref is valid until 
	T& pop_ref()
	{
		return items[rd++];
	}
	T pop()
	{
		return std::move(pop_ref());
	}
	bool empty()
	{
		return rd == wr;
	}
	~fifo()
	{
		for (size_t n=0;n<wr;n++)
			items[n].~T();
		free(items);
	}
};


// bitarray - somewhat like array<bool>, but stores eight bits per byte, not one.
// Also contains small string optimization, won't allocate until sizeof(uint8_t*)*8.
class bitarray {
protected:
	static const size_t n_inline = 8*sizeof(uint8_t*)/sizeof(uint8_t); // rearranging this may provoke -Wsizeof-pointer-div
	
	// unused but allocated bits must, at all points, be clear
	union {
		uint8_t bits_inline[n_inline/8];
		uint8_t* bits_outline;
	};
	size_t nbits;
	
	uint8_t* bits()
	{
		if (nbits <= n_inline) return bits_inline;
		else return bits_outline;
	}
	const uint8_t* bits() const
	{
		if (nbits <= n_inline) return bits_inline;
		else return bits_outline;
	}
	
	class entry {
		bitarray& parent;
		size_t index;
		
	public:
		operator bool() const { return parent.get(index); }
		entry& operator=(bool val) { parent.set(index, val); return *this; }
		
		entry(bitarray& parent, size_t index) : parent(parent), index(index) {}
	};
	friend class entry;
	
#if (defined(__i386__) || defined(__x86_64__)) && !defined(__clang__) // disabled on clang, https://bugs.llvm.org/show_bug.cgi?id=47866
	bool get(size_t n) const
	{
		// neither GCC nor Clang can emit bt with a memory operand (and, oddly enough, GCC only emits bt reg,reg on -O2, not -Os)
		// bt can accept integer arguments, but the assembler errors out if the argument is >= 256,
		//  and silently emits wrong machine code for arguments >= operand size
		// bt docs say operation is same as the C version, except it's implementation defined whether it uses u8, u16 or u32 units
		// but our buffer size is always a multiple of 4, so that's fine
		bool ret;
		__asm__("bt {%2,%1|%1,%2}" : "=@ccc"(ret) : "m"(*(const uint8_t(*)[])bits()), "r"(n) : "cc");
		return ret;
	}
	
	void set(size_t n, bool val)
	{
		// weird how there are instructions to set, clear, toggle, and read a bit, but not for setting to current carry or whatever
		if (val)
			__asm__ volatile("bts {%1,%0|%0,%1}" : "+m"(*(uint8_t(*)[])bits()) : "r"(n) : "cc");
		else
			__asm__ volatile("btr {%1,%0|%0,%1}" : "+m"(*(uint8_t(*)[])bits()) : "r"(n) : "cc");
	}
#else
	bool get(size_t n) const
	{
		return bits()[n/8] & (1<<(n&7));
	}
	
	void set(size_t n, bool val)
	{
		uint8_t& byte = bits()[n/8];
		byte &=~ (1<<(n&7));
		byte |= (val<<(n&7));
	}
#endif
	
	//does not resize
	void set_slice(size_t start, size_t num, const bitarray& other, size_t other_start);
	void clear_unused(size_t start, size_t nbytes);
	
	size_t alloc_size(size_t len)
	{
		return bitround((len+7)/8);
	}
	
public:
	bool operator[](size_t n) const { return get(n); }
	entry operator[](size_t n) { return entry(*this, n); }
	
	bool get_or(size_t n, bool def) const
	{
		if (n >= size()) return def;
		return get(n);
	}
	void set_resize(size_t n, bool val)
	{
		if (n >= size()) resize(n+1);
		set(n, val);
	}
	
	size_t size() const { return nbits; }
	void reset()
	{
		destruct();
		construct();
	}
	
	void resize(size_t len);
	
	void append(bool item)
	{
		resize(this->nbits+1);
		set(this->nbits-1, item);
	}
	
	bitarray slice(size_t first, size_t count) const;
	
private:
	void destruct()
	{
		if (nbits > n_inline) free(bits_outline);
	}
	void construct()
	{
		this->nbits = 0;
		memset(bits_inline, 0, sizeof(bits_inline));
	}
	void construct(const bitarray& other)
	{
		nbits = other.nbits;
		if (nbits > n_inline)
		{
			size_t nbytes = alloc_size(nbits);
			bits_outline = xmalloc(nbytes);
			memcpy(bits_outline, other.bits_outline, nbytes);
		}
		else
		{
			memcpy(bits_inline, other.bits_inline, sizeof(bits_inline));
		}
	}
	void construct(bitarray&& other)
	{
		memcpy((void*)this, (void*)&other, sizeof(*this));
		other.construct();
	}
public:
	
	bitarray() { construct(); }
	bitarray(const bitarray& other) { construct(other); }
	bitarray(bitarray&& other) { construct(std::move(other)); }
	~bitarray() { destruct(); }
	
	bitarray& operator=(const bitarray& other)
	{
		destruct();
		construct(other);
		return *this;
	}
	
	bitarray& operator=(bitarray&& other)
	{
		destruct();
		construct(std::move(other));
		return *this;
	}
	
	explicit operator bool() { return size(); }
};


#define X(T) COMMON_INST(array<T>);
ALLINTS(X)
#undef X

class cstring;
extern template class array<cstring>;
class string;
extern template class array<string>;

using bytesr = arrayview<uint8_t>;
using bytesw = arrayvieww<uint8_t>;
using bytearray = array<uint8_t>;
