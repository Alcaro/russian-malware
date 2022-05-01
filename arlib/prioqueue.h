#include "global.h"

template<typename T>
class prioqueue {
	// the heap invariant: items[n] <= items[n*2+1] && items[n] <= items[n*2+2]
	T* items;
	size_t capacity;
	size_t n_items;
	static size_t child1(size_t n) { return n*2+1; }
	static size_t child2(size_t n) { return n*2+2; }
	static size_t parent(size_t n) { return (n-1)/2; }
public:
	prioqueue()
	{
		capacity = 8;
		items = xmalloc(sizeof(T)*capacity);
		n_items = 0;
	}
	~prioqueue()
	{
		for (size_t i=0;i<n_items;i++)
			items[i].~T();
		free(items);
	}
	void push(T item)
	{
		if (n_items == capacity)
		{
			capacity *= 2;
			items = xrealloc(items, sizeof(T)*capacity);
		}
		size_t new_pos = n_items;
		while (new_pos != 0 && item < items[parent(new_pos)])
		{
			memcpy(&items[new_pos], &items[parent(new_pos)], sizeof(T));
			new_pos = parent(new_pos);
		}
		n_items++;
		new(&items[new_pos]) T(std::move(item));
	}
	T pop()
	{
		T ret = std::move(items[0]);
		items[0].~T();
		n_items--;
		if (n_items)
		{
			size_t parent = 0;
			while (true)
			{
				size_t child = child1(parent);
				if (child >= n_items)
					break;
				if (child+1 < n_items && items[child+1] < items[child])
					child++;
				if (items[n_items] < items[child])
					break;
				memcpy(&items[parent], &items[child], sizeof(T));
				parent = child;
			}
			memcpy(&items[parent], &items[n_items], sizeof(T));
		}
		return ret;
	}
	size_t size() const { return n_items; }
	const T* begin() const { return items; }
	const T* end() const { return items + n_items; }
};
