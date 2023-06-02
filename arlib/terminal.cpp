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

void terminal_pause_if_standalone(const char * msg)
{
	// if launched from cmd, there's another process in this console, and we're not standalone
	DWORD processes[1];
	DWORD num_processes = GetConsoleProcessList(processes, ARRAY_SIZE(processes));
	if (num_processes != 1)
		return;
	
	// if running in Wine, we're launched from some Linux program; not standalone
	// in a 64bit process under Wine, the console is shared with a C:/windows/syswow64/start.exe, so the above test returns 'don't pause'
	// but that feels fragile, and it's absent on 32bit, so let's explicitly check for Wine anyways
	if (GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version") != nullptr)
		return;
	
	// if stdin is redirected, reading from it will return immediately; better not try
	HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE); // never fails
	DWORD mode;
	bool have_console = GetConsoleMode(h_stdin, &mode);
	if (!have_console)
		return;
	
	puts(msg);
	SetConsoleMode(h_stdin, 0);
	WCHAR trash[1]; // utf16 is disgusting, but the risk of reading half a character is even worse
	DWORD n_chars;
	ReadConsoleW(h_stdin, trash, ARRAY_SIZE(trash), &n_chars, nullptr);
	SetConsoleMode(h_stdin, mode);
}
#endif
