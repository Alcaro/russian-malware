#include "global.h"

// If stdout is redirected, it remains unchanged. Only call once.
#ifdef _WIN32
void terminal_enable();
#else
static inline void terminal_enable() {}
#endif
