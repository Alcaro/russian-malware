#include "file.h"
#include "test.h"

arrayview<uint8_t> file::impl::default_mmap(size_t start, size_t len)
{
	arrayvieww<uint8_t> ret(malloc(len), len);
	size_t actual = this->pread(ret, start);
	return ret.slice(0, actual);
}

void file::impl::default_unmap(arrayview<uint8_t> data)
{
	free((void*)data.ptr());
}

arrayvieww<uint8_t> file::impl::default_mmapw(size_t start, size_t len)
{
	uint8_t* ptr = malloc(sizeof(size_t) + len);
	*(size_t*)ptr = start;
	
	arrayvieww<uint8_t> ret(ptr+sizeof(size_t), len);
	size_t actual = this->pread(ret, start);
	return ret.slice(0, actual);
}

bool file::impl::default_unmapw(arrayvieww<uint8_t> data)
{
	uint8_t* ptr = data.ptr() - sizeof(size_t);
	size_t start = *(size_t*)ptr;
	bool ok = this->pwrite(data, start);
	free(ptr);
	return ok;
}

string file::resolve(cstring path)
{
	array<cstring> parts = path.csplit("/");
	for (size_t i=0;i<parts.size();i++)
	{
		if (parts[i] == "" && i>0 && parts[i-1] != "")
		{
			parts.remove(i);
			i--;
			continue;
		}
		
		if (parts[i] == ".")
		{
			parts.remove(i);
			i--;
			continue;
		}
		
		if (parts[i] == ".." && i>0 && parts[i-1] != "..")
		{
			parts.remove(i);
			if (i>0) parts.remove(i-1);
			i-=2;
			continue;
		}
	}
	if (!parts) return ".";
	return parts.join("/");
}

string file::change_ext(cstring path, cstring new_ext)
{
	array<cstring> dir_name = path.crspliti<1>("/");
	if (dir_name.size() == 1)
		return dir_name[0].crsplit<1>(".")[0] + new_ext;
	else
		return dir_name[0] + dir_name[1].crsplit<1>(".")[0] + new_ext;
}

string file::sanitize_rel_path(string path)
{
	// this one currently leaves consecutive slashes alone
	// this means sanitized filename inequality does not mean they point to different files,
	//  but case insensitive FS means that already so no harm done
again:
	uint8_t * start = path.bytes().ptr();
	uint8_t * end = start + path.length();
	uint8_t * iter = start;
	
	size_t ndot = 0; // 0 - none seen, just after a slash; 1, 2 - obvious; 3+ - either 3+ seen, or a non-dot
	while (iter < end)
	{
		uint8_t& byte = *iter;
#ifdef _WIN32
		if (byte == '\\') byte = '/';
		if (byte == ':') byte = '_'; // no NTFS ADS allowed, nor C:/
#endif
		if (byte < ' ' || byte == '\x7F') byte = '_';
		
		if (byte == '/')
		{
			if (UNLIKELY(ndot == 1 || ndot == 2))
			{
				if (iter == start+1)
				{
					path = path.substr(2, ~0);
					goto again;
				}
				iter[-1] = '_';
			}
			ndot = 0;
		}
		else if (byte == '.') ndot++;
		else ndot = 3;
		
		if (byte >= 0x80)
		{
			uint32_t index = iter-start;
			uint32_t cp = path.codepoint_at(index);
			if (cp >= 0xDC80 && cp <= 0xDCFF) byte = '_';
			iter = start+index;
		}
		else iter++;
	}
	
	if (UNLIKELY(ndot == 1 || ndot == 2))
		iter[-1] = '_';
	
	if (path[0] == '/') path[0] = '_';
	if (!path) return "_";
	return path;
}

#ifdef ARGUI_NONE
file::impl* file::open_impl(cstring filename, mode m)
{
	return open_impl_fs(filename, m);
}

bool file::unlink(cstring filename)
{
	return unlink_fs(filename);
}

bool file::mkdir(cstring filename)
{
	return mkdir_fs(filename);
}
#endif


#ifdef ARLIB_TEST
//criteria for READONLY_FILE:
//- must be a normal file, nothing from /dev/
//- minimum 66000 bytes
//- the first few (min 2) bytes must be known, no .txt files or possibly-shebanged stuff
//- the file must contain somewhat unpredictable data, no huge streams of the same thing like /dev/zero
//- must be readable by everyone (assuming absense of sandboxes)
//- must NOT be writable or deletable by this program
//- no funny symbols in the name
//recommended choice: some random executable

