#include "init.h"
#include "runloop.h"

void arlib_init(void* args, char** argv)
{
#ifndef ARGUI_NONE
	arlib_init_gui(args, argv);
#endif
	arlib_init_file();
	srand(time(NULL));
#ifndef ARGUI_GTK3
#error TODO
#endif
}
