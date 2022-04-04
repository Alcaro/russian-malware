#pragma once
#include "global.h"
#include "string.h"
#include "array.h"
#ifdef __unix__
#include <unistd.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

// TODO: this class needs to be rewritten
// commonly needed usecases, like file::readall, should not depend on unnecessary functionality, like seeking
// (/dev/stdin is unseekable, and gvfs http seek is unreliable)
//
// usecases:
// read all, read streaming, read random
// replace contents, replace/append streaming, rw random
// the above six but async
// mmap read, mmap rw
//  no need for mmap partial; it'd make sense on 32bit, but 32bit is no longer useful
// read all and flock() - also allows replacing contents
//  actually, this should be the one with replace contents
//
// read all should, as now, be a single function, but implemented in the backend
//  since typical usecase is passing return value to e.g. JSON::parse, it should return normal array<uint8_t> or string
// mmap shouldn't be in the file object either, but take a filename and return an arrayview(w)<uint8_t> child with a dtor
//  mmap resizable 
// read-and-lock should also be a function, also returning an arrayview<uint8_t> child with a dtor
//  this one should also have a 'replace contents' member, initially writing to filename+".swptmp" (remember to fsync)
//   the standard recommendation is using unique names; the advantage is no risk that two programs use the temp name simultaneously,
//    but the drawback is a risk of leaving trash around that will never be cleaned
//    such overlapping writes are unsafe anyways - if two programs simultaneously update a file using unique names,
//     one will be reverted, after telling program it succeeded
//   ideally, it'd be open(O_TMPFILE)+linkat(O_REPLACE), but kernel doesn't allow that
//   alternatively ioctl(FISWAPRANGE) https://lwn.net/Articles/818977/, except it was never merged
//   for Windows, just go for ReplaceFile(), don't even try to not create temp files (fsync() is FlushFileBuffers())
// async can be ignored for now; it's rarely useful, and async without coroutines is painful
// the other four combinations belong in the file object; replace/append streaming is useful for logs
//  they should use seek/read/size as primitives, not pread
//
// may also want some kind of resilient file object, guaranteeing complete write or rollback
// unfortunately, guarantees about mmap (or even write) atomicity are rare or nonexistent
//  other than rename-overwrite, which requires a poorly documented fsync dance before any atomicity guarantees exist,
//   and does way more than it should
//
// http support should be deprecated, filesystem only

#ifdef _WIN32
struct iovec {
	void* iov_base;
	size_t iov_len;
};
typedef off64_t off_t;
static_assert(sizeof(off_t) == 8);
#endif

class file2 : nocopy {
#ifdef __unix__
	typedef int fd_t;
	static const constexpr fd_t null_fd = -1;
	fd_t fd = -1;
	void reset() { if (fd != null_fd) ::close(fd); fd = null_fd; }
#endif
#ifdef _WIN32
	typedef HANDLE fd_t;
#define null_fd INVALID_HANDLE_VALUE
	fd_t fd = null_fd;
	void reset() { if (fd != null_fd) CloseHandle(fd); fd = null_fd; }
#endif
	
public:
	enum mode {
		m_read,
		m_write,          // If the file exists, opens it. If it doesn't, creates a new file.
		m_wr_existing,    // Fails if the file doesn't exist.
		m_replace,        // If the file exists, it's either deleted and recreated, or truncated.
		m_create_excl,    // Fails if the file does exist.
		
		m_exclusive = 8, // OR with any other mode. Tries to claim exclusive write access, or with m_read, simply deny all writers.
		                 // The request may be bypassable on some OSes. However, if both processes use Arlib, the second one will be stopped.
	};
	
	class mmapw_t;
	class mmap_t : public arrayview<uint8_t>, nocopy {
		friend class file2;
		friend class mmapw_t;
		static void map(bytesr& by, file2& src, bool writable);
		static void unmap(bytesr& by);
		