//criteria for WRITABLE_FILE and CREATABLE_DIR:
//- must not exist under normal operation
//- directory must exist
//- directory must be writable by unprivileged users
//- no funny symbols in the name
#ifdef _WIN32
#define READONLY_FILE "C:/Windows/notepad.exe" // screw anything where the windows directory isn't on C:
#define READONLY_FILE_HEAD "MZ"
#include <windows.h>
static string get_temp_dir() // no constant location, grumble grumble
{
	char temp_dir[MAX_PATH];
	GetEnvironmentVariable("temp", temp_dir, sizeof(temp_dir));
	return file::sanitize_trusted_path(temp_dir);
}
#define WRITABLE_FILE get_temp_dir()+"/arlib-selftest.txt"
#define CREATABLE_DIR get_temp_dir()+"/arlib-selftest/"+
#define rmdir RemoveDirectory
#else
#define READONLY_FILE "/bin/sh"
#define READONLY_FILE_HEAD "\x7F""ELF"
#define WRITABLE_FILE "/tmp/arlib-selftest.txt"
#define CREATABLE_DIR "/tmp/arlib-selftest/"
#include <unistd.h>
#endif

test("file reading", "array", "file")
{
	file f;
	assert(f.open(READONLY_FILE));
	assert(f.size());
	assert(f.size() > strlen(READONLY_FILE_HEAD));
	assert(f.size() >= 66000);
	array<uint8_t> bytes = f.readall();
	assert(bytes.size() == f.size());
	assert(!memcmp(bytes.ptr(), READONLY_FILE_HEAD, strlen(READONLY_FILE_HEAD)));
	
	arrayview<uint8_t> map = f.mmap();
	assert(map.ptr());
	assert(map.size() == f.size());
	assert(!memcmp(bytes.ptr(), map.ptr(), bytes.size()));
	
	arrayview<uint8_t> map2 = f.mmap();
	assert(map2.ptr());
	assert(map2.size() == f.size());
	assert(!memcmp(bytes.ptr(), map2.ptr(), bytes.size()));
	f.unmap(map2);
	
	auto map3 = file2::mmap(READONLY_FILE);
	assert(map3.ptr());
	assert(map3.size() == f.size());
	assert(!memcmp(bytes.ptr(), map3.ptr(), bytes.size()));
	
	const size_t t_start[] = { 0,     65536, 4096, 1,     1,     1,     65537, 65535 };
	const size_t t_len[]   = { 66000, 400,   400,  65535, 65536, 65999, 400,   2     };
	for (size_t i=0;i<ARRAY_SIZE(t_start);i++)
	{
		arrayview<uint8_t> map3 = f.mmap(t_start[i], t_len[i]);
		assert(map3.ptr());
		assert(map3.size() == t_len[i]);
		assert(!memcmp(bytes.ptr()+t_start[i], map3.ptr(), t_len[i]));
		f.unmap(map3);
	}
	
	f.unmap(map);
	
	assert(f.open(READONLY_FILE));
	assert_eq(f.tell(), 0);
	
	assert(!f.open(file::dirname(READONLY_FILE))); // opening a directory should fail
	assert(file::dirname(READONLY_FILE).endswith("/"));
	
#ifdef _WIN32
	char prev_dir[MAX_PATH];
	assert(GetCurrentDirectory(MAX_PATH, prev_dir));
	assert(SetCurrentDirectory("C:/Windows/"));
	assert(f.open("notepad.exe"));
	assert(f.open("C:/Windows/notepad.exe"));
	//make sure these three paths are rejected, they're corrupt and have always been
	assert(!f.open("C:notepad.exe"));
	assert(!f.open("/Windows/notepad.exe"));
	assert(!f.open("C:\\Windows\\notepad.exe"));
	assert(SetCurrentDirectory(prev_dir));
#endif
}

