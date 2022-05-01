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

bytesr bytepipe::pull_begin(size_t nbytes)
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

bytesr bytepipe::pull_line()
{
	bytesr chunk1 = buf1.slice(buf1st, buf1end-buf1st);
	const uint8_t * nl1 = (uint8_t*)memchr(chunk1.ptr(), '\n', chunk1.size());
	if (nl1)
		return pull(nl1+1 - chunk1.ptr());
	
	bytesr chunk2 = buf2.slice(buf2st, buf2end-buf2st);
	const uint8_t * nl2 = (uint8_t*)memchr(chunk2.ptr(), '\n', chunk2.size());
	if (nl2)
		return pull(nl2+1 - chunk2.ptr() + chunk1.size());
	
	return {};
}

bytesr bytepipe::trim_line(bytesr line)
{
	if (line.size()==1) return {};
	if (line[line.size()-2]=='\r') return line.slice(0, line.size()-2);
	else return line.slice(0, line.size()-1);
}

void bytepipe::bufsize(size_t newsize)
{
	if (newsize > buf1end) buf1.resize(newsize);
	if (newsize > buf2end) buf2.resize(newsize);
}

void bytepipe::reset()
{
	buf1.resize(256);
	buf1st = 0;
	buf1end = 0;
	buf2.resize(256);
	buf2st = 0;
	buf2end = 0;
}