		void unmap() { unmap(*this); }
		void move_from(bytesr& other) { arrayview::operator=(other); other = nullptr; }
	public:
		mmap_t() {}
		mmap_t(mmap_t&& other) { move_from(other); }
		mmap_t(mmapw_t&& other) { move_from(other); }
		mmap_t& operator=(nullptr_t) { unmap(); return *this; }
		mmap_t& operator=(mmap_t&& other) { unmap(); move_from(other); return *this; }
		~mmap_t() { unmap(); }
	};
	class mmapw_t : public arrayvieww<uint8_t>, nocopy {
		friend class file2;
		void unmap() { mmap_t::unmap(*this); }
		void move_from(bytesr& other) { arrayview::operator=(other); other = nullptr; }
	public:
		mmapw_t() {}
		mmapw_t(mmapw_t&& other) { move_from(other); }
		mmapw_t& operator=(nullptr_t) { unmap(); return *this; }
		mmapw_t& operator=(mmapw_t&& other) { unmap(); move_from(other); return *this; }
		~mmapw_t() { unmap(); }
	};
	
	file2() {}
	file2(file2&& f) { fd = f.fd; f.fd = null_fd; }
	file2& operator=(file2&& f) { reset(); fd = f.fd; f.fd = null_fd; return *this; }
	file2(cstring filename, mode m = m_read) { open(filename, m); }
	~file2() { reset(); }
	bool open(cstring filename, mode m = m_read);
	void close() { reset(); }
	operator bool() const { return fd != null_fd; }
	
	void create_usurp(fd_t fd) { reset(); this->fd = fd; }
	fd_t peek_handle() { return this->fd; }
	fd_t release_handle() { fd_t ret = this->fd; this->fd = null_fd; return ret; }
	void create_dup(fd_t fd); // consider create_usurp() and release_handle(), if possible; they're faster
	
#ifndef __unix__
	// All return number of bytes processed. 0 means both EOF and error, return can't be negative.
	// Max size is 2**31-1 bytes, for both reads and writes.
	size_t read(bytesw by);
	size_t pread(off_t pos, bytesw by); // The p variants will not affect the current write pointer.
	size_t write(bytesr by);
	size_t pwrite(off_t pos, bytesr by);
	size_t writev(arrayview<iovec> iov); // No readv, hard to know sizes before reading.
	size_t pwritev(off_t pos, arrayview<iovec> iov);
	// TODO: async read/write (io_uring or WriteFileEx), but as a separate class (windows needs FILE_FLAG_OVERLAPPED)
	
	size_t sector_size();
	
	off_t size(); // Will return zero for certain weird files, like /dev/urandom.
	bool resize(off_t newsize); // May or may not seek to the file's new size.
	void seek(off_t pos); // Seeking outside the file is not allowed.
	off_t tell();
#else
	size_t read(bytesw by) { return max(::read(fd, by.ptr(), by.size()), 0); }
	size_t pread(off_t pos, bytesw by) { return max(::pread(fd, by.ptr(), by.size(), pos), 0); }
	size_t write(bytesr by) { return max(::write(fd, by.ptr(), by.size()), 0); }
	size_t pwrite(off_t pos, bytesr by) { return max(::pwrite(fd, by.ptr(), by.size(), pos), 0); }
	size_t writev(arrayview<iovec> iov) { return max(::writev(fd, iov.ptr(), iov.size()), 0); }
	size_t pwritev(off_t pos, arrayview<iovec> iov) { return max(::pwritev(fd, iov.ptr(), iov.size(), pos), 0); }
	
	size_t sector_size() { struct stat st; fstat(fd, &st); return st.st_blksize; }
	
	off_t size() { struct stat st; fstat(fd, &st); return st.st_size; }
	bool resize(off_t newsize) { return (ftruncate(fd, newsize) == 0); }
	//void seek(off_t pos);
	//off_t tell() { 
#endif
	
