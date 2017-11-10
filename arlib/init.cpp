#include "init.h"
#include "runloop.h"

void arlib_init(void* args, char** argv)
{
#ifndef ARGUI_NONE
	arlib_init_gui(args, argv);
#else
	//TODO: use argument parser
#endif
	arlib_init_file();
	srand(time(NULL));
}
