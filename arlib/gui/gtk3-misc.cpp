#include "window.h"
#ifdef ARGUI_GTK3
#include "../file.h"
#include "../os.h"
#include "../set.h"
#include "../init.h"
#include "../test.h"
#ifdef ARLIB_TESTRUNNER
#include "../process.h"
#endif
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <gtk/gtk.h>
#ifdef ARGUIPROT_X11
#include <gdk/gdkx.h>
#else
//TODO: if not X11, disable Viewport but keep other widgets
#error Only X11 supported.
#endif

//TODO: check if libinput provides all information I can get from the raw kernel interface, and if it needs root
//https://gitlab.freedesktop.org/libinput/libinput/blob/master/tools/libinput-debug-gui.c
//while that one uses gtk, they don't seem to know about each other

//Number of ugly hacks: 8
//The status bar is a GtkGrid with GtkLabel, not a GtkStatusbar, because I couldn't get GtkStatusbar
// to cooperate. While the status bar is a GtkBox, I couldn't find how to get rid of its child.
//The status bar manages layout manually (by resizing the GtkLabels), because a GtkGrid with a large
// number of slots seem to assign pixels 3,3,2,2 if told to split 10 pixels to 4 equally sized
// slots, as opposed to 2,3,2,3 or 3,2,3,2.
//Label widgets that ellipsize, as well as status bar labels, are declared with max width 1, and
// then ellipsizes. Apparently they use more space than maximum if they can. This doesn't seem to be
// documented, and is therefore not guaranteed to continue working.
//gtk_main_iteration_do(false) is called twice, so GTK thinks we're idle and sends out the mouse
// move events. Most of the time is spent waiting for A/V drivers, and our mouse move processor is
// cheap. (Likely fixable in GTK+ 3.12, but I'm on 3.8.)
//Refreshing a listbox is done by telling it to redraw, not by telling it that contents have changed.
// It's either that or send tens of thousands of contents-changed events, and I'd rather not.
//gtk_widget_override_background_color does nothing to GtkTextEntry, due to a background-image
// gradient. I had to throw a bit of their fancy CSS at it.
//GtkTreeView has non-constant performance for creating a pile of rows, and gets terrible around
// 100000. I had to cap it to 65536.
//The size of GtkTreeView is complete nonsense. I haven't found how to get it to give out its real
// row height, nor could I figure out what the nonsense I get from the tell-me-your-height functions
// is, but I am sure that whatever tricks I will need to pull fits here.

//Known unfixable bugs: 1
//In the following simple program (or anything that detects scroll events)
/*
int main(int argc, char** argv)
{
	gtk_init(&argc, &argv);
	
	GtkTextView* view = GTK_TEXT_VIEW(gtk_text_view_new());
	GtkScrolledWindow* scrollview = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
	gtk_container_add(GTK_CONTAINER(scrollview), GTK_WIDGET(view));
	
	GtkWindow* wnd = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
	gtk_container_add(GTK_CONTAINER(wnd), GTK_WIDGET(scrollview));
	
	gtk_widget_show_all(GTK_WIDGET(wnd));
	gtk_main();
	return 0;
}
*/
//the first scroll event on the scrollwindow is ignored. This is a bug in Gdk's XInput2 handler.
//It can be worked around by calling gdk_disable_multidevice(), but that has the obvious drawbacks.
//It seems to be related to scroll->last_value_valid as seen by _gdk_x11_device_xi2_get_scroll_delta.
//Unfortunately, I don't think the bug is fixable; the XInput2 header doesn't seem to include any way to query the XIValuatorState.
//The only option I can see is to assume it always starts at 0, but it would surprise me if that's true 100% of the time.

//static GdkFilterReturn scanfilter(GdkXEvent* xevent, GdkEvent* event, gpointer data)
//{
//	XEvent* ev=(XEvent*)xevent;
//	if (ev->type==Expose) printf("ex=%lu\n", ev->xexpose.window);
//	return GDK_FILTER_CONTINUE;
//}

//#include<sys/resource.h>

#if defined(ARGUIPROT_X11) && defined(ARLIB_OPENGL)
struct window_x11_info window_x11;
#endif

