#include "bytepipe.h"

void bytepipe::try_swap()
{
	if (buf1st == buf1end && buf2end != 0)
	{
		buf1.swap(buf2);
		buf1st = buf2st;
		buf1end = buf2end;
		buf2st = 0;
		buf2end = 0;
	}
}

void bytepipe::push(bytesr bytes)
{
	bytesw target = push_begin(bytes.size());
	memcpy(target.ptr(), bytes.ptr(), bytes.size());
	push_finish(bytes.size());
}

bytesw bytepipe::push_begin(size_t nbytes)
{
	if (buf2.size() - buf2end < nbytes)
		try_swap();
	if (buf2.size() - buf2end < nbytes)
	{
		size_t b2s = buf2.size();
		while (b2s-buf2end < nbytes)
			b2s *= 2;
		buf2.resize(b2s);
	}
	return buf2.skip(buf2end);
}

bytesw bytepipe::pull_begin(size_t nbytes)
{
	// - if the requested amount doesn't exist, just say so
	// - if buf1 is empty, swap the buffers
	// - if the requested amount exists in buf1 (after the swap, if applicable), return that
	// - if the requested amount fits in buf1, memmove+memcpy and set buf2st nonzero
	// - else an allocation must be made
	if (size() < nbytes)
	{
		return {};
	}
	try_swap();
	size_t size1 = buf1end - buf1st;
	if (size1 >= nbytes)
	{
		return buf1.slice(buf1st, size1);
	}
	if (nbytes <= buf1.size())
	{
		size_t size2 = nbytes - size1;
		if (buf1st != 0)
			memmove(buf1.ptr(), buf1.ptr()+buf1st, size1);
		memcpy(buf1.ptr()+size1, buf2.ptr()+buf2st, size2);
		buf1st = 0;
		buf1end = nbytes;
		buf2st += size2;
		return buf1.slice(0, nbytes);
	}
	else
	{
		size_t size2 = buf2end-buf2st;
		bytearray newbuf;
		newbuf.resize(bitround(size()));
		memcpy(newbuf.ptr(), buf1.ptr()+buf1st, size1);
		memcpy(newbuf.ptr()+size1, buf2.ptr()+buf2st, size2);
		buf1 = std::move(newbuf);
		buf1st = 0;
		buf1end = size1+size2;
		buf2st = 0;
		buf2end = 0;
		return buf1.slice(0, buf1end);
	}
}

bytesr bytepipe::pull_line(size_t skip)
{
	size_t skipped = 0;
	bytesr chunk1 = buf1.slice(buf1st, buf1end-buf1st);
	if (skip <= chunk1.size())
	{
		chunk1 = chunk1.skip(skip);
		skipped += skip;
		skip = 0;
		const uint8_t * nl1 = (uint8_t*)memchr(chunk1.ptr(), '\n', chunk1.size());
		if (nl1)
			return pull(skipped + nl1+1 - chunk1.ptr());
	}
	else
	{
		skip -= chunk1.size();
	}
	
	bytesr chunk2 = buf2.slice(buf2st, buf2end-buf2st);
	chunk2 = chunk2.skip(skip);
	skipped += skip;
	const uint8_t * nl2 = (uint8_t*)memchr(chunk2.ptr(), '\n', chunk2.size());
	if (nl2)
		return pull(skipped + nl2+1 - chunk2.ptr() + chunk1.size());
	
	return {};
}

bytesr bytepipe::trim_line(bytesr line)
{
	if (line.size() <= 1) return {};
	if (line[line.size()-2] == '\r') return line.slice(0, line.size()-2);
	else return line.slice(0, line.size()-1);
}

void bytepipe::bufsize(size_t newsize)
{
	if (newsize > buf1end) buf1.resize(newsize);
	if (newsize > buf2end) buf2.resize(newsize);
}

void bytepipe::reset(size_t bufsize)
{
	buf1.resize(bufsize);
	buf1st = 0;
	buf1end = 0;
	buf2.resize(bufsize);
	buf2st = 0;
	buf2end = 0;
}

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
		testcall(p.push(1, 6, 0));
		// { 0, 0, 0, 256 }, { 0, 0, 250, 256 }
		testcall(p.push(10, 256, 250));
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
		assert_eq(p.size(), 0);
		assert_eq(cstring(p.pull_line()), "");
		
		for (int i=0;i<500/7;i++)
		{
			p.push_text("abcdefg");
			assert_eq(cstring(p.pull_line()), "");
			assert_eq(cstring(p.pull_line(p.size()/2)), "");
		}
		p.push_text("\n");
		assert_eq(p.pull_line(300).size(), 500/7*7+1);
		
		p.push_text("abcdefg\n");
		assert_eq(p.pull_line(4).size(), 8);
	}
	
	{
		bytepipe p;
		
		for (int i=0;i<500/7;i++)
			p.push_text("abcdef\n");
		for (int i=0;i<500/7;i++)
			assert_eq(cstring(p.pull_line()), "abcdef\n");
		assert_eq(cstring(p.pull_line()), "");
	}
	
	{
		bytepipe p;
		
		bytesr fail = p.pull(1);
		assert(!fail.ptr());
		assert_eq(fail.size(), 0);
		
		fail = p.pull_any();
		assert(!fail.ptr());
		assert_eq(fail.size(), 0);
	}
}
