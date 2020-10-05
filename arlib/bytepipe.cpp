#include "bytepipe.h"

void bytepipe::try_swap()
{
	if (buf1st == buf1end)
	{
		buf1.swap(buf2);
		buf1st = 0;
		buf1end = buf2end;
		buf2end = 0;
		if (buf2.size() > 65536) buf2.resize(65536);
	}
}

void bytepipe::push_one(arrayview<uint8_t> bytes)
{
	arrayvieww<uint8_t> tmp = push_buf(bytes.size());
	memcpy(tmp.ptr(), bytes.ptr(), bytes.size());
	push_done(bytes.size());
}

void bytepipe::push_one(cstring str)
{
	push(str.bytes());
}

arrayvieww<uint8_t> bytepipe::push_buf(size_t bytes)
{
	if (buf2end + bytes > buf2.size())
	{
		try_swap();
	}
	if (buf2end + bytes > buf2.size())
	{
		size_t newsize = buf2.size();
		while (buf2end + bytes > newsize) newsize *= 2;
		buf2.resize(newsize);
	}
	return buf2.skip(buf2end);
}

arrayview<uint8_t> bytepipe::pull_buf_full()
{
	if (buf1end+buf2end > buf1.size())
	{
		if (buf1st != 0)
		{
			memmove(buf1.ptr(), buf1.skip(buf1st).ptr(), buf1end-buf1st);
			buf1end -= buf1st;
			buf1st = 0;
		}
		if (buf1end+buf2end > buf1.size())
		{
			if (buf1end > buf2end) buf1.resize(buf1.size()*2);
			else buf1.resize(buf2.size()*2);
		}
	}
	
	memcpy(buf1.slice(buf1end, buf2end).ptr(), buf2.ptr(), buf2end);
	buf1end += buf2end;
	buf2end = 0;
	
	return buf1.slice(buf1st, buf1end-buf1st);
}

array<uint8_t> bytepipe::pull_buf_full_drain()
{
	if (buf1st != 0)
	{
		memmove(buf1.skip(buf1st).ptr(), buf1.ptr(), buf1end-buf1st);
		buf1end -= buf1st;
	}
	if (buf2end != 0)
	{
		if (buf1end+buf2end > buf1.size())
		{
			size_t newsize = buf1.size();
			while (buf1end+buf2end > newsize) newsize *= 2;
			buf1.resize(newsize);
		}
		memcpy(buf1.skip(buf1end).ptr(), buf2.ptr(), buf2end);
		buf1end += buf2end;
	}
	
	array<uint8_t> ret = std::move(buf1);
	ret.resize(buf1end);
	
	reset();
	return ret;
}

arrayview<uint8_t> bytepipe::pull_line()
{
	uint8_t* start = buf1.ptr()+buf1st;
	size_t len = buf1end-buf1st;
	uint8_t* nl = (uint8_t*)memchr(start, '\n', len);
	if (nl)
	{
		return arrayview<uint8_t>(start, nl+1-start);
	}
	
	nl = (uint8_t*)memchr(buf2.ptr(), '\n', buf2end);
	if (nl)
	{
		size_t pos = buf1end-buf1st + nl+1-buf2.ptr();
		return pull_buf_full().slice(0, pos);
	}
	
	return NULL;
}

arrayview<uint8_t> bytepipe::trim_line(arrayview<uint8_t> line)
{
	if (line.size()==1) return NULL;
	if (line[line.size()-2]=='\r') return line.slice(0, line.size()-2);
	else return line.slice(0, line.size()-1);
}

void bytepipe::reset()
{
	buf1.resize(1024);
	buf1st = 0;
	buf1end = 0;
	buf2.resize(1024);
	buf2end = 0;
}
