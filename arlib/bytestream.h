#pragma once
#include "array.h"
#include "endian.h"
#include "string.h"

//signature and u8_or check for overflow; for anything else, use remaining()
//you're welcome to extend this object if you need a more rare operation, like leb128
class bytestream {
protected:
	const uint8_t* start;
	const uint8_t* at;
	const uint8_t* end;
	
public:
	static forceinline arrayview<uint8_t> to_signature(arrayview<uint8_t> sig) { return sig; }
	template<size_t N>
	static forceinline arrayview<uint8_t> to_signature(uint8_t (&sig)[N])      { return { sig, N }; }
	static forceinline arrayview<uint8_t> to_signature(cstring sig)            { return sig.bytes(); }
	static forceinline arrayview<uint8_t> to_signature(const char * sig)       { return { (uint8_t*)sig, strlen(sig) }; }
	template<typename... Ts>
	static forceinline sarray<uint8_t, 1+sizeof...(Ts)>
	to_signature(uint8_t first, Ts... next)
	{
		// TODO: use static_assert once this one is consteval
		if (((next < 0 || next > 255) || ...))
		{
			abort();
		}
		return { first, next... };
	}
	
	static forceinline bool sig_is_text(arrayview<uint8_t> sig) { return false; }
	static forceinline bool sig_is_text(cstring sig)            { return true; }
	static forceinline bool sig_is_text(const char * sig)       { return true; }
	template<typename... Ts>
	static forceinline bool sig_is_text(uint8_t first, Ts... next) { return false; }
	
public:
	bytestream(arrayview<uint8_t> buf)  { init(buf); }
	bytestream(const bytestream& other) : start(other.start), at(other.at), end(other.end) {}
	bytestream() : start(NULL), at(NULL), end(NULL) {}
	
	void init(arrayview<uint8_t> buf)
	{
		start = buf.ptr();
		at = buf.ptr();
		end = buf.ptr()+buf.size();
	}
	
	forceinline arrayview<uint8_t> bytes(size_t n)
	{
		arrayview<uint8_t> ret = arrayview<uint8_t>(at, n);
		at += n;
		return ret;
	}
	forceinline arrayview<uint8_t> peekbytes(size_t n)
	{
		return arrayview<uint8_t>(at, n);
	}
	
	template<typename... Ts>
	forceinline bool signature(Ts... args)
	{
		auto expected = bytestream::to_signature(args...);
		if (expected.size() > remaining())
			return false;
		arrayview<uint8_t> actual = peekbytes(expected.size());
		if (actual == expected)
		{
			bytes(expected.size());
			return true;
		}
		else return false;
	}
	
	forceinline uint8_t u8()
	{
		return *(at++);
	}
	forceinline int u8_or(int otherwise)
	{
		if (at==end) return otherwise;
		return *(at++);
	}
	forceinline uint8_t u8l() { return u8(); }
	forceinline uint8_t u8b() { return u8(); }
	forceinline uint16_t u16l() { return readu_le16(bytes(2).ptr()); }
	forceinline uint16_t u16b() { return readu_be16(bytes(2).ptr()); }
	forceinline uint32_t u32l() { return readu_le32(bytes(4).ptr()); }
	forceinline uint32_t u32b() { return readu_be32(bytes(4).ptr()); }
	forceinline uint32_t u64l() { return readu_le64(bytes(8).ptr()); }
	forceinline uint32_t u64b() { return readu_be64(bytes(8).ptr()); }
	
	forceinline float f32l() { return readu_lef32(bytes(4).ptr()); }
	forceinline float f32b() { return readu_bef32(bytes(4).ptr()); }
	forceinline float f64l() { return readu_lef64(bytes(8).ptr()); }
	forceinline float f64b() { return readu_bef64(bytes(8).ptr()); }
	
	forceinline cstring strnul()
	{
		const uint8_t * tmp = at;
		while (*at) at++;
		return cstring(arrayview<uint8_t>(tmp, (at++)-tmp));
	}
	forceinline const char * strnul_ptr()
	{
		const uint8_t * ret = at;
		while (*at++) {}
		return (char*)ret;
	}
	
	forceinline size_t tell() { return at-start; }
	forceinline size_t size() { return end-start; }
	forceinline size_t remaining() { return end-at; }
	
	forceinline void seek(size_t pos) { at = start+pos; }
	forceinline void skip(ssize_t off) { at += off; }
	
	forceinline arrayview<uint8_t> peek_at(size_t pos, size_t len) { return arrayview<uint8_t>(start+pos, len); }
	
