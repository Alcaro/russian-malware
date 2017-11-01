#pragma once
#include "string.h"

//This must be the first Arlib function called in main() (don't do anything funny in static initializers, either).
//Give it argv, and a descriptor of the arguments supported by this program. Afterwards, the arguments will be available for inspection.
//Automatically adds support for a few arguments, like --help, and --display on Linux.
//NULL means no arguments supported.

//TODO: actually add argument support
//TODO: instead of arlib_try_init, hardcode support for a --nogui parameter in args
//TODO: if gtk is enabled, decide if I should use g_option_context_parse, ignore GTK flags, or rip apart GOptionContext
//                         probably #1
void arlib_init(void* args, char** argv);


//Called by arlib_init(). Don't use them yourself.
void arlib_init_file();
void arlib_init_gui(void* args, char** & argv);
