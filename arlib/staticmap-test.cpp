// Separate file so file2 can be hijacked, without interfering with staticmap's normal definition.

#ifdef ARLIB_TEST
#include "file.h"
#include "test.h"

#define AGGRESSIVE_MODE 0 // takes three minutes under valgrind, half a minute without

#define file2 debug_file2
#define staticmap debug_staticmap
static array<bytearray> debugfile_prev;
static bytearray debugfile_storage;

class debug_file2 {
public:
	enum mode { m_write, m_exclusive };
	
	using mmapw_t = bytesw;
	
	bool open(cstring filename, mode m) { return true; }
	void close() {}
	operator bool() { return true; }
	
	size_t pwrite(off_t pos, bytesr by)
	{
		if (pos + by.size() > debugfile_storage.size())
			debugfile_storage.resize(pos + by.size());
		memcpy(debugfile_storage.slice(pos, 0).ptr(), by.ptr(), by.size());
		debugfile_prev.append(debugfile_storage);
		return by.size();
	}
	size_t pwritev(off_t pos, arrayview<iovec> iov)
	{
		off_t ret = 0;
		for (const iovec& io : iov)
		{
			memcpy(debugfile_storage.slice(pos, 0).ptr(), io.iov_base, io.iov_len);
			pos += io.iov_len;
			ret += io.iov_len;
		}
		debugfile_prev.append(debugfile_storage);
		return ret;
	}
	
	void sync() {}
	
	bool resize(off_t newsize)
	{
		debugfile_storage.resize(newsize);
		debugfile_prev.append(debugfile_storage);
		return true;
	}
	
	bool resize(off_t newsize, mmapw_t& map, bool map_writable)
	{
		resize(newsize);
		map = debugfile_storage;
		return true;
	}
	
	mmapw_t mmapw(bool map_writable) { return debugfile_storage; }
	
	size_t sector_size() { return 512; }
};

#include "staticmap.cpp"

// The number of objects in the map must be size_start or size_end at every point, and size_end at the end.
static size_t size_start;
static void verify(staticmap& m, int diff)
{
	m.sync();
	m.fsck();
	
	size_t size_end;
	if (diff == -2) size_end = 0;
	else size_end = size_start + diff;
	
	bytearray backup = std::move(debugfile_storage);
	
	bool inner = false;
	size_t last_size;
again:
	assert(debugfile_prev); // make sure the file object was successfully hijacked
	array<bytearray> states = std::move(debugfile_prev);
	for (bytearray& by : states)
	{
		debugfile_storage = std::move(by);
		staticmap m("");
		m.fsck();
		if (m.size() != size_start)
			assert_eq(m.size(), size_end);
		last_size = m.size();
	}
	assert_eq(last_size, size_end);
	
	if (!inner)
	{
		inner = true;
		if (AGGRESSIVE_MODE)
			goto again;
	}
	debugfile_prev.reset();
	
	debugfile_storage = std::move(backup);
	size_start = size_end;
}

