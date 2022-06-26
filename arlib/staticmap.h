#pragma once
#include "file.h"
#include "endian.h"

// Like map<bytesr,bytesw> (modulo memory management and some minor details), but persistent on disk.
// The object assumes that
// - all writes to the underlying file are ordered; each will complete before the next one starts
// - writes <= sector size are atomic
// - sector size is at least 512
// which I believe (but didn't verify) are guaranteed by all journaling and COW file systems.
// If these assumptions are false (for example on FAT32), data loss may result if the machine is unexpectedly powered off.
class staticmap : nocopy {
	file2 f;
	file2::mmapw_t mmap;
	bool map_writable;
	uint8_t synced[1] = { 0 };
	uint32_t sector_size;
	
	uint64_t rd64(uint64_t pos) { return readu_le64(mmap.skip(pos).ptr()); }
	
	template<typename... Ts>
	bool wr64(uint64_t pos, Ts... vals)
	{
		uint8_t buf[sizeof...(vals) * 8];
		size_t n = 0;
		((writeu_le64(buf+(n++)*8, vals)), ...);
		return (f.pwrite(pos, buf) == sizeof(buf));
	}
	
	template<typename... Ts>
	void wr64(uint8_t* target, Ts... vals)
	{
		size_t n = 0;
		((writeu_le64(target+(n++)*8, vals)), ...);
	}
	
	void create();
	// Returns an object of the exact given size; must be a power of two, and at least 32.
	// Caller is responsible for writing the object header.
	uint64_t alloc(size_t bytes);
	void free(uint64_t addr); // Address must be the object header.
	
	// Returns a pointer into the hashmap. If rd64(ret+off_h_ptr) is 0 or 1, object not found; can be inserted there.
	uint64_t hashmap_locate(uint64_t hash, bytesr key);
	
	void rehash_if_needed();
	void recover();
	
public:
	// If the map isn't writable, every returned bytesw will actually be bytesr, and must be treated as such.
	// If writable, such operations are not atomic. Memory corruption (for example UAF) can end up destroying the entire staticmap.
	// Performance: Under normal operation, creating this object is O(1), other than an O(n) mmap of the entire file.
	// If the object was not correctly destructed, including by power failure, will take O(n) to repair the file.
	// Other operations have same performance characteristics as a normal hashmap, but worse constant factors.
	// You can't create two staticmap objects on the same backing file.
	staticmap() {}
	staticmap(cstrnul fn, bool map_writable = false) { open(fn, map_writable); }
	// If not open, any operation except open() and dtor are undefined behavior. If it is open, opening again is also UB.
	bool open(cstrnul fn, bool map_writable = false);
	
	size_t size() { return rd64(32); } // rd64(off_r_entries)
	bytesw get_or_empty(bytesr key, bool* found = nullptr); // If nonnull, found allows telling empty value apart from nonexistent.
	bytesw insert(bytesr key, bytesr val); // If key already exists, it will be atomically replaced.
	void remove(bytesr key); // If key doesn't exist, it's a noop.
	void reset(); // The point of a staticmap is to be persistent; this one is mostly for testing and debugging.
	
	// Flushes all data to disk.
	// Also ensures that if the process terminates before the next write operation, recreating the object after reboot will be quick.
	void sync();
	~staticmap() { sync(); }
	
private:
	void desync();
	
	struct node {
		bytesr key;
		bytesw value;
	};
	class iterator {
		uint8_t* it;
		friend class staticmap;
		static uint8_t* next(uint8_t* it, uint8_t* end);
		iterator(uint8_t* it) : it(it) {}
	public:
		node operator*();
		iterator& operator++();
		bool operator!=(const iterator& other);
	};
	
public:
	// Like the normal hashmap, iteration order is unspecified.
	// Unlike the normal hashmap, iterators are fully invalidated if you modify the set.
	// Also unlike the normal hashmap, you can't for (auto& pair : smap), you need auto or const auto&.
	iterator begin() { return mmap.ptr(); }
	iterator end() { return mmap.ptr()+mmap.size(); }
	
	// Verifies that the backing store is correctly formed.
	// If yes, returns and does nothing; if no, abort()s.
	// Useful for debugging, less useful for anything else.
	void fsck();
};