static void init_gui_shared_early()
{
	setenv("NO_AT_BRIDGE", "1", true); // https://askubuntu.com/questions/1086294/terminal-and-nautilus-stopped-working-after-a-crash
#ifdef DEBUG
	g_log_set_always_fatal((GLogLevelFlags)(G_LOG_LEVEL_CRITICAL|G_LOG_LEVEL_WARNING));
#endif
#if defined(ARGUIPROT_X11) && defined(ARLIB_OPENGL)
	gdk_set_allowed_backends("x11");
	XInitThreads();
#endif
	gtk_disable_setlocale(); // go away, you're a library like any other and have no right to mess with libc config
	// locale plus libraries is a terrible combination, it breaks my json parser and probably 999 other things
	// for more details, see https://github.com/mpv-player/mpv/commit/1e70e82baa9193f6f027338b0fab0f5078971fbe
}

static void init_gui_shared_late()
{
#if defined(ARGUIPROT_X11) && defined(ARLIB_OPENGL)
	if (args.m_has_gui)
	{
		window_x11.display = gdk_x11_get_default_xdisplay();
		window_x11.screen = gdk_x11_get_default_screen();
	}
#endif
}

void _arlib_init_gui(char** argv)
{
	init_gui_shared_early();
	int argc = 0;
	while (argv[argc]) argc++;
	gtk_init(&argc, &argv);
	init_gui_shared_late();
	
	if (argv[1])
	{
		fprintf(stderr, "%s: this program does not take arguments\n", argv[0]);
	}
}
void _arlib_init_gui_manual_args(int* argc, char*** argv)
{
	init_gui_shared_early();
	gtk_init(argc, argv);
	init_gui_shared_late();
}
void _arlib_init_gui(argparse& args, char** argv)
{
	init_gui_shared_early();
	
	args.parse_pre(argv);
	
	//needs to be static so the callback can access it - I can't pass userdata to gtk_init_with_args
	static argparse* argsp;
	argsp = &args;
	
	//gtk won't let me rip apart its option parser and add its flags to mine
	//have to rip apart my parser and add it to gtk instead
	//why does gtk keep insisting on being as obnoxious as possible to make optional
	//(another example: gtk_gl_area_attach_buffers(). see rant in opengl/ctx-x11.cpp)
	array<GOptionEntry> gtkargs;
	gtkargs.resize(args.m_args.size()+1);
	for (size_t i=0;i<args.m_args.size();i++)
	{
		argparse::arg_base& arg = args.m_args[i];
		GOptionEntry& gtkarg = gtkargs[i];
		
		gtkarg.long_name = arg.name;
		gtkarg.short_name = arg.sname;
		if (!arg.accept_value) gtkarg.flags = G_OPTION_FLAG_NO_ARG;
		else gtkarg.flags = (arg.accept_no_value ? G_OPTION_FLAG_OPTIONAL_ARG : 0);
		
		gtkarg.arg = G_OPTION_ARG_CALLBACK;
		GOptionArgFunc tmp =
			[](const gchar * option_name, const gchar * value, gpointer data, GError* * error) -> gboolean
			{
				//option_name - The name of the option being parsed. This will be either a single dash
				//  followed by a single letter (for a short name) or two dashes followed by a long option name.
				//except for G_OPTION_REMAINING aka blank, where option_name is "", which of course isn't documented anywhere.
				cstring name = "-"; // both of these default to values not existing anywhere
				char sname = -1;
				
				if (!option_name[0]) name = "";
				else if (option_name[1] == '-') name = option_name+2;
				else sname = option_name[1];
				
				for (argparse::arg_base& arg : argsp->m_args)
				{
					if (arg.name == name || arg.sname == sname)
					{
						argsp->single_arg(arg, value, argparse::al_tight, NULL);
						return true;
					}
				}
				abort(); // should be unreachable
			};
		gtkarg.arg_data = (void*)tmp; // need a temporary variable because some gcc versions get confused by casting a lambda expression
		
		gtkarg.description = NULL;
		gtkarg.arg_description = NULL;
	}
	
	GError* error = NULL;
	int argc = 0;
	while (argv[argc]) argc++; // no clue why it needs an argc, but it does
	gtk_init_with_args(&argc, &argv, NULL, gtkargs.ptr(), NULL, &error);
	
	if (argv[1])
	{
		if (argv[1][0]=='-') args.error((cstring)"unknown argument: "+argv[1]);
		else args.error("positional arguments not supported");
	}
	args.parse_post();
	
	args.m_has_gui = true;
	if (error != NULL)
	{
		//shitty way to detect if the error is windowing initialization or bad arguments, but gtk doesn't seem to offer anything better
		//this is, of course, twice the fun with localization enabled
		if (args.m_accept_cli && strstr(error->message, "annot open display"))
			args.m_has_gui = false;
		else
			args.error(error->message);
		g_clear_error(&error);
	}
	
	init_gui_shared_late();
}

