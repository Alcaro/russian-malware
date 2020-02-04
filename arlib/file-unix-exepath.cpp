#include "file.h"

#ifdef __unix__
#include <unistd.h>
//#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

//separate file so this oninit can be optimized out if unused

static char* g_exepath;
cstring file::exepath() { return g_exepath; }

oninit()
{
	array<char> buf;
	buf.resize(64);
	
again: ;
	ssize_t r = readlink("/proc/self/exe", buf.ptr(), buf.size());
	if (r <= 0) abort();
	if ((size_t)r >= buf.size()-1)
	{
		buf.resize(buf.size() * 2);
		goto again;
	}
	
	buf[r] = '\0';
	char * end = strrchr(buf.ptr(), '/')+1; // a / is known to exist
	*end = '\0';
	
	g_exepath = buf.release().ptr();
}
#endif
