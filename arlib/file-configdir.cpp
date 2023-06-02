#include "file.h"

#ifdef __linux__
#include <unistd.h>
#include <dirent.h>

namespace {
struct config_finder {
	string path;
	
	// Input must contain a slash. (It's already checked that it starts with a slash.)
	static bool ends_with_slash(const char * path)
	{
		return (strrchr(path, '/')[1] == '\0');
	}
	
	config_finder()
	{
		const char * config = getenv("XDG_CONFIG_HOME");
		if (config && config[0] == '/')
		{
			if (ends_with_slash(config))
				path = config;
			else
				path = cstring(config) + "/";
		}
		else
		{
			const char * home = getenv("HOME");
			if (!home || home[0] != '/') home = "/"; // should never happen, but...
			
			if (ends_with_slash(home))
				path = cstring(home) + ".config/";
			else
				path = cstring(home) + "/.config/";
		}
	}
};

static config_finder g_config;
}

cstrnul file2::dir_config() { return g_config.path; }

#include "test.h"
test("file::dir_config", "string", "file")
{
	assert(file2::dir_config().endswith("/.config/"));
}
#endif
