#include "file.h"

#ifdef __unix__
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

//static void window_cwd_enter(const char * dir);
//static void window_cwd_leave();
//
//char * _window_native_get_absolute_path(const char * basepath, const char * path, bool allow_up)
//{
//	if (!basepath || !path) return NULL;
//	const char * filepart=strrchr(basepath, '/');
//	if (!filepart) return NULL;
//	char * basedir=strndup(basepath, filepart+1-basepath);
//	
//	window_cwd_enter(basedir);
//	char * ret=realpath(path, NULL);
//	window_cwd_leave();
//	
//	if (!allow_up && ret && strncasecmp(basedir, ret, filepart+1-basepath)!=0)
//	{
//		free(ret);
//		ret=NULL;
//	}
//	free(basedir);
//	
//	return ret;
//}
//
//static const char * cwd_init;
//static const char * cwd_bogus;
//static mutex cwd_mutex;
//
//static void window_cwd_enter(const char * dir)
//{
//	cwd_mutex.lock();
//	char * cwd_bogus_check=getcwd(NULL, 0);
//	if (strcmp(cwd_bogus, cwd_bogus_check)!=0) abort();//if this fires, someone changed the directory without us knowing - not allowed. cwd belongs to the frontend.
//	free(cwd_bogus_check);
//	ignore(chdir(dir));
//}
//
//static void window_cwd_leave()
//{
//	ignore(chdir(cwd_bogus));
//	cwd_mutex.unlock();
//}
//
//const char * window_get_cwd()
//{
//	return cwd_init;
//}
//
//void _window_init_file()
//{
//	char * cwd_init_tmp=getcwd(NULL, 0);
//	char * cwdend=strrchr(cwd_init_tmp, '/');
//	if (!cwdend) cwd_init="/";
//	else if (cwdend[1]=='/') cwd_init=cwd_init_tmp;
//	else
//	{
//		size_t cwdlen=strlen(cwd_init_tmp);
//		char * cwd_init_fixed=malloc(cwdlen+1+1);
//		memcpy(cwd_init_fixed, cwd_init_tmp, cwdlen);
//		cwd_init_fixed[cwdlen+0]='/';
//		cwd_init_fixed[cwdlen+1]='\0';
//		cwd_init=cwd_init_fixed;
//		free(cwd_init_tmp);
//	}
//}


#if defined(__x86_64__) || defined(__i386__)
static const long pagesize = 4096;
#else
#warning better hardcode the page size
static const long pagesize = sysconf(_SC_PAGESIZE);
#endif

namespace {
	class file_unix : public file::impl {
	public:
		int fd;
		
		file_unix(int fd) : fd(fd) {}
		
		size_t size()
		{
			return lseek(fd, 0, SEEK_END);
		}
		bool resize(size_t newsize)
		{
			return (ftruncate(this->fd, newsize) == 0);
		}
		
		size_t pread(arrayvieww<uint8_t> target, size_t start)
		{
			size_t ret = ::pread(fd, target.ptr(), target.size(), start);
			if (ret<0) return 0;
			else return ret;
		}
		bool pwrite(arrayview<uint8_t> data, size_t start)
		{
			size_t ret = ::pwrite(fd, data.ptr(), data.size(), start);
			if (ret<0) return 0;
			else return ret;
		}
		
		/*private*/ arrayvieww<uint8_t> mmap(bool writable, size_t start, size_t len)
		{
			//TODO: for small things (64KB? 1MB?), use malloc, it's faster
			//http://lkml.iu.edu/hypermail/linux/kernel/0004.0/0728.html
			size_t offset = start % pagesize;
			void* data = ::mmap(NULL, len+offset, writable ? PROT_WRITE|PROT_READ : PROT_READ, MAP_SHARED, this->fd, start-offset);
			if (data == MAP_FAILED) return NULL;
			return arrayvieww<uint8_t>((uint8_t*)data+offset, len);
		}
		
		arrayview<uint8_t> mmap(size_t start, size_t len) { return mmap(false, start, len); }
		void unmap(arrayview<uint8_t> data)
		{
			size_t offset = (uintptr_t)data.ptr() % pagesize;
			munmap((char*)data.ptr()-offset, data.size()+offset);
		}
		
		arrayvieww<uint8_t> mmapw(size_t start, size_t len) { return mmap(true, start, len); }
		bool unmapw(arrayvieww<uint8_t> data)
		{
			unmap(data);
			// manpage documents no errors for the case where file writing fails, gotta assume it never does
			return true;
		}
		
		~file_unix() { close(fd); }
	};
}

