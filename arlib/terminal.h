#include "global.h"

#ifdef _WIN32
// If stdout is redirected, it remains unchanged. Only call once.
// Use only with ARTERMINAL=hybrid.
void terminal_enable();
// Does system("pause") if the process is not launched from cmd, i.e. if its console window will disappear once the program exits.
// Use only with ARTERMINAL=1.
void terminal_pause_if_standalone(const char * msg = "Press any key to continue...");
#else
static inline void terminal_enable() {}
static inline void terminal_pause_if_standalone(const char * msg = nullptr) {}
#endif