	// mmap objects are not tied to the file object. You can keep using the mmap, unless you need the sync_map function.
	// mmap() will always map the file, and will update if the file is written.
	// If you don't care about concurrent writers, and the file is small, it may be faster to read into a normal malloc.
	// As long as the file is mapped, it can't be resized.
	mmap_t mmap() { return mmapw(false); }
	static mmap_t mmap(cstring filename) { return file2(filename).mmap(); }
	// If not writable, the compiler will let you write, but doing so will segfault at runtime.
	mmapw_t mmapw(bool writable = true)
	{
		mmapw_t ret;
		mmap_t::map(ret, *this, writable);
		return ret;
	}
	static mmapw_t mmapw(cstring filename) { return file2(filename, m_wr_existing).mmapw(); }
	
	// Resizes the file, and its associated mmap object. Same as unmapping, resizing and remapping, but optimizes slightly better.
	bool resize(off_t newsize, mmap_t& map) { return resize(newsize, (bytesr&)map, false); }
	bool resize(off_t newsize, mmapw_t& map, bool writable = true) { return resize(newsize, (bytesr&)map, writable); }
private:
	bool resize(off_t newsize, bytesr& map, bool writable);
public:
	
	// Flushes write() calls to disk. Normally done automatically after a second or so.
#ifndef __unix__
	void sync();
#else
	void sync() { fdatasync(fd); }
#endif
	
	// Flushes the mapped bytes to disk. Input must be mapped from this file object.
	// Would fit better directly on mmapw_t, but a full flush on Windows requires both FlushViewOfFile and FlushFileBuffers.
#ifndef __unix__
	void sync_map(const mmapw_t& map);
#else
	void sync_map(const mmapw_t& map) { msync((void*)map.ptr(), map.size(), MS_SYNC); }
#endif
#undef null_fd
};

class file : nocopy {
public:
	class impl : nocopy {
	public:
		virtual size_t size() = 0;
		virtual bool resize(size_t newsize) = 0;
		
		virtual size_t pread(arrayvieww<uint8_t> target, size_t start) = 0;
		virtual bool pwrite(arrayview<uint8_t> data, size_t start = 0) = 0;
		virtual bool replace(arrayview<uint8_t> data) { return resize(data.size()) && (data.size() == 0 || pwrite(data)); }
		
		virtual array<uint8_t> readall()
		{
			array<uint8_t> ret;
			ret.reserve_noinit(this->size());
			size_t actual = this->pread(ret, 0);
			ret.resize(actual);
			return ret;
		}
		
		virtual arrayview<uint8_t> mmap(size_t start, size_t len) = 0;
		virtual void unmap(arrayview<uint8_t> data) = 0;
		virtual arrayvieww<uint8_t> mmapw(size_t start, size_t len) = 0;
		virtual bool unmapw(arrayvieww<uint8_t> data) = 0;
		
		virtual ~impl() {}
		
		arrayview<uint8_t> default_mmap(size_t start, size_t len);
		void default_unmap(arrayview<uint8_t> data);
		arrayvieww<uint8_t> default_mmapw(size_t start, size_t len);
		bool default_unmapw(arrayvieww<uint8_t> data);
	};
	
	class implrd : public impl {
	public:
		virtual size_t size() = 0;
		bool resize(size_t newsize) { return false; }
		
		virtual size_t pread(arrayvieww<uint8_t> target, size_t start) = 0;
		bool pwrite(arrayview<uint8_t> data, size_t start = 0) { return false; }
		bool replace(arrayview<uint8_t> data) { return false; }
		