bool window_console_avail()
{
	return getenv("TERM");
}

bool window_console_attach()
{
	//nothing to do
	return window_console_avail();
}

string window_config_path()
{
	return g_get_user_config_dir();
}

//file* file::create(const char * filename)
//{
//	//TODO
//	return create_fs(filename);
//}

//static void * mem_from_g_alloc(void * mem, size_t size)
//{
//	if (g_mem_is_system_malloc()) return mem;
//	
//	if (!size) size=strlen((char*)mem)+1;
//	
//	void * ret=malloc(size);
//	memcpy(ret, mem, size);
//	g_free(ret);
//	return ret;
//}
//
////enum mbox_sev { mb_info, mb_warn, mb_err };
////enum mbox_btns { mb_ok, mb_okcancel, mb_yesno };
//bool window_message_box(const char * text, const char * title, enum mbox_sev severity, enum mbox_btns buttons)
//{
//	//"Please note that GTK_BUTTONS_OK, GTK_BUTTONS_YES_NO and GTK_BUTTONS_OK_CANCEL are discouraged by the GNOME HIG."
//	//I do not listen to advise without a rationale. Tell me which section it violates, and I'll consider it.
//	GtkMessageType sev[3]={ GTK_MESSAGE_OTHER, GTK_MESSAGE_WARNING, GTK_MESSAGE_ERROR };
//	GtkButtonsType btns[3]={ GTK_BUTTONS_OK, GTK_BUTTONS_OK_CANCEL, GTK_BUTTONS_YES_NO };
//	GtkWidget* dialog=gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, sev[severity], btns[buttons], "%s", text);
//	gint ret=gtk_dialog_run(GTK_DIALOG(dialog));
//	gtk_widget_destroy(dialog);
//	return (ret==GTK_RESPONSE_ACCEPT || ret==GTK_RESPONSE_OK || ret==GTK_RESPONSE_YES);
//}
//
//const char * const * window_file_picker(window * parent,
//                                        const char * title,
//                                        const char * const * extensions,
//                                        const char * extdescription,
//                                        bool dylib,
//                                        bool multiple)
//{
//	static char * * ret=NULL;
//	if (ret)
//	{
//		char * * del=ret;
//		while (*del)
//		{
//			free(*del);
//			del++;
//		}
//		free(ret);
//		ret=NULL;
//	}
//	
//	GtkFileChooser* dialog=GTK_FILE_CHOOSER(
//	                         gtk_file_chooser_dialog_new(
//	                           title,
//	                           GTK_WINDOW(parent?(void*)parent->_get_handle():NULL),
//	                           GTK_FILE_CHOOSER_ACTION_OPEN,
//	                           "_Cancel",
//	                           GTK_RESPONSE_CANCEL,
//	                           "_Open",
//	                           GTK_RESPONSE_ACCEPT,
//	                           NULL));
//	gtk_file_chooser_set_select_multiple(dialog, multiple);
//	gtk_file_chooser_set_local_only(dialog, dylib);
//	
//	GtkFileFilter* filter;
//	
//	if (*extensions)
//	{
//		filter=gtk_file_filter_new();
//		gtk_file_filter_set_name(filter, extdescription);
//		char extstr[64];
//		extstr[0]='*';
//		extstr[1]='.';
//		while (*extensions)
//		{
//			strcpy(extstr+2, *extensions+(**extensions=='.'));
//			gtk_file_filter_add_pattern(filter, extstr);
//			extensions++;
//		}
//		gtk_file_chooser_add_filter(dialog, filter);
//	}
//	
//	filter=gtk_file_filter_new();
//	gtk_file_filter_set_name(filter, "All files");
//	gtk_file_filter_add_pattern(filter, "*");
//	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
//	
//	if (gtk_dialog_run(GTK_DIALOG(dialog))!=GTK_RESPONSE_ACCEPT)
//	{
//		gtk_widget_destroy(GTK_WIDGET(dialog));
//		return NULL;
//	}
//	
//	GSList * list=gtk_file_chooser_get_uris(dialog);
//	gtk_widget_destroy(GTK_WIDGET(dialog));
//	unsigned int listlen=g_slist_length(list);
//	if (!listlen)
//	{
//		g_slist_free(list);
//		return NULL;
//	}
//	ret=malloc(sizeof(char*)*(listlen+1));
//	
//	char * * retcopy=ret;
//	GSList * listcopy=list;
//	while (listcopy)
//	{
//		*retcopy=window_get_absolute_path(NULL, (char*)listcopy->data, true);
//		g_free(listcopy->data);
//		retcopy++;
//		listcopy=listcopy->next;
//	}
//	ret[listlen]=NULL;
//	g_slist_free(list);
//	return (const char * const *)ret;
//}