file::impl* file::open_impl_fs(cstring filename, mode m)
{
	static const int flags[] = { O_RDONLY, O_RDWR|O_CREAT, O_RDWR, O_RDWR|O_CREAT|O_TRUNC, O_RDWR|O_CREAT|O_EXCL };
	int fd = ::open(filename.c_str(), flags[m]|O_CLOEXEC, 0644);
	if (fd<0) return NULL;
	
	struct stat st;
	fstat(fd, &st);
	if (!S_ISREG(st.st_mode)) // no opening directories
	{
		::close(fd);
		return NULL;
	}
	
	return new file_unix(fd);
}

bool file::mkdir_fs(cstring filename)
{
	int ret = ::mkdir(filename.c_str(), 0755);
	if (ret==0) return true;
	if (ret==-1 && errno==EEXIST)
	{
		struct stat st;
		if (lstat(filename.c_str(), &st) < 0) return false;
		return (S_ISDIR(st.st_mode));
	}
	return false;
}

bool file::unlink_fs(cstring filename)
{
	int ret = ::unlink(filename.c_str());
	return ret==0 || (ret==-1 && errno==ENOENT);
}

array<string> file::listdir(cstring path)
{
	DIR* dir = opendir(path.c_str());
	if (!dir) return NULL;
	
	array<string> ret;
	dirent* ent;
	while ((ent = readdir(dir)))
	{
		// don't use strcmp, its constant overhead is suboptimal at best
		if (UNLIKELY(ent->d_name[0] == '.'))
		{
			if (ent->d_name[1] == '\0') continue;
			if (ent->d_name[1] == '.' && ent->d_name[2] == '\0') continue;
		}
		
		string childpath = path + ent->d_name;
		if (ent->d_type == DT_UNKNOWN)
		{
			struct stat st;
			stat(childpath, &st);
			if (S_ISDIR(st.st_mode)) childpath += "/";
		}
		else if (ent->d_type == DT_DIR) childpath += "/";
		ret.append(std::move(childpath));
	}
	closedir(dir);
	return ret;
}


/*
static char* g_cwd;
cstring file::cwd() { return g_cwd; }

void arlib_init_file()
{
	string cwd = string::create_usurp(getcwd(NULL, 0));
	if (!cwd.endswith("/")) cwd += "/";
	g_cwd = strdup(cwd);
	
	//char * cwd_init_tmp=getcwd(NULL, 0);
	//char * cwdend=strrchr(cwd_init_tmp, '/');
	//if (!cwdend) cwd_init="/";
	//else if (cwdend[1]=='/') cwd_init=cwd_init_tmp;
	//else
	//{
	//	size_t cwdlen=strlen(cwd_init_tmp);
	//	char * cwd_init_fixed=malloc(cwdlen+1+1);
	//	memcpy(cwd_init_fixed, cwd_init_tmp, cwdlen);
	//	cwd_init_fixed[cwdlen+0]='/';
	//	cwd_init_fixed[cwdlen+1]='\0';
	//	cwd_init=cwd_init_fixed;
	//	free(cwd_init_tmp);
	//}
	
//	//disable cwd
//	//try a couple of useless directories and hope one of them works
//	//this seems to be the best one:
//	//- even root can't create files here
//	//- it contains no files with a plausible name on a standard Ubuntu box
//	//    (I have an ath9k-phy0, and a bunch of stuff with colons, nothing will ever want those filenames)
//	//- a wild write will not do anything dangerous except turn on some lamps
//	!chdir("/sys/class/leds/") ||
//		//the rest are in case it's not accessible (weird chroot? not linux?), so try some random things
//		!chdir("/sys/") ||
//		!chdir("/dev/") ||
//		!chdir("/home/") ||
//		!chdir("/tmp/") ||
//		!chdir("/");
//	cwd_bogus = getcwd(NULL, 0); // POSIX does not specify getcwd(NULL), it's Linux-specific
}
*/



#define MMAP_THRESHOLD 1024*1024
file2::mmap_t file2::mmap(cstring filename)
{
	int fd = open(filename.c_str(), O_RDONLY|O_CLOEXEC);
	if (fd < 0) return NULL;
	off_t size = lseek(fd, 0, SEEK_END);
	if (size < 0) return NULL;
	uint8_t * data;
	if (size <= MMAP_THRESHOLD)
	{
		data = xmalloc(size);
		if (pread(fd, data, size, 0) != size)
		{
			free(data);
			close(fd);
			return NULL;
		}
	}
	else
	{
		data = (uint8_t*)::mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
		if (data == MAP_FAILED) return NULL;
	}
	close(fd);
	return { data, (size_t)size };
}
file2::mmap_t::~mmap_t()
{
	if (count > MMAP_THRESHOLD) munmap(items, count);
	else free(items);
}
//writable mmap may not use malloc optimization
#endif