		virtual arrayview<uint8_t> mmap(size_t start, size_t len) = 0;
		virtual void unmap(arrayview<uint8_t> data) = 0;
		arrayvieww<uint8_t> mmapw(size_t start, size_t len) { return NULL; }
		bool unmapw(arrayvieww<uint8_t> data) { return false; }
	};
private:
	impl* core;
	size_t pos = 0;
	file(impl* core) : core(core) {}
	
public:
	enum mode {
		m_read,
		m_write,          // If the file exists, opens it. If it doesn't, creates a new file.
		m_wr_existing,    // Fails if the file doesn't exist.
		m_replace,        // If the file exists, it's either deleted and recreated, or truncated.
		m_create_excl,    // Fails if the file does exist.
	};
	
	file() : core(NULL) {}
	file(file&& f) { core = f.core; pos = f.pos; f.core = NULL; }
	file& operator=(file&& f) { delete core; core = f.core; f.core = NULL; pos = f.pos; return *this; }
	file(cstring filename, mode m = m_read) : core(NULL) { open(filename, m); }
	
	//A path refers to a directory if it ends with a slash, and file otherwise. Directories may not be open()ed.
	bool open(cstring filename, mode m = m_read)
	{
		delete core;
		core = open_impl(filename, m);
		pos = 0;
		return core;
	}
	void close()
	{
		delete core;
		core = NULL;
		pos = 0;
	}
	static file wrap(impl* core) { return file(core); }
	
private:
	//This one will create the file from the filesystem.
	//open_impl() can simply return open_impl_fs(filename), or can additionally support stuff like gvfs.
	static impl* open_impl_fs(cstring filename, mode m);
	static impl* open_impl(cstring filename, mode m);
public:
	
	operator bool() const { return core; }
	
	//Reading outside the file will return partial results.
	size_t size() const { return core->size(); }
	size_t pread(arrayvieww<uint8_t> target, size_t start) const { return core->pread(target, start); }
	size_t pread(array<uint8_t>& target, size_t start, size_t len) const { target.resize(len); return core->pread(target, start); }
	array<uint8_t> readall() const { return core->readall(); }
	static array<uint8_t> readall(cstring path)
	{
		file f(path);
		if (f) return f.readall();
		else return NULL;
	}
	string readallt() const { return readall(); }
	static string readallt(cstring path) { return readall(path); }
	
	bool resize(size_t newsize) { return core->resize(newsize); }
	//Writes outside the file will extend it with NULs.
	bool pwrite(arrayview<uint8_t> data, size_t pos = 0) { return core->pwrite(data, pos); }
	//File pointer is undefined after calling this.
	bool replace(arrayview<uint8_t> data) { return core->replace(data); }
	bool replace(cstring data) { return replace(data.bytes()); }
	bool pwrite(cstring data, size_t pos = 0) { return pwrite(data.bytes(), pos); }
	static bool writeall(cstring path, arrayview<uint8_t> data)
	{
		file f(path, m_replace);
		return f && f.pwrite(data);
	}
	static bool writeall(cstring path, cstring data) { return writeall(path, data.bytes()); }
	static bool replace_atomic(cstring path, arrayview<uint8_t> data);
	static bool replace_atomic(cstring path, cstring data) { return replace_atomic(path, data.bytes()); }
	
	//Seeking outside the file is fine. This will return short reads, or extend the file on write.
	bool seek(size_t pos) { this->pos = pos; return true; }
	size_t tell() { return pos; }
	size_t read(arrayvieww<uint8_t> data)
	{
		size_t ret = core->pread(data, pos);
		pos += ret;
		return ret;
	}
	array<uint8_t> read(size_t len)
	{
		array<uint8_t> ret;
		ret.resize(len);
		size_t newlen = core->pread(ret, pos);
		pos += newlen;
		ret.resize(newlen);
		return ret;
	}
	string readt(size_t len)
	{
		string ret;
		arrayvieww<uint8_t> bytes = ret.construct(len);
		size_t newlen = core->pread(bytes, pos);
		pos += newlen;
		
		if (newlen == bytes.size()) return ret;
		else return ret.substr(0, newlen);
	}
	bool write(arrayview<uint8_t> data)
	{
		bool ok = core->pwrite(data, pos);
		if (ok) pos += data.size();
		return ok;
	}
	bool write(cstring data) { return write(data.bytes()); }
	