namespace {
class file_gtk : public file::impl {
public:
	/*
	m_read,
	m_write,          // If the file exists, opens it. If it doesn't, creates a new file.
	m_wr_existing,    // Fails if the file doesn't exist.
	m_replace,        // If the file exists, it's either deleted and recreated, or truncated.
	m_create_excl,    // Fails if the file does exist.
	*/
	GFile* file;
	GFileIOStream* io;
	GSeekable* seek;
	GInputStream* input;
	GOutputStream* output;
	
	size_t size()
	{
		GFileInfo* info;
		if (this->io) info = g_file_io_stream_query_info(this->io, G_FILE_ATTRIBUTE_STANDARD_SIZE, NULL, NULL);
		else info = g_file_input_stream_query_info(G_FILE_INPUT_STREAM(this->input), G_FILE_ATTRIBUTE_STANDARD_SIZE, NULL, NULL);
		gsize size = g_file_info_get_size(info);
		g_object_unref(info);
		return size;
	}
	bool resize(size_t newsize)
	{
		if (newsize < size())
		{
			return g_seekable_truncate(this->seek, newsize, NULL, NULL);
		}
		else
		{
			uint8_t nul[1] = {0};
			return pwrite(nul, newsize-1);
		}
	}
	
	size_t pread(arrayvieww<uint8_t> target, size_t start)
	{
		if (!target.size()) return 0;
		bool ok = g_seekable_seek(this->seek, start, G_SEEK_SET, NULL, NULL);
		if (!ok) return 0;
		return g_input_stream_read(this->input, target.ptr(), target.size(), NULL, NULL);
	}
	bool pwrite(arrayview<uint8_t> data, size_t start)
	{
		bool ok = g_seekable_seek(this->seek, start, G_SEEK_SET, NULL, NULL);
		if (!ok) return false;
		size_t actual = g_output_stream_write(this->output, data.ptr(), data.size(), NULL, NULL);
		return (actual == data.size());
	}
	
	array<uint8_t> readall()
	{
		array<uint8_t> ret;
		bool ok = g_seekable_seek(this->seek, 0, G_SEEK_SET, NULL, NULL);
		if (!ok) return ret; // always return ret, named return value optimization
		
		ret.resize(4096);
		size_t actual = 0;
		while (true)
		{
			ssize_t newbytes = g_input_stream_read(this->input, ret.ptr()+actual, ret.size()-actual, NULL, NULL);
			if (newbytes < 0) { ret.reset(); return ret; }
			actual += newbytes;
			if (actual == ret.size()) ret.reserve_noinit(ret.size()*2);
			if (newbytes == 0) break;
		}
		ret.resize(actual);
		return ret;
	}
	
	arrayview<uint8_t> mmap(size_t start, size_t len) { return default_mmap(start, len); }
	void unmap(arrayview<uint8_t> data) { default_unmap(data); }
	arrayvieww<uint8_t> mmapw(size_t start, size_t len) { return default_mmapw(start, len); }
	bool unmapw(arrayvieww<uint8_t> data) { return default_unmapw(data); }
	