test("file writing", "array", "file")
{
	file f;
	
	assert(!f.open(READONLY_FILE, file::m_wr_existing)); // keep this first, to ensure it doesn't shred anything if we're run as admin
	assert(!f.open(READONLY_FILE, file::m_write));
	assert(!f.open(READONLY_FILE, file::m_replace));
	assert(!f.open(READONLY_FILE, file::m_create_excl));
	
	assert( file::unlink(WRITABLE_FILE));
	assert(!file::unlink(READONLY_FILE));
	
	assert(!f.open(WRITABLE_FILE));
	
	assert(f.open(WRITABLE_FILE, file::m_write));
	assert(f.replace("foobar"));
	
	assert_eq(string(file::readall(WRITABLE_FILE)), "foobar");
	
	assert(f.resize(3));
	assert_eq(f.size(), 3);
	assert_eq(string(file::readall(WRITABLE_FILE)), "foo");
	
	assert(f.resize(8));
	assert_eq(f.size(), 8);
	uint8_t expected[8]={'f','o','o',0,0,0,0,0};
	array<uint8_t> actual = file::readall(WRITABLE_FILE);
	assert(actual.ptr());
	assert_eq(actual.size(), 8);
	assert(!memcmp(actual.ptr(), expected, 8));
	
	arrayvieww<uint8_t> map = f.mmapw();
	assert(map.ptr());
	assert_eq(map.size(), 8);
	assert(!memcmp(map.ptr(), expected, 8));
	map[3]='t';
	f.unmapw(map);
	
	expected[3] = 't';
	actual = file::readall(WRITABLE_FILE);
	assert(actual.ptr());
	assert_eq(actual.size(), 8);
	assert(!memcmp(actual.ptr(), expected, 8));
	
	//test the various creation modes
	//file exists, these three should work
	assert( (f.open(WRITABLE_FILE, file::m_write)));
	assert_eq(f.size(), 8);
	assert( (f.open(WRITABLE_FILE, file::m_wr_existing)));
	assert_eq(f.size(), 8);
	assert( (f.open(WRITABLE_FILE, file::m_replace)));
	assert_eq(f.size(), 0);
	assert(!(f.open(WRITABLE_FILE, file::m_create_excl))); // but this should fail
	
	assert(file::unlink(WRITABLE_FILE));
	assert(!f.open(WRITABLE_FILE, file::m_wr_existing)); // this should fail
	assert(f.open(WRITABLE_FILE, file::m_create_excl)); // this should create
	assert(file::unlink(WRITABLE_FILE));
	
	assert(f.open(WRITABLE_FILE, file::m_replace)); // replacing a nonexistent file is fine
	//opening a nonexistent file with m_write is tested at the start of this function
	f.close();
	assert(file::unlink(WRITABLE_FILE));
	assert(file::unlink(WRITABLE_FILE)); // ensure it properly deals with unlinking a nonexistent file
}

test("in-memory files", "array", "file")
{
	array<uint8_t> bytes;
	bytes.resize(8);
	for (int i=0;i<8;i++) bytes[i]=i;
	array<uint8_t> bytes2;
	bytes2.resize(4);
	
	file f = file::mem(bytes.slice(0, 8));
	assert(f);
	assert_eq(f.size(), 8);
	assert_eq(f.pread(bytes2, 1), 4);
	for (int i=0;i<4;i++) assert_eq(bytes2[i], i+1);
	
	//readonly
	assert(!f.pwrite(bytes2, 6));
	assert(!f.replace(bytes2));
	assert(!f.mmapw());
	
	f = file::mem(bytes);
	assert(f);
	assert_eq(f.size(), 8);
	
	assert(f.pwrite(bytes2, 6));
	assert_eq(f.size(), 10);
	for (int i=0;i<6;i++) assert_eq(bytes[i], i);
	for (int i=0;i<4;i++) assert_eq(bytes[i+6], i+1);
	
	assert(f.replace(bytes2));
	for (int i=0;i<4;i++) assert_eq(bytes[i], i+1);
	assert_eq(f.size(), 4);
}

test("file::resolve", "array,string", "")
{
	assert_eq(file::resolve("foo/bar/../baz"), "foo/baz");
	assert_eq(file::resolve("./foo.txt"), "foo.txt");
	assert_eq(file::resolve("."), ".");
	assert_eq(file::resolve("../foo.txt"), "../foo.txt");
	assert_eq(file::resolve(".."), "..");
	assert_eq(file::resolve("foo//bar.txt"), "foo/bar.txt");
	assert_eq(file::resolve("/foo.txt"), "/foo.txt");
	assert_eq(file::resolve("//foo.txt"), "//foo.txt");
}

