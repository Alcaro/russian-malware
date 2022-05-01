#include "bytepipe.h"
#include "test.h"

namespace {
class pipetest {
public:
	size_t t_read = 0;
	size_t t_written = 0;
	bytepipe p;
	
	void push(size_t request, size_t expect, size_t actual)
	{
		if (expect != 0)
			assert_gte(expect, request);
		assert_gte(expect, actual);
		
		assert_eq(p.size(), t_written-t_read);
		bytesw tmp = p.push_begin(request);
		assert(tmp.ptr() != NULL);
		assert_eq(tmp.size(), expect);
		for (size_t i=0;i<actual;i++)
		{
			tmp[i] = (++t_written) % 253;
		}
		p.push_finish(actual);
		assert_eq(p.size(), t_written-t_read);
	}
	void pull(size_t request, size_t expect, size_t actual)
	{
		if (expect != 0)
			assert_gte(expect, request);
		assert_gte(expect, actual);
		
		assert_eq(p.size(), t_written-t_read);
		bytesr tmp = p.pull_begin(request);
		assert_eq(p.size(), t_written-t_read);
		assert_eq(tmp.size(), expect);
		for (size_t i=0;i<actual;i++)
		{
			assert_eq(tmp[i], (++t_read) % 253);
		}
		p.pull_finish(actual);
		assert_eq(p.size(), t_written-t_read);
	}
};
}

test("bytepipe", "array", "bytepipe")
{
	// state notation: two buffers, each containing { 0, start, end, size }
	{
		pipetest p;
		// { 0, 0, 0, 256 }, { 0, 0, 0, 256 }
		testcall(p.push(1, 256, 250));
		// { 0, 0, 0, 256 }, { 0, 0, 250, 256 }
		testcall(p.push(1, 256, 250));
		// { 0, 0, 250, 256 }, { 0, 0, 250, 256 }
		testcall(p.push(5, 6, 0));
		// { 0, 0, 250, 256 }, { 0, 0, 250, 256 }
		testcall(p.push(10, 262, 0));
		// { 0, 0, 250, 256 }, { 0, 0, 250, 512 }
		testcall(p.pull(1024, 0, 0));
		// { 0, 0, 250, 256 }, { 0, 0, 250, 512 }
		testcall(p.pull(8, 250, 192));
		// { 0, 192, 250, 256 }, { 0, 0, 250, 512 }
		testcall(p.pull(128, 128, 64));
		// { 0, 64, 128, 256 }, { 0, 70, 250, 512 }
		testcall(p.pull(1, 64, 64));
		// { 0, 128, 128, 256 }, { 0, 70, 250, 512 }
		testcall(p.pull(1, 180, 0));
		// { 0, 70, 250, 512 }, { 0, 0, 0, 256 }
		testcall(p.pull(1, 180, 180));
		// { 0, 250, 250, 512 }, { 0, 0, 0, 256 }
		testcall(p.push(1, 256, 256));
		// { 0, 250, 250, 512 }, { 0, 0, 256, 256 }
		testcall(p.push(1, 512, 512));
		// { 0, 0, 256, 256 }, { 0, 0, 512, 512 }
		testcall(p.pull(768, 768, 768));
		// { 0, 768, 768, 1024 }, { 0, 0, 0, 512 }
		testcall(p.push(1, 512, 512));
		// { 0, 768, 768, 1024 }, { 0, 0, 512, 512 }
		testcall(p.push(1, 1024, 1024));
		// { 0, 0, 512, 512 }, { 0, 0, 1024, 1024 }
		assert_eq(p.p.size(), 1536);
	}
	
	{
		bytepipe p;
		
		p.push_text("words\n");
		assert_eq(cstring(p.pull_line()), "words\n");
		assert_eq(cstring(p.pull_line()), "");
		
		p.push_text("word");
		assert_eq(cstring(p.pull_line()), "");
		p.push_text("s\n");
		assert_eq(cstring(p.pull_line()), "words\n");
		assert_eq(cstring(p.pull_line()), "");
	}
	
	{
		bytepipe p;
		
		for (int i=0;i<500/7;i++)
			p.push_text("abcdefg");
		assert_eq(cstring(p.pull_line()), "");
		p.push_text("\n");
		assert_eq(p.pull_line().size(), 500/7*7+1);
	}
	
	{
		bytepipe p;
		
		for (int i=0;i<500/7;i++)
			p.push_text("abcdef\n");
		for (int i=0;i<500/7;i++)
			assert_eq(cstring(p.pull_line()), "abcdef\n");
		assert_eq(cstring(p.pull_line()), "");
	}
}
