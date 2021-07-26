#include "file.h"

#ifdef __linux__
#include <unistd.h>
#include <dirent.h>
//separate file so this ctor can be optimized out if unused

namespace {
struct exepath_finder {
	string path;
	
	// can't oninit() to initialize a string, must use a ctor; it blows up if oninit runs before string's own ctor
	// (no, I don't know why string ctor isn't optimized into the data section; it is if constexpr)
	exepath_finder()
	{
#ifdef ARTYPE_DLL
		// dladdr() can return this information, but it doesn't always contain path, so better use something else
		
		char linkpath[64];
		strcpy(linkpath, "/proc/self/map_files/");
		
		DIR* dir = opendir(linkpath);
		if (!dir) goto fallback;
		
		dirent* ent;
		while ((ent = readdir(dir)))
		{
			// for . and .., sscanf will fail, return 0, and leave low/high unchanged
			// easiest way to ensure this doesn't break anything is initialize high to 0, and check it before low, so check is false
			uintptr_t low;
			uintptr_t high = 0;
			sscanf(ent->d_name, "%zx-%zx", &low, &high);
			
			uintptr_t target = (uintptr_t)(void*)&file::exepath;
			if (target < high && low <= target)
			{
				strcat(linkpath, ent->d_name);
				break;
			}
		}
		closedir(dir);
		if (!ent)
		{
		fallback: // should be unreachable
			strcpy(linkpath, "/proc/self/exe");
		}
#else
		const char * linkpath = "/proc/self/exe";
#endif
		
		path.construct(64);
		
	again: ;
		char * ptr = (char*)path.bytes().ptr();
		size_t buflen = path.length();
		ssize_t r = readlink(linkpath, ptr, buflen);
		if (r <= 0) { path = ""; return; }
		if ((size_t)r >= buflen-1)
		{
			path.construct(buflen * 2);
			goto again;
		}
		
		ptr[r] = '\0';
		char * end = strrchr(ptr, '/')+1; // at least one / is known to exist
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
	HMODULE hmod;
#ifdef ARTYPE_DLL
	// TODO: check if I can use __ImageBase instead
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   (LPCSTR)(void*)&file::exepath, &hmod);
#else
	hmod = NULL;
#endif
	GetModuleFileName(hmod, g_exepath, MAX_PATH);
	for (int i=0;g_exepath[i];i++)
	{
		if (g_exepath[i]=='\\') g_exepath[i]='/';
	}
	strrchr(g_exepath, '/')[1] = '\0';
}

cstring file::exepath() { return g_exepath; }
#endif
