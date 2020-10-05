#include "file.h"

#ifdef __unix__
#include <unistd.h>
//separate file so this ctor can be optimized out if unused

namespace {
struct cwd_finder {
	string path;
	
	cwd_finder() // can't oninit() to initialize a string, globals' ctors' order is implementation defined and often wrong
	{
#ifdef __linux__
		path = string::create_usurp(getcwd(NULL, 0));
#else
#error TODO
#endif
		if (path[path.length()-1] != '/')
			path += "/";
	}
};

static cwd_finder g_cwd;
}

cstring file::cwd() { return g_cwd.path; }
#endif