test("file::mkdir", "array,string", "")
{
	file::unlink(CREATABLE_DIR "foo.txt");
	rmdir(CREATABLE_DIR ""); // must have this silly suffix because Windows
	
	assert(!file::writeall(CREATABLE_DIR "foo.txt", "hello"));
	assert_eq(file::mkdir(CREATABLE_DIR ""), true);
	assert( file::writeall(CREATABLE_DIR "foo.txt", "hello"));
	assert_eq(file::mkdir(CREATABLE_DIR ""), true);
	assert_eq(file::mkdir(READONLY_FILE), false);
	
	assert(file::unlink(CREATABLE_DIR "foo.txt"));
	rmdir(CREATABLE_DIR "");
}

test("file::change_ext", "array,string", "")
{
	assert_eq(file::change_ext("/foo.bar/bar/baz.bin", ".txt"), "/foo.bar/bar/baz.txt");
	assert_eq(file::change_ext("/foo.bar/bar/baz", ".txt"), "/foo.bar/bar/baz.txt");
	assert_eq(file::change_ext("/foo.bar/bar/baz.bin", ""), "/foo.bar/bar/baz");
	assert_eq(file::change_ext("baz.bin", ".txt"), "baz.txt");
	assert_eq(file::change_ext("baz", ".txt"), "baz.txt");
	assert_eq(file::change_ext("baz.bin", ""), "baz");
}

test("file::listdir", "array,string", "")
{
	array<string> files = file::listdir("arlib/");
	assert_gt(files.size(), 50);
	assert(files.contains("arlib/string.cpp"));
	assert(files.contains("arlib/.gitignore"));
	assert(files.contains("arlib/gui/"));
	assert(!files.contains("arlib/."));
	assert(!files.contains("arlib/.."));
}

static void path_safe(cstring path)
{
	assert_eq(file::sanitize_rel_path(path), path);
}
static void path_unsafe(cstring path)
{
	string safe = file::sanitize_rel_path(path);
	assert_ne(safe, path);
	assert_ne(safe, "");
	assert_ne(safe, ".");
	assert_ne(safe, "..");
	assert(!safe.startswith("./"));
	assert(!safe.startswith("../"));
	assert(!safe.endswith("/."));
	assert(!safe.endswith("/.."));
	assert(!safe.contains("/./"));
	assert(!safe.contains("/../"));
	assert(!file::is_absolute(safe));
	assert(safe.isutf8());
	for (uint8_t ch : safe.bytes())
		assert_gte(ch, ' ');
#ifdef _WIN32
	assert(!safe.contains("\\"));
	assert_ne(safe[0], '/');
	assert(!safe.contains(":"));
#endif
}

test("file::sanitize_rel_path", "array,string", "")
{
	testcall(path_safe("arlib/"));
	testcall(path_safe("arlib/deps/"));
	testcall(path_safe("arlib/string.h"));
	testcall(path_safe("smörgåsräka.txt"));
	testcall(path_safe("arlib/.gitignore"));
	testcall(path_safe("arlib/extensionless."));
	testcall(path_safe("foo bar.txt"));
	testcall(path_unsafe("/etc/passwd"));
	testcall(path_unsafe("../etc/passwd"));
	testcall(path_unsafe("foo/../etc/passwd"));
	testcall(path_unsafe("foo/../../../etc/passwd"));
	testcall(path_unsafe("foo/.."));
	testcall(path_unsafe(".."));
	testcall(path_unsafe("./etc/passwd"));
	testcall(path_unsafe("foo/./etc/passwd"));
	testcall(path_unsafe("foo/./././etc/passwd"));
	testcall(path_unsafe("foo/."));
	testcall(path_unsafe("."));
	testcall(path_unsafe("./foo"));
	testcall(path_unsafe("\x1F"));
	testcall(path_unsafe("\x80")); // no need to test all invalid utf8, string::isutf8/codepoint_at are already well tested
	testcall(path_unsafe(string::nul()));
	testcall(path_unsafe(""));
	testcall(path_unsafe("./"));
	testcall(path_unsafe("foo//..//bar"));
	assert_eq(file::sanitize_rel_path("./arlib/string.h"), "arlib/string.h");
#ifdef _WIN32
	assert_eq(file::sanitize_rel_path("foo\\bar.txt"), "foo/bar.txt");
	testcall(path_unsafe("C:/foo.txt"));
	testcall(path_unsafe("C:foo.txt"));
	testcall(path_unsafe("/foo.txt"));
	testcall(path_unsafe("\\foo.txt"));
	testcall(path_unsafe("foo.txt:ads"));
	testcall(path_unsafe(".\\"));
	testcall(path_unsafe("foo\\..\\bar.txt"));
#endif
}
#endif
