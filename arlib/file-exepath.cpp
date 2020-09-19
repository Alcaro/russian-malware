#include "file.h"

#ifdef __unix__
#include <unistd.h>
//separate file so this ctor can be optimized out if unused

namespace {
struct exepath_finder {
	string path;
	
	exepath_finder() // can't oninit() to initialize a string, globals' ctors' order is implementation defined and often wrong
	{
		path.construct(64);
		
	again: ;
		char * ptr = (char*)path.bytes().ptr();
		size_t buflen = path.length();
		ssize_t r = readlink("/proc/self/exe", ptr, buflen);
		if (r <= 0) abort();
		if ((size_t)r >= buflen-1)
		{
			path.construct(buflen * 2);
			goto again;
		}
		
		ptr[r] = '\0';
		char * end = strrchr(ptr, '/')+1; // a / is known to exist
		path = path.substr(0, end-ptr);
	}
};

static exepath_finder g_path;
}

cstring file::exepath() { return g_path.path; }
#endif

#ifdef _WIN32
#include <windows.h>

static char g_exepath[MAX_PATH];

oninit_static()
{
	GetModuleFileName(NULL, g_exepath, MAX_PATH);
	for (int i=0;g_exepath[i];i++)
	{
		if (g_exepath[i]=='\\') g_exepath[i]='/';
	}
	char * end=strrchr(g_exepath, '/');
	if (end) end[1]='\0';
}

cstring file::exepath() { return g_exepath; }
#endif
