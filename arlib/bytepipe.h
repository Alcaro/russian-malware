#pragma once
#include "array.h"
#include "string.h"

// A bytepipe accepts an infinite amount of bytes and returns them, first one first.
// Unlike splicing and splitting a bytearray, this is guaranteed amortized O(1) per byte,
//  except if pull_line() is not passed the proper skip argument.
class bytepipe {
	bytearray buf1;
	size_t buf1st;
	size_t buf1end;
	
	bytearray buf2;
	size_t buf2st;
	size_t buf2end;
	
	void try_swap();
	
	size_t push_text_size(const char * str) { return strlen(str); }
	size_t push_text_size(cstring str) { return str.length(); }
	void push_text_core(uint8_t*& ptr, bytesr by)
	{
		memcpy(ptr, by.ptr(), by.size());
		ptr += by.size();
	}
	void push_text_inner(uint8_t*& ptr, const char * str) { push_text_core(ptr, bytesr((uint8_t*)str, strlen(str))); }
	void push_text_inner(uint8_t*& ptr, cstring str) { push_text_core(ptr, str.bytes()); }
	
public:
	bytepipe() { reset(); }
	
	// The basic API: Simply push and pull fixed-size buffers.
	// For every function, the returned buffer is valid until the next non-const call on the object.
	void push(bytesr bytes);
	// Returns an empty bytesw if the object contains too few bytes. Feel free to scribble in it if you need some scratch space.
	bytesw pull(size_t nbytes) { bytesw ret = pull_begin(nbytes); if (!ret) return ret; pull_finish(nbytes); return ret.slice(0, nbytes); }
	// Returns one or more bytes. Usually more; O(1) calls will empty the bytepipe.
	bytesw pull_any() { bytesw ret = pull_begin(); pull_finish(ret.size()); return ret; }
	
	template<typename... Ts> void push_text(Ts... args)
	{
		static_assert(sizeof...(args) >= 1);
		size_t nbytes = (push_text_size(args) + ...);
		uint8_t* target = push_begin(nbytes).ptr();
		(push_text_inner(target, args), ...);
		push_finish(nbytes);
	}
	
	// Returns data until and including the next \n. If there is no \n, returns an empty bytesr.
	// If the first N bytes in the pipe are known to not contain a \n, you can pass in said N.
	bytesr pull_line(size_t skip = 0);
	// Returns 'line' minus a trailing \r\n or \n, or if input is empty, returns the same.
	// If the input is not empty, the \n must exist.
	static bytesr trim_line(bytesr line);
	static cstring trim_line(cstring line) { return trim_line(line.bytes()); }
	
	// If you don't know how much data you'll insert, you can use this.
	// The returned buffer may be bigger than requested; if so, you're welcome to use the extra capacity too, if you want.
	// Calling push_finish(0) is legal. If zero, you can also omit it completely. Don't call it twice.
	bytesw push_begin(size_t nbytes = 128);
	void push_finish(size_t nbytes) { buf2end += nbytes; }
	
	// Like the above.
	bytesw pull_begin(size_t nbytes = 1);
	void pull_finish(size_t nbytes) { buf1st += nbytes; }
	
	size_t size() const { return buf1end - buf1st + buf2end - buf2st; }
	
	// Resizes the internal buffers used by the object, so push_begin(1) is more likely to return something big, or to reclaim memory.
	// Only affects performance; buffers will automatically grow if needed, and shrinking to smaller than current contents will be ignored.
	// Size must be a power of two, and must be nonzero. Sizes below 256 are not recommended.
	void bufsize(size_t size);
	
	void reset(size_t bufsize = 256);
};