	~file_gtk()
	{
		if (this->io) g_object_unref(this->io);
		else
		{
			if (this->input) g_object_unref(this->input);
			if (this->output) g_object_unref(this->output);
		}
		if (this->file) g_object_unref(this->file);
	}
};

static bool path_is_glib_uri(cstring filename)
{
	for (char c : filename.bytes())
	{
		if (c == ':') return true;
		if (c == '/') return false;
	}
	return false;
}
}

file::impl* file::open_impl(cstring filename, mode m)
{
	//GFile doesn't support mmap, so let's use native if possible
	if (!path_is_glib_uri(filename)) return open_impl_fs(filename, m);
	
	file_gtk* ret = new file_gtk();
	ret->file = g_file_new_for_uri(filename.c_str());
	switch (m)
	{
		case m_read:
			ret->input = G_INPUT_STREAM(g_file_read(ret->file, NULL, NULL));
			break;
		case m_write:
			ret->io = g_file_open_readwrite(ret->file, NULL, NULL);
			if (!ret->io) ret->io = g_file_create_readwrite(ret->file, G_FILE_CREATE_NONE, NULL, NULL);
			break;
		case m_wr_existing:
			ret->io = g_file_open_readwrite(ret->file, NULL, NULL);
			break;
		case m_replace:
			ret->io = g_file_replace_readwrite(ret->file, NULL, false, G_FILE_CREATE_REPLACE_DESTINATION, NULL, NULL);
			break;
		case m_create_excl:
			ret->io = g_file_create_readwrite(ret->file, G_FILE_CREATE_NONE, NULL, NULL);
			break;
	}
	if (ret->io)
	{
		ret->input = g_io_stream_get_input_stream(G_IO_STREAM(ret->io));
		ret->output = g_io_stream_get_output_stream(G_IO_STREAM(ret->io));
	}
	if (!ret->input)
	{
		delete ret;
		return NULL;
	}
	if (ret->output) ret->seek = G_SEEKABLE(ret->output); // can't truncate on input streams, even if it's the same as the output
	else ret->seek = G_SEEKABLE(ret->input);
	return ret;
}

bool file::unlink(cstring filename)
{
	//native isn't needed here, removing a file doesn't need mmap
	//but on the other hand, it's absurdly slow, so let's use native anyways
	if (!path_is_glib_uri(filename)) return unlink_fs(filename);
	
	GFile* file = g_file_new_for_uri(filename.c_str());
	GError* err = NULL;
	bool ok = g_file_delete(file, NULL, &err);
	g_object_unref(file);
	if (ok) return true;
	ok = (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND));
	g_error_free(err);
	return ok;
}

bool file::mkdir(cstring filename)
{
	if (!path_is_glib_uri(filename)) return mkdir_fs(filename);
	
	GFile* file = g_file_new_for_uri(filename.c_str());
	g_file_make_directory(file, NULL, NULL);
	bool ret = (g_file_query_file_type(file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL) == G_FILE_TYPE_DIRECTORY);
	g_object_unref(file);
	return ret;
}



//for windows support, https://developer.gnome.org/glib/stable/glib-IO-Channels.html#g-io-channel-win32-new-socket
#include <glib-unix.h>
namespace {
class runloop_gtk : public runloop
{
public:
	struct fd_cbs {
#ifdef ARLIB_TESTRUNNER
		char* valgrind_dummy = nullptr; // an uninitialized malloc(1), used to print stack trace of the guilty allocation
#endif
		guint source_id;
		
		function<void(uintptr_t)> cb_read;
		function<void(uintptr_t)> cb_write;
	};
	map<uintptr_t,fd_cbs> fdinfo;
	
	struct timer_cb {
#ifdef ARLIB_TESTRUNNER
		char* valgrind_dummy;
#endif
		guint source_id;
		bool repeat;
		bool inside = false;
		bool deleted = false;
		bool finished = false;
		runloop_gtk* parent;
		function<void()> callback;
	};
	refarray<timer_cb> timerinfo;
	
	// if runloop is never entered (someone trying to measure startup performance by calling exit() before entering loop), don't sync
	bool need_exit_sync = false;
	
	static const GIOCondition cond_rd = (GIOCondition)(G_IO_IN |G_IO_HUP|G_IO_ERR);
	static const GIOCondition cond_wr = (GIOCondition)(G_IO_OUT|G_IO_HUP|G_IO_ERR);
	
#ifdef ARLIB_THREAD
	int submit_fds[2] = { -1, -1 };
#endif
	