test("staticmap", "", "file")
{
	test_skip("kinda slow");
	
	uint64_t hash = 5381;
	hash = (hash ^ (hash>>7) ^ 0x6867666564636261) * 2546270801;
	hash = (hash ^ (hash>>7) ^ 0x706F6E6D6C6B6A69) * 2546270801;
	hash = (hash ^ (hash>>7) ^ 0x7877767574737271) * 2546270801;
	hash = (hash ^ 0x79) * 31;
	hash = (hash ^ 0x7A) * 31;
	assert_eq(hash, 0x0BF0D45A5E6D240A);
	hash ^= hash >> 30;
	hash *= 0xbf58476d1ce4e5b9;
	hash ^= hash >> 27;
	hash *= 0x94d049bb133111eb;
	hash ^= hash >> 31;
	assert_eq(hash, 0x40BE58450D80B718);
	assert_eq(sm_hash(bytesr((uint8_t*)"abcdefghijklmnopqrstuvwxyz", 26)), hash);
	
	uint8_t k1[1] = { 1 };
	uint8_t k2[1] = { 2 };
	uint8_t k3[1] = { 3 };
	uint8_t k4[1] = { 4 };
	uint8_t k5[1] = { 5 };
	uint8_t k6[1] = { 6 };
	uint8_t v65536[65536] = { };
	
	staticmap m("");
	size_start = 0;
	
	verify(m, 0);
	for (int i=0;i<256;i++)
	{
		uint8_t k[1] = { (uint8_t)i };
		uint8_t v[1] = { (uint8_t)(i*3) };
		m.insert(k, v);
		verify(m, +1);
		m.remove(k);
		verify(m, -1);
	}
	for (int i=0;i<16;i++)
	{
		uint8_t k[1] = { (uint8_t)i };
		uint8_t v[1] = { (uint8_t)(i*3) };
		uint8_t v2[1] = { (uint8_t)(i*3+1) };
		m.insert(k, v);
		verify(m, +1);
		m.insert(k, v2);
		verify(m, +0);
	}
	for (int i=15;i>=0;i--)
	{
		uint8_t k[1] = { (uint8_t)i };
		m.remove(k);
		verify(m, -1);
	}
	for (int i=0;i<64;i++)
	{
		uint8_t k[1] = { (uint8_t)i };
		m.insert(k, bytesr(v65536).slice(0, 1024));
		verify(m, +1);
	}
	
	m.reset();
	verify(m, -2);
	m.insert(k1, bytesr(v65536).slice(0, 32000)); // addr 32768
	verify(m, +1);
	m.insert(k2, bytesr(v65536).slice(0, 32000)); // addr 65536
	verify(m, +1);
	m.remove(k2); // and free it, so it gets merged and goes in a freelist that has never been nonempty
	verify(m, -1); // the freelist pointer will be 1, and cannot be copied into the freelist object's next pointer
	
	m.reset();
	verify(m, -2);
	m.insert(k1, bytesr(v65536).slice(0, 2000));
	verify(m, +1);
	m.insert(k2, bytesr(v65536).slice(0, 2000));
	verify(m, +1);
	m.remove(k1);
	verify(m, -1);
	m.remove(k2);
	verify(m, -1); // this creates multiple freelist elements of same size, without merging; must update previous head's prev pointer
	
	m.reset();
	verify(m, -2);
	m.insert(k1, bytesr(v65536).slice(0, 2000)); // offset 2048
	verify(m, +1);
	m.insert(k1, bytesr(v65536).slice(0, 2000)); // non-atomically overwrite it, so recovery sees two objects with same hash
	verify(m, 0);
	m.insert(k1, bytesr(v65536).slice(0, 2000)); // back to 2048
	verify(m, 0);
	m.insert(k2, bytesr(v65536).slice(0, 2000)); // 4096
	verify(m, +1);
	m.insert(k3, bytesr(v65536).slice(0, 2000)); // 6144
	verify(m, +1);
	m.insert(k4, bytesr(v65536).slice(0, 2000)); // 8192
	verify(m, +1);
	m.insert(k5, bytesr(v65536).slice(0, 2000)); // 10240
	verify(m, +1);
	m.insert(k6, bytesr(v65536).slice(0, 2000)); // 12288
	verify(m, +1);
	m.remove(k1); // free every second one, put em on 2048 freelist
	verify(m, -1);
	m.remove(k3);
	verify(m, -1);
	m.remove(k5);
	verify(m, -1);
	m.insert(k1, bytesr(v65536).slice(0, 2000)); // put em back, to use the freelists
	verify(m, +1);
	m.insert(k3, bytesr(v65536).slice(0, 2000));
	verify(m, +1);
	m.insert(k5, bytesr(v65536).slice(0, 2000));
	verify(m, +1);
	m.remove(k1); // back out
	verify(m, -1);
	m.remove(k3);
	verify(m, -1);
	m.remove(k5);
	verify(m, -1);
	m.remove(k2); // and now let them get merged
	verify(m, -1);
	m.remove(k4);
	verify(m, -1);
	m.remove(k6);
	verify(m, -1);
}

#endif