	// for bytestream_dbg
	void enter() {}
	void exit() {}
	void compress() {}
	void panic() { abort(); }
};

class bytestreame : public bytestream {
protected:
	bool big_endian = false;
	
public:
	bytestreame(arrayview<uint8_t> buf, bool big_endian) : bytestream(buf), big_endian(big_endian) {}
	bytestreame(arrayview<uint8_t> buf) : bytestream(buf) {}
	bytestreame(const bytestreame& other) = default;
	bytestreame() {}
	
	void set_big_endian(bool big) { big_endian = big; }
	
	forceinline uint16_t u16() { return big_endian ? u16b() : u16l(); }
	forceinline uint32_t u32() { return big_endian ? u32b() : u32l(); }
	forceinline uint64_t u64() { return big_endian ? u64b() : u64l(); }
	forceinline float    f32() { return big_endian ? f32b() : f32l(); }
	forceinline double   f64() { return big_endian ? f64b() : f64l(); }
};

#ifndef STDOUT_ERROR
#include "stringconv.h"
#include "test.h"
template<typename Tinner = bytestreame>
class bytestream_dbg {
	string log;
	int indent = 0;
	bool first = false;
	bool compact = false;
	bool compact_first = false;
	
protected:
	Tinner inner;
	
	void d_log(cstring x)
	{
		if (!compact || compact_first)
		{
			string line;
			for (int i=0;i<indent;i++)
			{
				if (first && i == indent-1) line += "- ";
				else line += "  ";
			}
			log += "\n"+line+x;
			first = false;
			compact_first = false;
		}
		else
		{
			log += " "+x;
		}
	}
	
	template<typename T>
	T d_node(T in)
	{
		d_log(tostring(in));
		return in;
	}
	
private:
	void dump()
	{
		puts(log.substr_nul(1));
		size_t rem = inner.remaining();
		if (rem == 0) return;
		else if (inner.remaining() < 64) puts(tostringhex_dbg(inner.bytes(rem)));
		else puts(tostringhex_dbg(inner.bytes(64))+" + "+tostring(rem-64));
	}
	
public:
	template<typename... Ts> bytestream_dbg(Ts... args) : inner(args...) {}
	
	template<typename... Ts>
	bool signature(Ts... args)
	{
		arrayview<uint8_t> sig = bytestream::to_signature(args...);
		bool ret = inner.signature(sig);
		if (ret)
		{
			if (bytestream::sig_is_text(args...)) d_node("sig "+tostringhex(arrayview<uint8_t>(sig)));
			else d_node("sig "+cstring(sig));
		}
		return ret;
	}
	
	arrayview<uint8_t> bytes(size_t n)
	{
		arrayview<uint8_t> ret = inner.bytes(n);
		d_node(tostringhex(ret));
		return ret;
	}
	
#define FN(x) auto x() { return d_node(inner.x()); }
	FN(u8); FN(u16); FN(u32); FN(u64);
	FN(u8l); FN(u16l); FN(u32l); FN(u64l);
	FN(u8b); FN(u16b); FN(u32b); FN(u64b);
	FN(f32); FN(f32l); FN(f32b); FN(f64); FN(f64l); FN(f64b);
	FN(strnul);
#undef FN
	
	size_t tell() { return inner.tell(); }
	size_t size() { return inner.size(); }
	size_t remaining() { return inner.remaining(); }
	
	// TODO: how should these interact with the log?
	//void seek(size_t pos) { }
	//void skip(ssize_t off) { }
	//uint32_t u32lat(size_t pos) { }
	//uint32_t u32bat(size_t pos) { }
	//arrayview<uint8_t> peek_at(size_t pos, size_t len) { }
	
	~bytestream_dbg()
	{
		dump();
	}
	
	void enter() { indent++; first = true; }
	void exit() { indent--; first = false; compact = false; }
	void compress() { compact = true; compact_first = true; }
	
	void panic()
	{
		dump();
		abort();
	}
};
#endif


class bytestreamw_dyn {
protected:
	bytearray buf;
	
public:
	void bytes(bytesr data)
	{
		buf += data;
	}
	void text(cstring str)
	{
		buf += str.bytes();
	}
	void text(const char * str)
	{
		buf += bytesr((uint8_t*)str, strlen(str));
	}
	void strnul(cstring str)
	{
		buf += str.bytes();
		u8(0);
	}
	void strnul(const char * str)
	{
		buf += bytesr((uint8_t*)str, strlen(str)+1);
	}
	template<typename... Ts>
	void u8s(Ts... bs)
	{
		uint8_t raw[] = { (uint8_t)bs... };
		bytes(raw);
	}
	