	/*private*/ gboolean fd_cb(gint fd, GIOCondition condition)
	{
		fd_cbs& cbs = fdinfo.get(fd);
		     if (cbs.cb_read  && (condition & cond_rd)) cbs.cb_read(fd);
		else if (cbs.cb_write && (condition & cond_wr)) cbs.cb_write(fd);
		return G_SOURCE_CONTINUE;
	}
	//TODO: should autogenerate this from fd_cb using same methods as function.h probably
	/*private*/ static gboolean fd_cb_s(gint fd, GIOCondition condition, gpointer user_data)
	{
		return ((runloop_gtk*)user_data)->fd_cb(fd, condition);
	}
	
	void set_fd(uintptr_t fd, function<void(uintptr_t)> cb_read, function<void(uintptr_t)> cb_write) override
	{
		fd_cbs& cbs = fdinfo.get_create(fd);
#ifdef ARLIB_TESTRUNNER
		if (!cbs.valgrind_dummy)
			cbs.valgrind_dummy = malloc(1);
#endif
		if (cbs.source_id) g_source_remove(cbs.source_id);
		
		guint conds = 0;
		if (cb_read)  conds |= cond_rd;
		if (cb_write) conds |= cond_wr;
		
		if (!conds)
		{
#ifdef ARLIB_TESTRUNNER
			free(cbs.valgrind_dummy);
#endif
			fdinfo.remove(fd);
			return;
		}
		
		cbs.source_id = g_unix_fd_add(fd, (GIOCondition)conds, &runloop_gtk::fd_cb_s, this);
		cbs.cb_read = cb_read;
		cbs.cb_write = cb_write;
	}
	
	
	/*private*/ static gboolean timer_cb_s(gpointer user_data)
	{
		timer_cb* cb = (timer_cb*)user_data;
		cb->inside = true;
		cb->callback();
		cb->inside = false;
		
		if (cb->repeat && !cb->deleted) return G_SOURCE_CONTINUE;
		cb->finished = true;
		if (cb->deleted)
			cb->parent->remove_timer(cb->source_id, false);
		return G_SOURCE_REMOVE;
	}
	uintptr_t raw_set_timer_rel(unsigned ms, bool repeat, function<void()> callback) override
	{
		timer_cb& cb = timerinfo.append();
#ifdef ARLIB_TESTRUNNER
		cb.valgrind_dummy = malloc(1);
#endif
		cb.callback = callback;
		cb.parent = this;
		cb.repeat = repeat;
		if (ms < 2000) cb.source_id = g_timeout_add(ms, timer_cb_s, &cb);
		else cb.source_id = g_timeout_add_seconds((ms+750)/1000, timer_cb_s, &cb);
		return cb.source_id;
	}
	uintptr_t raw_set_idle(function<void()> callback) override
	{
		timer_cb& cb = timerinfo.append();
#ifdef ARLIB_TESTRUNNER
		cb.valgrind_dummy = malloc(1);
#endif
		cb.callback = callback;
		cb.parent = this;
		cb.repeat = false;
		cb.source_id = g_idle_add(timer_cb_s, &cb);
		return cb.source_id;
	}
	
	
	/*private*/ void remove_fd(uintptr_t fd, bool remove_source)
	{
		for (auto pair : fdinfo)
		{
			if (pair.key == fd)
			{
				if (remove_source) g_source_remove(pair.value.source_id);
#ifdef ARLIB_TESTRUNNER
				free(pair.value.valgrind_dummy);
#endif
				fdinfo.remove(pair.key);
				return;
			}
		}
	}
	/*private*/ void remove_timer(uintptr_t id, bool remove_source)
	{
		if (!id) return;
		for (size_t i=0;i<timerinfo.size();i++)
		{
			timer_cb& cb = timerinfo[i];
			if (cb.source_id == id)
			{
				if (cb.inside)
				{
					cb.deleted = true;
					return;
				}
				
				if (remove_source && !cb.finished) g_source_remove(cb.source_id);
#ifdef ARLIB_TESTRUNNER
				free(cb.valgrind_dummy);
#endif
				timerinfo.remove(i);
				return;
			}
		}
		abort(); // removing nonexistent events is bad news
	}
	void raw_timer_remove(uintptr_t id) override
	{
		remove_timer(id, true);
	}
	
	
#ifdef ARLIB_THREAD
	void prepare_submit() override
	{
		if (submit_fds[0] >= 0) return;
		//leak these fds - this object only goes away on process exit
		if (pipe2(submit_fds, O_CLOEXEC) < 0) abort();
		this->set_fd(submit_fds[0], bind_this(&runloop_gtk::submit_cb), NULL);
	}
	void submit(function<void()>&& cb) override
	{
		//full pipe should be impossible
		static_assert(sizeof(cb) <= PIPE_BUF);
		if (write(submit_fds[1], &cb, sizeof(cb)) != sizeof(cb)) abort();
		memset(&cb, 0, sizeof(cb));
	}
	/*private*/ void submit_cb(uintptr_t)
	{
		function<void()> cb;
		//we know the write pushed a complete one of those, we can assume we can read it out
		if (read(submit_fds[0], &cb, sizeof(cb)) != sizeof(cb)) abort();
		cb();
	}
#endif
	
	
	void enter() override { gtk_main(); need_exit_sync = false; }
	void exit() override { gtk_main_quit(); }
	void step(bool wait) override
	{
		// TODO: investigate which of these comments are still accurate
		// especially if both trues need to be true in that branch, and if it's possible to get stuck between them
		
		if (wait && !gtk_events_pending())
		{
			//workaround for Gtk thinking the program is lagging if we only call this every 16ms
			//we're busy waiting in non-Gtk syscalls, waiting less costs us nothing
			
			//yes, I need to call this twice if there are no events; first one is nonblocking for some reason,
			// even though I ask for blocking, and if I only call this once, the 'first one?' flag is reset by
			// something in gl.swapBuffers
			//I suspect Gtk is trying to be "smart" about something, but it just ends up being counterproductive
			//and I can't find which part of the source code describes such absurd behavior,
			// nor any sensible way to debug it, track it down, and find a less absurd workaround
			
			gtk_main_iteration_do(true); // TODO: investigate whether it's possible to get stuck between those,
			gtk_main_iteration_do(true); // and which need to be blocking
		}
		else
		{
			//if there are events, we have everything we're suppose to wait for; dispatch them and report we're done
			//however, we must (again) call this twice, to disable mouse motion compression
			gtk_main_iteration_do(false);
			gtk_main_iteration_do(false);
		}
		
		need_exit_sync = true;
	}
	