	//Mappings are not guaranteed to update if the underlying file changes. To force an update, delete and recreate the mapping.
	//If the underlying file is changed while a written mapping exists, it's undefined which (if any) writes take effect.
	//Resizing the file while a mapping exists is undefined behavior, including if the mapping is still in bounds (memimpl doesn't like that).
	//Mappings must be deleted before deleting the file object.
	arrayview<uint8_t> mmap(size_t start, size_t len) const { return core->mmap(start, len); }
	arrayview<uint8_t> mmap() const { return this->mmap(0, this->size()); }
	void unmap(arrayview<uint8_t> data) const { return core->unmap(data); }
	
	arrayvieww<uint8_t> mmapw(size_t start, size_t len) { return core->mmapw(start, len); }
	arrayvieww<uint8_t> mmapw() { return this->mmapw(0, this->size()); }
	//If this succeeds, data written to the file is guaranteed to be written, assuming no other writes were made in the region.
	//If not, file contents are undefined in that range.
	//TODO: remove return value, replace with ->sync()
	//if failure is detected, set a flag to fail sync()
	//actually, make all failures trip sync(), both read/write/unmapw
	bool unmapw(arrayvieww<uint8_t> data) { return core->unmapw(data); }
	
	~file() { delete core; }
	
	static file mem(arrayview<uint8_t> data)
	{
		return file(new file::memimpl(data));
	}
	//the array may not be modified while the file object exists, other than via the file object itself
	static file mem(array<uint8_t>& data)
	{
		return file(new file::memimpl(&data));
	}
private:
	class memimpl : public file::impl {
	public:
		arrayview<uint8_t> datard;
		array<uint8_t>* datawr; // even if writable, this object does not own the array
		
		memimpl(arrayview<uint8_t> data) : datard(data), datawr(NULL) {}
		memimpl(array<uint8_t>* data) : datard(*data), datawr(data) {}
		
		size_t size() { return datard.size(); }
		bool resize(size_t newsize)
		{
			if (!datawr) return false;
			datawr->resize(newsize);
			datard = *datawr;
			return true;
		}
		
		size_t pread(arrayvieww<uint8_t> target, size_t start)
		{
			size_t nbyte = min(target.size(), datard.size()-start);
			memcpy(target.ptr(), datard.slice(start, nbyte).ptr(), nbyte);
			return nbyte;
		}
		bool pwrite(arrayview<uint8_t> newdata, size_t start = 0)
		{
			if (!datawr) return false;
			size_t nbyte = newdata.size();
			datawr->reserve_noinit(start+nbyte);
			memcpy(datawr->slice(start, nbyte).ptr(), newdata.ptr(), nbyte);
			datard = *datawr;
			return true;
		}
		bool replace(arrayview<uint8_t> newdata)
		{
			if (!datawr) return false;
			*datawr = newdata;
			datard = *datawr;
			return true;
		}
		
		arrayview<uint8_t>  mmap (size_t start, size_t len) { return datard.slice(start, len); }
		arrayvieww<uint8_t> mmapw(size_t start, size_t len) { if (!datawr) return NULL; return datawr->slice(start, len); }
		void  unmap(arrayview<uint8_t>  data) {}
		bool unmapw(arrayvieww<uint8_t> data) { return true; }
	};
public:
	
	static array<string> listdir(cstring path); // Returns all items in the given directory. All outputs are prefixed with the input.
	static bool mkdir(cstring path); // Returns whether that's now a directory. If it existed already, returns true; if a file, false.
	static bool unlink(cstring filename); // Returns whether the file is now gone. If the file didn't exist, returns true.
	static cstring dirname(cstring path){ return path.substr(0, path.lastindexof("/")+1); } // If the input path is a directory, the basename is blank.
	static cstring basename(cstring path) { return path.substr(path.lastindexof("/")+1, ~0); }
	static string change_ext(cstring path, cstring new_ext); // new_ext should be ".bin" (can be blank)
	
