#pragma once
#include "array.h"
#include "global.h"
#include "heap.h"

// a simple wrapper over the low-level heap functions
template<typename T>
class prioqueue {
	T* items;
	size_t count;
	
	size_t capacity_for(size_t n) { return array<T>::capacity_for(n); }
	
	void init_from(array<T>&& src)
	{
		arrayvieww<T> src_raw = src.release();
		count = src_raw.size();
		items = xrealloc(src_raw.ptr(), sizeof(T)*capacity_for(count));
		heap::heapify(items, count);
	}
public:
	prioqueue()
	{
		count = 0;
		items = xmalloc(sizeof(T)*capacity_for(0));
	}
	prioqueue(array<T>&& src)
	{
		init_from(std::move(src));
	}
	prioqueue(arrayview<T> src)
	{
		array<T> src_wrap = src;
		init_from(std::move(src_wrap));
	}
	~prioqueue()
	{
		for (size_t i=0;i<count;i++)
			items[i].~T();
		free(items);
	}
	void push(T item)
	{
		if (count == capacity_for(count))
			items = xrealloc(items, sizeof(T)*capacity_for(count+1));
		
		alignas(T) char buf[sizeof(T)];
		new((T*)buf) T(std::move(item));
		
		heap::push(items, count, (T*)buf);
		count++;
	}
	T pop()
	{
		alignas(T) char buf[sizeof(T)];
		heap::pop(items, count, (T*)buf);
		count--;
		
		auto tmp = dtor([&](){ ((T*)buf)->~T(); });
		return std::move(*(T*)buf);
	}
	size_t size() const { return count; }
	
#ifdef ARLIB_TEST
	arrayview<T> peek_heap() const { return { items, count }; }
#endif
};