	~runloop_gtk()
	{
		abort(); // illegal to delete this object
	}
	
	void finalize()
	{
		if (need_exit_sync)
		{
			//GTK BUG WORKAROUND:
			
			//Gtk docs say "It's OK to use the GLib main loop directly instead of gtk_main(), though it involves slightly more typing"
			//but that is a lie. gtk_main() does a few deinitialization tasks when it leaves
			//for example, it saves the clipboard to X11-server-side storage; without that, the clipboard is wiped on app exit
			//this is also done in GtkApplication::shutdown, so it's my choice which of those to call
			//gtk_main is easier, but it potentially dispatches events to objects that don't exist anymore,
			// and doesn't save clipboard if gtk_main is already running (i.e. someone called exit() in an event handler),
			// so GtkApplication it is - I'll have to create one just so it can be shut down
			
			//but this is still a terrible hack. a much better solution would be creating gtk_deinit()
			// and moving these shutdown actions there, or having Gtk put this in its own atexit handler
			
#ifndef ARLIB_OPT // keep a pointer in debug mode, so Valgrind doesn't flag it as a leak
			static
#endif
			GApplication* app;
			app = (GApplication*)gtk_application_new(NULL, G_APPLICATION_FLAGS_NONE);
			g_application_register(app, NULL, NULL);
			G_APPLICATION_GET_CLASS(app)->shutdown(app);
			// then leak the object. program's exiting, let kernel deal with cleanup
			// fair chance the object's halfway deinitialized and doesn't know how to finish destruction, anyways
		}
	}
	
#ifdef ARLIB_TESTRUNNER
	void assert_empty() override
	{
		bool is_empty = true;
	again:
		for (auto& pair : fdinfo)
		{
#ifdef ARLIB_THREAD
			if ((int)pair.key == submit_fds[0]) // leave this fd in the runloop
				continue;
#endif
			if ((int)pair.key == process::_sigchld_fd_runloop_only())
				continue;
			
			if (RUNNING_ON_VALGRIND)
				free(pair.value.valgrind_dummy); // intentional double free, to make valgrind print a stack trace of the malloc
			else
				printf("ERROR: fd %lu left in runloop\n", pair.key);
			remove_fd(pair.key, true);
			is_empty = false;
			goto again;
		}
		while (timerinfo.size())
		{
			if (RUNNING_ON_VALGRIND)
				free(timerinfo[0].valgrind_dummy); // intentional double free
			else
				printf("ERROR: timer left in runloop\n");
			remove_timer(timerinfo[0].source_id, true);
			is_empty = false;
		}
		assert(is_empty);
	}
#endif
};
}

