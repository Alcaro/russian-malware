#include "terminal.h"

// on Linux, terminal existence can be checked with getenv("TERM")
// whether stdout is devnull can be checked with fstat(STDOUT_FILENO).st_mode&S_IFMT == S_IFCHR, .st_rdev=makedev(1, 3)

#ifdef _WIN32
#include <windows.h>

void terminal_enable()
{
	//doesn't create a new console if not launched from one, it'd go away on app exit anyways
	//doesn't like being launched from cmd; cmd wants to run a new command if spawning a gui app
	//  I can't make it not be a gui app, that flashes a console; it acts sanely from batch files
	//windows consoles are, like so much else, a massive mess
	
	bool attach_stdin  = (GetFileType(GetStdHandle(STD_INPUT_HANDLE))  == FILE_TYPE_UNKNOWN);
	bool attach_stdout = (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN);
	bool attach_stderr = (GetFileType(GetStdHandle(STD_ERROR_HANDLE))  == FILE_TYPE_UNKNOWN);
	
	AttachConsole(ATTACH_PARENT_PROCESS); // this one messes up the std handles
	
	if (attach_stdin) freopen("CONIN$", "rt", stdin);
	if (attach_stdout) { freopen("CONOUT$", "wt", stdout); fputc('\r', stdout); }
	if (attach_stderr) { freopen("CONOUT$", "wt", stderr); fputc('\r', stdout); }
	
	//return GetConsoleWindow() || !attach_stdout;
}
#endif
