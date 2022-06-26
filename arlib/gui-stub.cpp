#if defined(ARLIB_GUI_GTK3) || defined(ARLIB_GUI_GTK4)
#include "global.h"

void arlib_init();
bool arlib_try_init();

#include <gtk/gtk.h>

static void init_gui_shared_early()
{
	gtk_disable_setlocale(); // go away, you're a library like any other and have no right to mess with libc config
	// locale plus libraries is a terrible combination, it uglifies debug printfs and breaks everything that didn't reinvent strtod/sprintf
	// for more details, see https://github.com/mpv-player/mpv/commit/1e70e82baa9193f6f027338b0fab0f5078971fbe
}

static void init_gui_shared_late()
{
}

#ifdef ARLIB_GUI_GTK3
void arlib_init()
{
	int argc = 1;
	char * argv[] = { (char*)"arlib", nullptr };
	char** argv_p = argv;
	init_gui_shared_early();
	gtk_init(&argc, &argv_p);
	init_gui_shared_late();
}

bool arlib_try_init()
{
	int argc = 1;
	char * argv[] = { (char*)"arlib", nullptr };
	char** argv_p = argv;
	init_gui_shared_early();
	bool ret = gtk_init_check(&argc, &argv_p);
	init_gui_shared_late();
	return ret;
}
#endif

#ifdef ARLIB_GUI_GTK4
void arlib_init()
{
	init_gui_shared_early();
	gtk_init();
	init_gui_shared_late();
}

bool arlib_try_init()
{
	init_gui_shared_early();
	bool ret = gtk_init_check();
	init_gui_shared_late();
	return ret;
}
#endif
#endif
