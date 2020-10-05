#define WANT_VALGRIND
#include "os.h"
#include "thread.h"
#include "test.h"

#ifdef __unix__
#include <dlfcn.h>

//static mutex dylib_lock;
//
//static void* dylib_load_uniq(const char * filename, bool force)
//{
//	synchronized(dylib_lock)
//	{
//		//try loading it normally first, to avoid loading libc twice if not needed
//		//duplicate libcs probably don't work very well
//		void* test = dlopen(filename, RTLD_NOLOAD);
//		if (!test) return dlopen(filename, RTLD_LAZY);
//		
//		dlclose(test);
//#ifdef __linux__
//		if (force)
//			return dlmopen(LM_ID_NEWLM, filename, RTLD_LAZY);
//		else
//#endif
//			return NULL;
//	}
//	return NULL; // unreachable, bug in synchronized() and/or gcc
//}

bool dylib::init(const char * filename)
{
	if (handle) abort();
	//synchronized(dylib_lock)
	{
		handle = dlopen(filename, RTLD_LAZY);
	}
	return handle;
}

//bool dylib::init_uniq(const char * filename)
//{
//	if (handle) abort();
//	handle = dylib_load_uniq(filename, false);
//	return handle;
//}
//
//bool dylib::init_uniq_force(const char * filename)
//{
//	if (handle) abort();
//	handle = dylib_load_uniq(filename, true);
//	return handle;
//}

void* dylib::sym_ptr(const char * name)
{
	if (!handle) return NULL;
	return dlsym(handle, name);
}

void dylib::deinit()
{
	if (handle) dlclose(handle);
	handle = NULL;
}
#endif


#ifdef _WIN32
#include <windows.h>

static mutex dylib_lock;

bool dylib::init(const char * filename)
{
	if (handle) abort();
	
	//if (uniq)
	//{
	//	if (GetModuleHandleEx(0, filename, (HMODULE*)&handle)) return NULL;
	//}
	
	// this is so dependencies, for example winpthread-1.dll, can be placed beside the dll where they belong
	char * filename_copy = strdup(filename);
	char * filename_copy_slash = strrchr(filename_copy, '/');
	if (!filename_copy_slash) filename_copy_slash = strrchr(filename_copy, '\0');
	filename_copy_slash[0]='\0';
	
	// two threads racing on SetDllDirectory is bad news
	// (hope nothing else uses SetDllDirectory; even without threads, global state is bad news)
	// (and even without dlls, this thing is inherited by child processes for some absurd reason?)
	synchronized(dylib_lock)
	{
		SetDllDirectory(filename_copy);
		handle = LoadLibrary(filename);
		SetDllDirectory(NULL);
	}
	free(filename_copy);
	return handle;
}

//bool dylib::init_uniq(const char * filename)
//{
//	deinit();
//	handle = dylib_init(filename, true);
//	return handle;
//}
//
//bool dylib::init_uniq_force(const char * filename)
//{
//	return init_uniq(filename);
//}

void* dylib::sym_ptr(const char * name)
{
	if (!handle) return NULL;
	return (void*)GetProcAddress((HMODULE)handle, name);
}

void dylib::deinit()
{
	if (handle) FreeLibrary((HMODULE)handle);
	handle = NULL;
}
#endif

bool dylib::sym_multi(funcptr* out, const char * names)
{
	bool all = true;
	
	while (*names)
	{
		*out = this->sym_func(names);
		if (!*out) all = false;
		
		out++;
		names += strlen(names)+1;
	}
	
	return all;
}


void not_a_function(); // linker error if the uses aren't optimized out
DECL_DYLIB_T(libc_t, not_a_function, fread, mktime, atoi);
DECL_DYLIB_PREFIX_T(libc_f_t, f, open, read, close);

test("dylib", "", "dylib")
{
	libc_t libc;
	libc_f_t f;
	assert(!libc.fread);
#ifdef __linux__
	const char * libc_so = "libc.so.6";
#endif
#ifdef _WIN32
	const char * libc_so = "msvcrt.dll";
#endif
	assert(!libc.init(libc_so));
	assert(f.init(libc_so));
	assert(libc.fread); // these guys must exist, despite not_a_function failing
	assert(libc.mktime);
	assert(libc.atoi);
	assert_eq(libc.atoi("123"), 123);
	assert(!libc.not_a_function);
	assert(f.read == libc.fread);
}