	// Takes a byte sequence supposedly representing a relative file path from an untrusted source (for example a ZIP file).
	// If it's a normal, relative path, it's returned unchanged; if it contains anything weird, it's purged.
	// Output is guaranteed to be a relative file path without .. or other surprises.
	// Output may contain backslashes (Linux only) and spaces. foo/bar/../baz/ does not necessarily get transformed to foo/baz/.
	static string sanitize_rel_path(string path);
	
	// Takes a byte sequence representing any file path from a trusted source (for example command line arguments),
	//  and returns it in a form usable by Arlib.
#ifdef _WIN32
	static string sanitize_trusted_path(cstring path) { return path.replace("\\", "/"); }
#else
	static cstring sanitize_trusted_path(cstring path) { return path; }
	static string sanitize_trusted_path(string path) { return path; }
#endif
	
	//Returns whether the path is absolute.
	//On Unix, absolute paths start with a slash.
	//On Windows:
	// Absolute paths start with two slashes, or letter+colon+slash.
	// Half-absolute paths, like /foo.txt or C:foo.txt on Windows, are considered corrupt and are undefined behavior.
	//The path component separator is the forward slash on all operating systems, including Windows.
	//Paths to directories end with a slash, paths to files do not. For example, /home/ and c:/windows/ are valid,
	// but /home and c:/windows are not.
	static bool is_absolute(cstring path)
	{
#if defined(__unix__)
		return path[0]=='/';
#elif defined(_WIN32)
		if (path[0]=='/' && path[1]=='/') return true;
		if (path[0]!='\0' && path[1]==':' && path[2]=='/') return true;
		return false;
#else
#error unimplemented
#endif
	}
	
	//Removes all possible ./ and ../ components, and duplicate slashes, while still referring to the same file.
	//Similar to realpath(), but does not flatten symlinks.
	//foo/bar/../baz -> foo/baz, ./foo.txt -> foo.txt, ../foo.txt -> ../foo.txt, foo//bar.txt -> foo/bar.txt, . -> .
	//Invalid paths (above the root, or Windows half-absolute paths) are undefined behavior. Relative paths remain relative.
	static string resolve(cstring path);
	//Returns sub if it's absolute, else resolve(parent+sub). parent must end with a slash.
	static string resolve(cstring parent, cstring sub)
	{
		if (is_absolute(sub)) return resolve(sub);
		else return resolve(parent+sub);
	}
	
	//Returns the location of the currently executing code, whether that's EXE or DLL.
	//May be blank if the path can't be determined. The cstring is owned by Arlib and lives forever.
	static cstring exepath();
	//Returns the directory of the above.
	static cstring exedir();
	//Returns the current working directory.
	static const string& cwd();
	
	static string realpath(cstring path) { return resolve(cwd(), path); }
private:
	static bool mkdir_fs(cstring filename);
	static bool unlink_fs(cstring filename);
};


class autommap : public arrayview<uint8_t> {
	const file& f;
public:
	autommap(const file& f, arrayview<uint8_t> b) : arrayview(b), f(f) {}
	autommap(const file& f, size_t start, size_t end) : arrayview(f.mmap(start, end)), f(f) {}
	autommap(const file& f) : arrayview(f.mmap()), f(f) {}
	~autommap() { f.unmap(*this); }
};

class autommapw : public arrayvieww<uint8_t> {
	file& f;
public:
	autommapw(file& f, arrayvieww<uint8_t> b) : arrayvieww(b), f(f) {}
	autommapw(file& f, size_t start, size_t end) : arrayvieww(f.mmapw(start, end)), f(f) {}
	autommapw(file& f) : arrayvieww(f.mmapw()), f(f) {}
	~autommapw() { f.unmapw(*this); }
};
