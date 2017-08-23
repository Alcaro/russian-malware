#include "bytepipe.h"
#include "test.h"

static void push(bytepipe& p, uint8_t& start, size_t request, size_t push)
{
	arrayvieww<byte> tmp = p.push_buf(request);
	assert(tmp.ptr() != NULL);
	assert(tmp.size() >= request);
	for (size_t i=0;i<push;i++)
	{
		tmp[i] = ++start;
		if (start==253) start=0;
	}
	p.push_done(push);
}

static void pull(bytepipe& p, uint8_t& start, bool all, size_t expect, size_t use)
{
	arrayview<byte> tmp = (all ? p.pull_buf_full() : p.pull_buf());
	assert_eq(tmp.size(), expect);
	for (size_t i=0;i<use;i++)
	{
		assert_eq(tmp[i], ++start);
		if (start==253) start=0;
	}
	p.pull_done(use);
}

test()
{
	uint8_t w = 0;
	uint8_t r = 0;
	
	{
		bytepipe p;
		testcall(push(p,w, 768, 768));
		testcall(push(p,w, 512, 512));
		testcall(pull(p,r, false, 768, 512));
		testcall(pull(p,r, false, 256, 256));
		testcall(pull(p,r, false, 512, 512));
		testcall(pull(p,r, false, 0, 0));
	}
	
	{
		bytepipe p;
		testcall(push(p,w, 2, 2));
		testcall(push(p,w, 1, 1));
		testcall(pull(p,r, false, 3, 3));
		testcall(push(p,w, 2, 2));
		testcall(push(p,w, 1023, 1023));
		testcall(pull(p,r, false, 2, 2));
		testcall(pull(p,r, false, 1023, 1023));
	}
	
	{
		bytepipe p;
		testcall(push(p,w, 1024, 1024));
		testcall(push(p,w, 1, 1));
		testcall(pull(p,r, false, 1024, 1023));
		testcall(pull(p,r, true, 2, 2));
		testcall(pull(p,r, true, 0, 0));
	}
	
	{
		bytepipe p;
		testcall(push(p,w, 768, 768));
		testcall(push(p,w, 512, 512));
		testcall(pull(p,r, true, 1280, 0));
		testcall(pull(p,r, false, 1280, 512));
		testcall(pull(p,r, false, 768, 768));
		testcall(pull(p,r, false, 0, 0));
		testcall(pull(p,r, true, 0, 0));
	}
}
