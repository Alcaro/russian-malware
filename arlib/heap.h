#pragma once
#include "global.h"

class heap {
	// the heap invariant: items[n] <= items[n*2+1] && items[n] <= items[n*2+2]
	static size_t child1(size_t n) { return n*2+1; }
	static size_t child2(size_t n) { return n*2+2; }
	static size_t parent(size_t n) { return (n-1)/2; }
	
	template<typename T>
	static void move(T* dst, T* src)
	{
		memcpy((void*)dst, (void*)src, sizeof(T));
	}
	template<typename T>
	static void move(T* ptr, size_t dst, size_t src)
	{
		move(ptr+dst, ptr+src);
	}
	
	template<typename T>
	static void swap(T* a, T* b)
	{
		char tmp[sizeof(T)];
		memcpy(tmp, (void*)a, sizeof(T));
		memcpy((void*)a, (void*)b, sizeof(T));
		memcpy((void*)b, tmp, sizeof(T));
	}
	template<typename T>
	static void swap(T* ptr, size_t dst, size_t src)
	{
		swap(ptr+dst, ptr+src);
	}
	
public:
	template<typename T, typename Tless>
	static void heapify(T* body, size_t len, const Tless& less)
	{
		if (len <= 1)
			return;
		// for each node that has any children, backwards:
		// - find the smaller (or only) child
		// - compare to the parent
		// - if larger, swap, and repeat these steps for the now-child
		// this is O(n); while the higher nodes have log(n) levels of children, each level also has fewer members
		size_t i = parent(len-1);
		while (true)
		{
			size_t j = i;
			while (true)
			{
				if (child2(j) >= len)
				{
					if (child1(j) < len)
					{
						if (less(body[child1(j)], body[j]))
							swap(body, child1(j), j);
					}
					break;
				}
				size_t par = j;
				size_t ch1 = child1(j);
				size_t ch2 = child2(j);
				size_t min_ch = (less(body[ch1], body[ch2]) ? ch1 : ch2);
				if (less(body[min_ch], body[par]))
				{
					swap(body, min_ch, par);
					j = min_ch;
					continue;
				}
				break;
			}
			if (!i)
				break;
			i--;
		}
	}
	template<typename T>
	static void heapify(T* body, size_t len)
	{
		heapify(body, len, [](const T& a, const T& b) { return a < b; });
	}
	
	// Will memcpy new_elem to somewhere in body, and overwrite body[len].
	// It's caller's responsibility to not call new_elem's dtor afterwards.
	// Length must be the heap size before the operation. new_elem may not be at body[len].
	template<typename T, typename Tless>
	static void push(T* body, size_t len, T* new_elem, const Tless& less)
	{
		size_t new_pos = len;
		while (new_pos != 0 && less(*new_elem, body[parent(new_pos)]))
		{
			move(body, new_pos, parent(new_pos));
			new_pos = parent(new_pos);
		}
		move(body+new_pos, new_elem);
	}
	template<typename T>
	static void push(T* body, size_t len, T* new_elem)
	{
		push(body, len, new_elem, [](const T& a, const T& b) { return a < b; });
	}
	
	// Will memcpy something from body to ret, and leave body[len-1] as garbage that should not be destructed.
	// It's caller's responsibility to ensure ret is uninitialized prior to the call.
	// Length must be the heap size before the operation. ret may not be at body[len-1].
	template<typename T, typename Tless>
	static void pop(T* body, size_t len, T* ret, const Tless& less)
	{
		move(ret, body+0);
		size_t n_items = len-1;
		if (n_items)
		{
			size_t parent = 0;
			while (true)
			{
				size_t child = child1(parent);
				if (child >= n_items)
					break;
				if (child+1 < n_items && less(body[child+1], body[child]))
					child++;
				if (less(body[n_items], body[child]))
					break;
				move(body, parent, child);
				parent = child;
			}
			move(body, parent, n_items);
		}
	}
	template<typename T>
	static void pop(T* body, size_t len, T* ret)
	{
		pop(body, len, ret, [](const T& a, const T& b) { return a < b; });
	}
};
