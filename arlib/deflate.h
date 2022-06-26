#pragma once
#include "array.h"

class inflator {
private:
	uint8_t m_state;
	uint8_t m_block_type;
	
	bool m_in_last; // logically belongs beside m_in_at, but that'd yield padding, better rearrange it
	
	uint32_t m_in_nbits;
	uint64_t m_in_bits_buf; // The bottom m_in_nbits bits contain valid data. Anything above that is zeroes.
	
	const uint8_t * m_in_at;
	const uint8_t * m_in_end;
	
	void bits_refill_fast();
	void bits_refill_all();
	uint32_t bits_extract(uint32_t n);
	uint32_t bits_read_huffman(const uint16_t * huff);
	
	
	const uint8_t * m_out_prev;
	uint8_t * m_out_start;
	uint8_t * m_out_at;
	uint8_t * m_out_end;
	
	
#ifdef DEFLATE_TEST_IMPL
public: // so tests can access sizeof(huff_table_123)
#endif
	static constexpr size_t huff_table_size_286 = 1760;
	static constexpr size_t huff_table_size_30 = 608;
	static constexpr size_t huff_table_size_19 = 560;
private:
	
	union {
		uint16_t m_stlitblk_len;
		uint16_t m_sthuff2_sizes;
		uint16_t m_stcopy_len;
		uint8_t m_stlitby_val;
	};
	union {
		uint16_t m_sthuff3_iter;
		uint16_t m_stcopy_dist;
	};
	union {
		uint16_t m_huff_distance[huff_table_size_30];
		uint8_t m_symbol_lengths[286+32];
	};
	uint16_t m_huff_symbol[huff_table_size_286];
	
	
public:
	inflator() { reset(); }
	void reset()
	{
		m_state = 0;
		m_in_nbits = 0;
		m_in_bits_buf = 0;
#ifndef ARLIB_OPT
		m_in_at = NULL;
		m_in_end = NULL;
		m_in_last = false;
		m_out_at = NULL;
#endif
	}
	
	void set_input(bytesr by, bool last)
	{
#ifndef ARLIB_OPT
		if (m_in_at != m_in_end) abort();
		if (m_in_last) abort();
#endif
		m_in_at = by.ptr();
		m_in_end = by.ptr()+by.size();
		m_in_last = last;
	}
	
	void set_output_first(bytesw by)
	{
#ifndef ARLIB_OPT
		if (m_out_at) abort();
		if (!by) by = bytesw((uint8_t*)"", 0); // so the m_out_at check on set_output_grow passes if input is null
#endif
		m_out_prev = NULL;
		m_out_start = by.ptr();
		m_out_at = by.ptr();
		m_out_end = by.ptr()+by.size();
	}
	
	void set_output_grow(bytesw by)
	{
#ifndef ARLIB_OPT
		if (!m_out_at) abort();
		if (m_out_at != m_out_end) abort();
		if (by.size() < (size_t)(m_out_end-m_out_start)) abort();
#endif
		size_t off = m_out_at-m_out_start;
		m_out_start = by.ptr();
		m_out_at = by.ptr()+off;
		m_out_end = by.ptr()+by.size();
	}
	
	void set_output_next(bytesw by)
	{
#ifndef ARLIB_OPT
		if (!m_out_at) abort();
		if (m_out_at != m_out_end) abort();
		if (m_out_end-m_out_start < 32768) abort();
#endif
		m_out_prev = m_out_end;
		m_out_start = by.ptr();
		m_out_at = by.ptr();
		m_out_end = by.ptr()+by.size();
	}
	
	enum ret_t {
		ret_done,
		ret_error,
		ret_more_input,
		ret_more_output
	};
	
	// set_input() and set_output_first() must be called exactly once each before calling this.
	// If the function returns ret_more_input or _output, it won't do anything else until you supply the required resource.
	//   If the function did not return ret_more_x, that resource may not be supplied.
	// Output must be supplied as either a single chunk (which may be realloc()ed and passed to set_output_grow),
	//   in chunks of at least 32KB where the previous one remains unchanged until the next one is full,
	//   or by reusing the same >=32KB chunk (latter two are done via set_output_next).
	// If the function returns ret_done or ret_error, the next function call on the object must be reset() or dtor.
	// The function may buffer an arbitrary amount of data internally, unless last=true in set_input;
	//   it is not guaranteed to emit every possible byte of output before asking for more input.
	// This object handles raw DEFLATE data; it will return errors or garbage data if given a zlib or gzip header.
	// This object does not support buffer sizes below 32KB, unless the complete output is less than that.
	ret_t inflate();
	
	// Returns number of bytes in the last output buffer. If there's only one chunk (either because it was reallocated,
	//   or because the output size was already known), this is the size of the complete output; if chunked (either multiple,
	//   or reusing the same chunk), it only counts the last chunk (caller may count previous outputs if desired).
	// These two are only valid after inflate() has returned ret_done.
	size_t output_in_last() const { return m_out_at - m_out_start; }
	// Returns number of bytes not used from the last input chunk.
	size_t unused_input() const { return m_in_end - m_in_at + m_in_nbits/8; }
	
	// Shortcuts to the above. Former returns empty on failure, which is indistinguishable from empty output;
	//  latter returns false if the decompressed data didn't fit exactly. Both return failure if input contains unused bytes.
	static bytearray inflate(bytesr in);
	static bool inflate(bytesw out, bytesr in);
	
	// Same interface as the outer class, but also parses zlib headers.
	// Also offers an adler32 calculator, in case you need that for anything else (for example creating a zlib stream).
	class zlibhead;
};
class inflator::zlibhead {
	uint32_t adler;
	inflator inf;
public:
	zlibhead() { reset(); }
	void reset() { adler = 1; inf.reset(); inf.m_state = 254; }
	void set_input(bytesr by, bool last) { inf.set_input(by, last); }
	void set_output_first(bytesw by) { inf.set_output_first(by); }
	void set_output_grow(bytesw by) { inf.set_output_grow(by); }
	void set_output_next(bytesw by) { inf.set_output_next(by); }
	ret_t inflate();
	
	size_t output_in_last() const { return inf.output_in_last(); }
	size_t unused_input() const { return inf.unused_input(); }
	
	static bytearray inflate(bytesr in);
	static bool inflate(bytesw out, bytesr in);
	static void inflate_trusted(bytesw out, bytesr in);
	
	static uint32_t adler32(bytesr by, uint32_t adler_prev = 1);
};
