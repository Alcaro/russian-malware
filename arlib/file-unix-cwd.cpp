#include "file.h"

#ifdef __unix__
#include <unistd.h>

//separate file so this oninit can be optimized out if unused

static char* g_cwd;
cstring file::cwd() { return g_cwd; }

oninit()
{
	char* cwd;
#ifdef __linux__
	cwd = getcwd(NULL, 0);
#else
#error TODO
#endif
	
	size_t n = strlen(cwd);
	if (cwd[n-1] != '/')
	{
		char* tmp = malloc(n+2);
		strcpy(tmp, cwd);
		tmp[n] = '/';
		tmp[n+1] = '\0';
		free(cwd);
		g_cwd = tmp;
	}
	else g_cwd = cwd;
}
#endif