	void u8(uint8_t val) { buf += arrayview<uint8_t>(&val, 1); }
	void u16l(uint16_t val) { buf += pack_le16(val); }
	void u16b(uint16_t val) { buf += pack_be16(val); }
	void u32l(uint32_t val) { buf += pack_le32(val); }
	void u32b(uint32_t val) { buf += pack_be32(val); }
	void u64l(uint64_t val) { buf += pack_le64(val); }
	void u64b(uint64_t val) { buf += pack_be64(val); }
	
	class pointer {
		bytearray* buf;
		size_t pos;
		friend class bytestreamw_dyn;
		pointer(bytearray* buf, size_t pos) : buf(buf), pos(pos) {}
	public:
		void fill32l() { writeu_le32(buf->ptr()+pos, buf->size()); }
		void fill32b() { writeu_be32(buf->ptr()+pos, buf->size()); }
		void fill64l() { writeu_le64(buf->ptr()+pos, buf->size()); }
		void fill64b() { writeu_be64(buf->ptr()+pos, buf->size()); }
	};
	pointer ptr32() { buf.resize(buf.size()+4); return pointer { &buf, buf.size()-4 }; }
	pointer ptr64() { buf.resize(buf.size()+8); return pointer { &buf, buf.size()-8 }; }
	
	void f32l(float  val) { buf += pack_lef32(val); }
	void f32b(float  val) { buf += pack_bef32(val); }
	void f64l(double val) { buf += pack_lef64(val); }
	void f64b(double val) { buf += pack_bef64(val); }
	
	void align8() {}
	void align16() { buf.resize((buf.size()+1)&~1); }
	void align32() { buf.resize((buf.size()+3)&~3); }
	void align64() { buf.resize((buf.size()+7)&~7); }
	
	arrayview<uint8_t> peek()
	{
		return buf;
	}
	array<uint8_t> finish()
	{
		return std::move(buf);
	}
};

class bytestreamw {
protected:
	uint8_t* start;
	uint8_t* at;
	uint8_t* end;
	
public:
	bytestreamw(bytesw by)
	{
		start = by.ptr();
		at = by.ptr();
		end = by.ptr() + by.size();
	}
	template<size_t N>
	bytestreamw(uint8_t(&ptr)[N])
	{
		start = ptr;
		at = ptr;
		end = ptr + N;
	}
	forceinline void bytes(bytesr data)
	{
		memcpy(at, data.ptr(), data.size());
		at += data.size();
	}
	forceinline void text(cstring str)
	{
		bytes(str.bytes());
	}
	forceinline void text(const char * str)
	{
		bytes(bytesr((uint8_t*)str, strlen(str)));
	}
	forceinline void strnul(cstring str)
	{
		bytes(str.bytes());
		u8(0);
	}
	forceinline void strnul(const char * str)
	{
		bytes(bytesr((uint8_t*)str, strlen(str)+1));
	}
	template<typename... Ts>
	forceinline void u8s(Ts... bs)
	{
		uint8_t raw[] = { (uint8_t)bs... };
		bytes(raw);
	}
	
	forceinline void u8(uint8_t val) { *at++ = val; }
	forceinline void u16l(uint16_t val) { bytes(pack_le16(val)); }
	forceinline void u16b(uint16_t val) { bytes(pack_be16(val)); }
	forceinline void u32l(uint32_t val) { bytes(pack_le32(val)); }
	forceinline void u32b(uint32_t val) { bytes(pack_be32(val)); }
	forceinline void u64l(uint64_t val) { bytes(pack_le64(val)); }
	forceinline void u64b(uint64_t val) { bytes(pack_be64(val)); }
	
	forceinline void f32l(float  val) { bytes(pack_lef32(val)); }
	forceinline void f32b(float  val) { bytes(pack_bef32(val)); }
	forceinline void f64l(double val) { bytes(pack_lef64(val)); }
	forceinline void f64b(double val) { bytes(pack_bef64(val)); }
	
	forceinline size_t tell() { return at-start; }
	forceinline size_t capacity() { return end-start; }
	forceinline size_t remaining() { return end-at; }
	
	//void align8() {}
	//void align16() { buf.resize((buf.size()+1)&~1); }
	//void align32() { buf.resize((buf.size()+3)&~3); }
	//void align64() { buf.resize((buf.size()+7)&~7); }
	
	forceinline arrayvieww<uint8_t> finish()
	{
		return bytesw(start, at-start);
	}
};