runloop* runloop::global()
{
	static runloop_gtk* ret = NULL;
	if (!ret)
	{
		ret = new runloop_gtk();
		//If more than one atexit function has been specified by different calls to this function,
		// they are all executed in reverse order as a stack (i.e. the last function specified is
		// the first to be executed at exit).
		//so this one most likely gets called early, while gtk is still operational
		atexit([](){ ret->finalize(); });
	}
#ifdef ARLIB_TESTRUNNER
	static runloop* ret2 = NULL;
	if (!ret2) ret2 = runloop_wrap_blocktest(ret);
	return ret2;
#else
	return ret;
#endif
}



#if 0
char * window_get_absolute_path(const char * basepath, const char * path, bool allow_up)
{
	if (!path) return NULL;
	
	GFile* file;
	const char * pathend = NULL; // gcc bug: this initialization does nothing, but gcc whines
	if (basepath)
	{
		pathend = strrchr(basepath, '/');
		gchar * basepath_dir = g_strndup(basepath, pathend+1-basepath);
		file = g_file_new_for_commandline_arg_and_cwd(path, basepath_dir);
		g_free(basepath_dir);
	}
	else
	{
		if (!allow_up) return NULL;
		//not sure if gvfs URIs are absolute or not, so if absolute, let's use the one that works for both
		//if not absolute, demand it's an URI
		//per glib/gconvert.c g_filename_from_uri, file:// URIs are absolute
		if (g_path_is_absolute(path)) file = g_file_new_for_commandline_arg(path);
		else file = g_file_new_for_uri(path);
	}
	
	gchar * ret;
	if (g_file_is_native(file)) ret = g_file_get_path(file);
	else ret = g_file_get_uri(file);
	g_object_unref(file);
	
	if (!ret) return NULL;
	
	if (!allow_up && strncmp(basepath, ret, pathend+1-basepath) != 0)
	{
		g_free(ret);
		return NULL;
	}
	
	return (char*)mem_from_g_alloc(ret, 0);
}

char * window_get_native_path(const char * path)
{
	if (!path) return NULL;
	GFile* file=g_file_new_for_commandline_arg(path);
	gchar * ret=g_file_get_path(file);
	g_object_unref(file);
	if (!ret) return NULL;
	return (char*)mem_from_g_alloc(ret, 0);
}


void* file_find_create(const char * path)
{
	if (!path) return NULL;
	GFile* parent=g_file_new_for_path(path);
	GFileEnumerator* children=g_file_enumerate_children(parent, G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
	                                                    G_FILE_QUERY_INFO_NONE, NULL, NULL);
	g_object_unref(parent);
	return children;
}

bool file_find_next(void* find, char * * path, bool * isdir)
{
	if (!find) return false;
	GFileEnumerator* children=(GFileEnumerator*)find;
	
	GFileInfo* child=g_file_enumerator_next_file(children, NULL, NULL);
	if (!child) return false;
	
	*path=strdup(g_file_info_get_name(child));
	*isdir=(g_file_info_get_file_type(child)==G_FILE_TYPE_DIRECTORY);
	g_object_unref(child);
	return true;
}

void file_find_close(void* find)
{
	if (!find) return;
	g_object_unref((GFileEnumerator*)find);
}
#endif

test("file::GVFS","","")
{
	test_skip("kinda slow");
	file f("https://floating.muncher.se/arlib/latesize.php");
	// <?php
	// echo "a";
	// flush();
	// echo "b";
	assert_eq(f.size(), 0);
	assert_eq(f.readallt(), "ab");
}
#endif
