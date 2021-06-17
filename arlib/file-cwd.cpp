#include "file.h"

//separate file so this ctor can be optimized out if unused

#ifdef __unix__
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
struct cwd_finder {
	string path;
	
	cwd_finder() // can't oninit() to initialize a string, globals' ctors' order is implementation defined and often wrong
	{
#if defined(_WIN32)
		char chars[PATH_MAX];
		GetCurrentDirectory(sizeof(chars), chars);
		char* iter = chars;
		while (*iter)
		{
			if (*iter == '\\') *iter = '/';
			iter++;
		}
		if (iter[-1] != '/') *iter++ = '/';
		path = cstring(bytesr((uint8_t*)chars, iter-chars));
#elif defined(__linux__)
		path = string::create_usurp(getcwd(NULL, 0));
		if (path[path.length()-1] != '/')
			path += "/";
#else
#error TODO
#endif
	}
};

static cwd_finder g_cwd;
}

const string& file::cwd() { return g_cwd.path; }
