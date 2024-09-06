#include "file.h"

// separate file so the ctors can be optimized out if unused
// for other platforms, see https://stackoverflow.com/a/1024937

#ifdef __linux__
#include <unistd.h>
#include <dirent.h>

namespace {
struct exepath_finder {
	string fullname;
	cstring path;
	
	// can't oninit() to initialize a string, must use a ctor; it blows up if oninit runs before string's own ctor
	// (no, I don't know why string ctor isn't optimized into the data section; it is if constexpr)
	exepath_finder()
	{
#ifdef ARTYPE_DLL
		// dladdr() can return this information, but it doesn't always contain path, so better use something else
		
		// TODO: on kernel >= 6.11, use ioctl(PROCMAP_QUERY) instead
		
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
			
			uintptr_t target = (uintptr_t)this;
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
		
		fullname = file::readlink(linkpath);
		path = file::dirname(fullname);
	}
};

static exepath_finder g_path;
}

cstring file::exedir() { return g_path.path; }
cstrnul file::exepath() { return g_path.fullname; }
#endif

#ifdef _WIN32
#include <windows.h>

extern "C" const IMAGE_DOS_HEADER __ImageBase;

// https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation says max 32767 utf16 units, so let's fit that
static uint8_t g_exepath[32768*3];
static uint32_t g_exepath_dirlen = 0;

oninit_static()
{
	wchar_t utf16_buf[32768];
	size_t utf16_len = GetModuleFileNameW((HMODULE)&__ImageBase, utf16_buf, 32768);
	smelly_string::utf16_to_utf8_buf(arrayview<wchar_t>(utf16_buf, utf16_len+1), g_exepath);
	
	for (size_t i=0;g_exepath[i];i++)
	{
		if (g_exepath[i] == '\\') g_exepath[i] = '/';
		if (g_exepath[i] == '/') g_exepath_dirlen = i+1;
	}
}

cstring file::exedir() { return cstring(bytesr(g_exepath, g_exepath_dirlen)); }
cstrnul file::exepath() { return (char*)g_exepath; }
#endif

#include "test.h"
test("file::exepath", "string", "file")
{
#ifdef _WIN32
	static const uint8_t magic[] = { 'M', 'Z' };
#else
	static const uint8_t magic[] = { 0x7F, 'E', 'L', 'F' };
#endif
	assert_eq((bytesr)file::readall(file::exepath()).slice(0,sizeof(magic)), (bytesr)magic);
}
