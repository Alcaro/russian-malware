#pragma once
#include "array.h"
#include "string.h"

//A bytepipe accepts an infinite amount of bytes and returns them, first one first.
//Guaranteed amortized O(n) no matter how many bytes are pushed at the time, except if pull_line() is used and there is no line.
class bytepipe {
	array<uint8_t> buf1;
	size_t buf1st;
	size_t buf1end;
	
	array<uint8_t> buf2;
	size_t buf2end;
	
	void try_swap();
	void push_one(arrayview<uint8_t> bytes);
	void push_one(cstring str);
	void push() {}
	
public:
	bytepipe()
	{
		reset();
	}
	
	//Will return a buffer of at least 'bytes' bytes. Can be bigger. Use push_done afterwards.
	arrayvieww<uint8_t> push_buf(size_t bytes = 512);
	void push_done(size_t bytes)
	{
		buf2end += bytes;
	}
	
	template<typename T, typename... Tnext> void push(T first, Tnext... next)
	{
		push_one(first);
		push(next...);
	}
	
	//Can return less than remaining().
	//You can cast the return value of the pull family to mutable.
	//If you do, subsequent pulls will return the altered data; other than that, no harm done.
	//In particular, if you immediately acknowledge it, it's safe.
	arrayview<uint8_t> pull_buf()
	{
		try_swap();
		return buf1.slice(buf1st, buf1end-buf1st);
	}
	//Returns whatever was pushed that pull_buf didn't return. Can't be acknowledged and discarded, use pull_buf.
	arrayview<uint8_t> pull_next()
	{
		return buf2.slice(0, buf2end);
	}
	void pull_done(size_t bytes)
	{
		buf1st += bytes;
	}
	void pull_done(arrayview<uint8_t> bytes)
	{
		pull_done(bytes.size());
	}
	//Returns the entire thing.
	arrayview<uint8_t> pull_buf_full();
	//Returns the entire thing, and immediately acknowledges it. Other than the return value, it's equivalent to reset().
	array<uint8_t> pull_buf_full_drain();
	
	//Returns data until and including the next \n. Doesn't acknowledge it. If there is no \n, returns an empty array.
	arrayview<uint8_t> pull_line();
	//Returns 'line' minus a trailing \r\n or \n. The \n must exist.
	//Usable together with the above, though you must acknowledge the \n too.
	static arrayview<uint8_t> trim_line(arrayview<uint8_t> line);
	
	operator bool() { return size(); }
	
	size_t size() { return buf1end-buf1st+buf2end; }
	void reset();
};
