#include "prioqueue.h"
#include "array.h"
#include "test.h"

#ifdef ARLIB_TEST
static void validate(const prioqueue<int>& q)
{
	arrayview<int> items { q.begin(), q.size() };
	for (size_t i=0;i<items.size();i++)
	{
		if (i*2+1 < items.size())
			assert_lte(items[i], items[i*2+1]);
		if (i*2+2 < items.size())
			assert_lte(items[i], items[i*2+2]);
	}
}

static void test_queue(arrayvieww<int> ints)
{
	prioqueue<int> q;
	for (int i : ints)
	{
		q.push(i);
		validate(q);
	}
	
	array<int> extracted;
	while (q.size())
	{
		extracted.append(q.pop());
		validate(q);
	}
	
	ints.sort();
	assert_eq(extracted, ints);
}

test("priority queue", "array", "prioqueue")
{
	int ints1[] = { 0,1,2,3,4,5,6,7,8,9 };
	test_queue(ints1);
	int ints2[] = { 9,8,7,6,5,4,3,2,1,0 };
	test_queue(ints2);
	int ints3[] = { 3,1,4,5,9,2,6,8,7,0 }; // the unique digits of pi, in order
	test_queue(ints3);
	int ints4[] = { 3,1,4,1,5,9,2,6,5,3 }; // a few duplicate elements
	test_queue(ints4);
	int ints5[] = { 0,1,7,2,5,8,9,3,4,6 }; // maximally unsorted heap; every child in the left tree is smaller than the right child
	test_queue(ints5);
	int ints6[] = { 0,4,1,7,5,3,2,9,8,6 }; // the above but mirrored
	test_queue(ints6);
}
#endif
