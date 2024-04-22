#include "prioqueue.h"
#include "test.h"

#ifdef ARLIB_TEST
template<typename T>
static void validate(const prioqueue<T>& q)
{
	arrayview<T> items = q.peek_heap();
	for (size_t i=0;i<items.size();i++)
	{
		if (i*2+1 < items.size())
			assert_lte(items[i], items[i*2+1]);
		if (i*2+2 < items.size())
			assert_lte(items[i], items[i*2+2]);
	}
}

template<typename T>
static void test_queue(array<T> items)
{
	prioqueue<T> q;
	for (const T& i : items)
	{
		q.push(i);
		validate(q);
	}
	
	array<T> extracted;
	while (q.size())
	{
		extracted.append(q.pop());
		validate(q);
	}
	
	prioqueue<T> q2 = items;
	array<T> extracted2;
	validate(q2);
	while (q2.size())
	{
		extracted2.append(q2.pop());
		validate(q2);
	}
	
	items.ssort();
	assert_eq(extracted, items);
	assert_eq(extracted2, items);
}

test("priority queue", "array", "prioqueue")
{
	test_queue<int>({});
	test_queue<int>({ 1 });
	test_queue<int>({ 1, 2 });
	test_queue<int>({ 2, 1 });
	test_queue<int>({ 0,1,2,3,4,5,6,7,8,9 });
	test_queue<int>({ 9,8,7,6,5,4,3,2,1,0 });
	test_queue<int>({ 3,1,4,5,9,2,6,8,7,0 }); // the unique digits of pi, in order
	test_queue<int>({ 3,1,4,1,5,9,2,6,5,3 }); // a few duplicate elements
	test_queue<int>({ 0,1,7,2,5,8,9,3,4,6 }); // maximally unsorted heap; every child in the left tree is smaller than the right child
	test_queue<int>({ 0,4,1,7,5,3,2,9,8,6 }); // the above but mirrored; every child in the right tree is smaller than the left child
	test_queue<int>({ 0,1,2,3,4,5,6,7,8 }); // the above six, minus the last element
	test_queue<int>({ 9,8,7,6,5,4,3,2,1 });
	test_queue<int>({ 3,1,4,5,9,2,6,8,7 });
	test_queue<int>({ 3,1,4,1,5,9,2,6,5 });
	test_queue<int>({ 0,1,7,2,5,8,9,3,4 });
	test_queue<int>({ 0,4,1,7,5,3,2,9,8 });
	
	class nontrivial {
		int* body;
	public:
		nontrivial(int n) { body = new int(n); }
		nontrivial(const nontrivial& other) { body = new int(other.body[0]); }
		~nontrivial() { delete body; }
		bool operator<(const nontrivial& other) const { return *body < other.body[0]; }
		operator string() const { return tostring(*body); }
	};
	test_queue<nontrivial>({ 3,1,4,5,9,2,6,8,7,0 });
}
#endif
