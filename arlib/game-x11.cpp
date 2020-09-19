#if defined(ARLIB_GAME) && defined(ARGUIPROT_X11)
#include "game.h"
#include "runloop.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <unistd.h>

struct window_x11_info window_x11 = {};

class gameview_x11;
// there is no way to associate a userdata with an X window, GDK uses a global hashmap of all known X window IDs
// (gdk/x11/gdkxid.c, gdk_x11_window_lookup_for_display)
// for me, there will only be one, so I'll stick to something simpler than a hashmap
gameview_x11* g_gameview = NULL;

class gameview_x11 : public gameview {
public:
Display* display;
Window window;

bool focus = true; // oddly enough, windows default to focused on x11
bool stopped = false;
bool got_event;

#define X11_ATOMS \
	ATOM(WM_PROTOCOLS) \
	ATOM(WM_DELETE_WINDOW) \
	ATOM(_NET_WM_PING) \
	ATOM(_NET_WM_PID) \
	ATOM(_NET_WM_NAME) \
	ATOM(UTF8_STRING) \

struct atoms_t {
#define ATOM(varname) Atom varname;
	X11_ATOMS
#undef ATOM
} atoms;


static bool process_events()
{
	bool ret = false;
	while (XPending(window_x11.display))
	{
		ret = true;
		
		XEvent ev;
		XNextEvent(window_x11.display, &ev);
		if (g_gameview && ev.xany.window == g_gameview->window)
			g_gameview->process_event(&ev);
	}
	return ret;
}

static void global_init()
{
	if (window_x11.display) return;
	
	window_x11.display = XOpenDisplay(NULL);
	if (!window_x11.display)
	{
		perror("XOpenDisplay");
		exit(1);
	}
	window_x11.screen = DefaultScreen(window_x11.display);
	
	runloop::global()->set_fd(ConnectionNumber(window_x11.display), [](uintptr_t){ process_events(); });
	
	Bool ignore;
	XkbSetDetectableAutoRepeat(window_x11.display, true, &ignore);
	
	// this may be useful on Solaris, but I can't find any trace of it doing anything anywhere else
	// https://bugzilla.gnome.org/show_bug.cgi?id=76681
	//if (!XAddConnectionWatch(display,
	//	[](Display* display, char* g_userdata, int fd, Bool opening, char* * l_userdata) {
	//		printf("connwatch: %d %d\n",fd,opening);
	//		if (opening) runloop::global()->set_fd(fd, [](uintptr_t fd){ XProcessInternalConnection(display, fd); });
	//		else runloop::global()->set_fd(fd, NULL);
	//	}, NULL)) abort();
}

static uintptr_t root_parent()
{
	global_init();
	return XRootWindow(window_x11.display, window_x11.screen);
}

gameview_x11(Window window, uint32_t width, uint32_t height, cstring title) : window(window)
{
	g_gameview = this;
	display = window_x11.display;
	
	XSelectInput(display, window, ExposureMask|FocusChangeMask|KeyPressMask|KeyReleaseMask|
	                              PointerMotionMask|ButtonPressMask|ButtonReleaseMask|LeaveWindowMask);
	
	XSizeHints hints;
	hints.flags = PSize|PMinSize|PMaxSize;
	hints.width = hints.min_width = hints.max_width = width;
	hints.height = hints.min_height = hints.max_height = height;
	XSetWMNormalHints(display, window, &hints);
	
	static const char * const atom_names[] = {
#define ATOM(name) #name,
		X11_ATOMS
#undef ATOM
	};
	XInternAtoms(display, (char**)atom_names, ARRAY_SIZE(atom_names), false, (Atom*)&atoms);
	
	Atom protocols[2] = { atoms.WM_DELETE_WINDOW, atoms._NET_WM_PING, };
	XSetWMProtocols(display, window, protocols, ARRAY_SIZE(protocols));
	
	long pid = getpid(); // gotta love how X11 insists that long is 32 bits. the C builtin types are ridiculous
	XChangeProperty(display, window, atoms._NET_WM_PID, XA_CARDINAL, 32, PropModeReplace, (uint8_t*)&pid, 1);
	
	XChangeProperty(display, window, atoms._NET_WM_NAME, atoms.UTF8_STRING, 8, PropModeReplace, title.bytes().ptr(), title.length());
	
	XMapWindow(window_x11.display, window);
	
	calc_keyboard_map();
}

function<void()> cb_exit = [this](){ stop(); };
/*public*/ bool running() { return !stopped; }
/*public*/ void stop() { stopped = true; }
/*public*/ void exit_cb(function<void()> cb) { cb_exit = cb; }

/*public*/ bool focused() { return focus; }

void process_event(XEvent* ev)
{
	got_event = true;
	switch (ev->type)
	{
		case Expose: break; // just set got_event, nothing else needed
		case FocusIn:
			focus = true;
			set_all_keys();
			break;
		case FocusOut:
			focus = false;
			break;
		case KeyPress:
		case KeyRelease:
			set_key(ev->xkey.keycode, (ev->type == KeyPress));
			break;
		case ButtonPress:
		case ButtonRelease:
			static_assert(Button1Mask == 256); // guaranteed by X11 spec
			static_assert(Button2Mask == 512);
			static_assert(Button3Mask == 1024);
			if (ev->xbutton.button >= 1 && ev->xbutton.button <= 3)
				send_mouse(ev->xbutton.x, ev->xbutton.y, ev->xbutton.state^(Button1Mask >> 1 << ev->xbutton.button));
			break;
		case MotionNotify:
			send_mouse(ev->xmotion.x, ev->xmotion.y, ev->xmotion.state);
			break;
		case LeaveNotify:
			if (!(ev->xcrossing.state & (Button1Mask | Button2Mask | Button3Mask))) // if a button is held, we'll get a MotionNotify soon
				send_mouse(-0x8000000, -0x8000000, ev->xcrossing.state);
			break;
		case ClientMessage:
			if (ev->xclient.message_type == atoms.WM_PROTOCOLS && ev->xclient.format == 32)
			{
				Atom subtype = ev->xclient.data.l[0];
				if (subtype == atoms.WM_DELETE_WINDOW)
				{
					cb_exit();
				}
				else if (subtype == atoms._NET_WM_PING)
				{
					ev->xclient.window = XRootWindow(display, window_x11.screen);
					XSendEvent(display, ev->xclient.window, false, SubstructureRedirectMask|SubstructureNotifyMask, ev);
				}
#ifndef ARLIB_OPT
				else puts("AAAAA unknown WM_PROTOCOLS");
#endif
			}
#ifndef ARLIB_OPT
			else puts("AAAAA unknown ClientMessage");
#endif
			break;
#ifndef ARLIB_OPT
		default:
			printf("AAAAA unknown event %d\n",ev->type);
			break;
#endif
	}
}

~gameview_x11() {} // nothing, the only resources we acquire are a Window (aropengl deletes that) and a Display* (it's global anyways)



uint16_t scan_to_key[256]; // 256 is hardcoded in, among others, XQueryKeymap; it's safe to rely on
uint8_t current_scans[32];

void calc_keyboard_map()
{
	struct {
		uint8_t arlib; // there's a byte of padding, but there's no real way to remove it without making a mess
		uint16_t x11; // and such a thing would save 138 bytes at most
	} static const keymap[] = {
		{ K_BACKSPACE, XK_BackSpace }, { K_TAB, XK_Tab }, { K_CLEAR, XK_Clear }, { K_RETURN, XK_Return }, { K_PAUSE, XK_Pause },
		{ K_ESCAPE, XK_Escape }, { K_SPACE, XK_space }, { K_EXCLAIM, XK_exclam }, { K_QUOTEDBL, XK_quotedbl },
		{ K_HASH, XK_numbersign }, { K_DOLLAR, XK_dollar }, { K_AMPERSAND, XK_ampersand }, { K_QUOTE, XK_apostrophe },
		{ K_LEFTPAREN, XK_parenleft }, { K_RIGHTPAREN, XK_parenright }, { K_ASTERISK, XK_asterisk }, { K_PLUS, XK_plus },
		{ K_COMMA, XK_comma }, { K_MINUS, XK_minus }, { K_PERIOD, XK_period }, { K_SLASH, XK_slash },
		
		{ K_0, XK_0 }, { K_1, XK_1 }, { K_2, XK_2 }, { K_3, XK_3 }, { K_4, XK_4 },
		{ K_5, XK_5 }, { K_6, XK_6 }, { K_7, XK_7 }, { K_8, XK_8 }, { K_9, XK_9 },
		
		{ K_COLON, XK_colon }, { K_SEMICOLON, XK_semicolon }, { K_LESS, XK_less }, { K_EQUALS, XK_equal }, { K_GREATER, XK_greater },
		{ K_QUESTION, XK_question }, { K_AT, XK_at }, { K_LEFTBRACKET, XK_bracketleft }, { K_BACKSLASH, XK_backslash },
		{ K_RIGHTBRACKET, XK_bracketright }, { K_CARET, XK_asciicircum }, { K_UNDERSCORE, XK_underscore },
		{ K_BACKQUOTE, XK_grave }, { K_BACKQUOTE, XK_dead_acute },
		{ K_a, XK_a }, { K_b, XK_b }, { K_c, XK_c }, { K_d, XK_d }, { K_e, XK_e }, { K_f, XK_f }, { K_g, XK_g }, { K_h, XK_h },
		{ K_i, XK_i }, { K_j, XK_j }, { K_k, XK_k }, { K_l, XK_l }, { K_m, XK_m }, { K_n, XK_n }, { K_o, XK_o }, { K_p, XK_p },
		{ K_q, XK_q }, { K_r, XK_r }, { K_s, XK_s }, { K_t, XK_t }, { K_u, XK_u }, { K_v, XK_v }, { K_w, XK_w }, { K_x, XK_x },
		{ K_y, XK_y }, { K_z, XK_z }, { K_DELETE, XK_Delete },
		
		{ K_KP0, XK_KP_0 }, { K_KP1, XK_KP_1 }, { K_KP2, XK_KP_2 }, { K_KP3, XK_KP_3 }, { K_KP4, XK_KP_4 },
		{ K_KP5, XK_KP_5 }, { K_KP6, XK_KP_6 }, { K_KP7, XK_KP_7 }, { K_KP8, XK_KP_8 }, { K_KP9, XK_KP_9 },
		{ K_KP_PERIOD, XK_KP_Separator }, { K_KP_DIVIDE, XK_KP_Divide }, { K_KP_MULTIPLY, XK_KP_Multiply },
		{ K_KP_MINUS, XK_KP_Subtract }, { K_KP_PLUS, XK_KP_Add }, { K_KP_ENTER, XK_KP_Enter }, { K_KP_EQUALS, XK_KP_Equal },
		
		{ K_UP, XK_Up }, { K_DOWN, XK_Down }, { K_RIGHT, XK_Right }, { K_LEFT, XK_Left },
		{ K_INSERT, XK_Insert }, { K_HOME, XK_Home }, { K_END, XK_End }, { K_PAGEUP, XK_Page_Up }, { K_PAGEDOWN, XK_Page_Down },
		
		{ K_F1, XK_F1 }, { K_F2, XK_F2 }, { K_F3, XK_F3 }, { K_F4, XK_F4 }, { K_F5, XK_F5 },
		{ K_F6, XK_F6 }, { K_F7, XK_F7 }, { K_F8, XK_F8 }, { K_F9, XK_F9 }, { K_F10, XK_F10 },
		{ K_F11, XK_F11 }, { K_F12, XK_F12 }, { K_F13, XK_F13 }, { K_F14, XK_F14 }, { K_F15, XK_F15 },
		
		{ K_NUMLOCK, XK_Num_Lock }, { K_CAPSLOCK, XK_Caps_Lock }, { K_SCROLLOCK, XK_Scroll_Lock },
		{ K_RSHIFT, XK_Shift_R }, { K_LSHIFT, XK_Shift_L }, { K_RCTRL, XK_Control_R }, { K_LCTRL, XK_Control_L },
		{ K_RALT, XK_Alt_R }, { K_LALT, XK_Alt_L }, { K_RMETA, XK_Meta_R }, { K_LMETA, XK_Meta_L },
		{ K_LSUPER, XK_Super_L }, { K_RSUPER, XK_Super_R }, { K_MODE, XK_Mode_switch }, { K_COMPOSE, XK_Multi_key },
		
		{ K_HELP, XK_Help }, { K_PRINT, XK_Print }, { K_SYSREQ, XK_Sys_Req }, { K_BREAK, XK_Break }, { K_MENU, XK_Menu },
		/*{ K_POWER, x },*/ { K_EURO, XK_EuroSign }, { K_UNDO, XK_Undo }, /*{ K_OEM_102, x },*/
		
		{ K_RALT, XK_ISO_Level3_Shift/*AltGr*/ }, { K_CARET, XK_dead_circumflex }, { K_KP_PERIOD, XK_KP_Decimal },
	};
	
	int minkc;
	int maxkc;
	XDisplayKeycodes(display, &minkc, &maxkc);
	int sym_per_code;
	KeySym* sym = XGetKeyboardMapping(display, minkc, maxkc-minkc+1, &sym_per_code);
	
	// run loop backwards, so the unshifted state is the one that stays in the array
	memset(scan_to_key, 0, sizeof(scan_to_key));
	size_t i = sym_per_code*(maxkc-minkc);
	while (i--)
	{
		if (sym[i] == NoSymbol) continue; // most of them point nowhere, ignore inner loop
		if (sym[i]&~0xFFFF) continue; // ignore extended (non-0x0000xxxx) keysyms too, there are none in the above table
		                              // if one shows up, gcc will throw a warning about constant truncation
		for (size_t j : range(ARRAY_SIZE(keymap)))
		{
			if (keymap[j].x11 == sym[i])
				scan_to_key[minkc + i/sym_per_code] = keymap[j].arlib;
		}
	}
	
	XFree(sym);
}

function<void(int scancode, key_t k, bool down)> cb_keys;

/*public*/ void keys_cb(function<void(int scancode, key_t k, bool down)> cb)
{
	cb_keys = cb;
	memset(current_scans, 0, sizeof(current_scans));
	set_all_keys();
}

void set_key(uint8_t scancode, bool state)
{
	int byte = scancode>>3;
	int bit = scancode&7;
	
	bool prevstate = (current_scans[byte] & (1<<bit));
	if (state == prevstate) return;
	
	current_scans[byte] = (current_scans[byte] & ~(1<<bit)) | (state<<bit);
	cb_keys(scancode, (key_t)scan_to_key[scancode], state);
}

void set_all_keys()
{
	uint8_t state[32];
	XQueryKeymap(display, (char*)state);
	for (int i=0;i<256/8;i++)
	{
		if (current_scans[i] == state[i]) continue;
		for (int bit=0;bit<8;bit++)
		{
			set_key((i<<3)|bit, state[i] & (1<<bit));
		}
	}
}



function<void(int x, int y, uint8_t buttons)> cb_mouse;

/*public*/ void mouse_cb(function<void(int x, int y, uint8_t buttons)> cb)
{
	cb_mouse = cb;
}

void send_mouse(int x, int y, uint32_t state)
{
	static_assert(Button1Mask == 256); // guaranteed by X11 spec
	static_assert(Button2Mask == 512);
	static_assert(Button3Mask == 1024);
	cb_mouse(x, y, (0x76325410>>((state>>6)&0x1c))&7); // funky math to swap middle and right buttons
}



// sending a frame to OpenGL makes X server reply, which makes runloop break and render next frame
// TODO: delete once runloop rewrite is done
/*public*/ void tmp_step(bool wait)
{
	got_event = false;
	process_events(); // in case someone else (for example ctx-x11.cpp) did synchronous X calls behind our back
	runloop::global()->step(false);
	while (wait && !got_event)
		runloop::global()->step(true);
}

};

gameview* gameview::create_finish(uintptr_t window, uint32_t width, uint32_t height, cstring windowtitle) {
	return new gameview_x11(window, width, height, windowtitle); }
uintptr_t gameview::root_parent() { return gameview_x11::root_parent(); }

#endif
