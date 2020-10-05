#if defined(ARLIB_GAME) && defined(_WIN32)
#include "game.h"
#include "runloop.h"
#include <windowsx.h> // GET_X_LPARAM isn't in the usual header

#define WS_BASE WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX // okay microsoft, did I miss anything?
#define WS_RESIZABLE (WS_BASE|WS_MAXIMIZEBOX|WS_THICKFRAME)
#define WS_NONRESIZ (WS_BASE|WS_BORDER)

class gameview_windows : public gameview {
public:

HWND parent;
HWND child;

gameview_windows(uint32_t width, uint32_t height, cstring windowtitle, uintptr_t* pparent, uintptr_t** pchild)
{
	static_assert(sizeof(HWND) == sizeof(uintptr_t));
	
	WNDCLASS wc;
	wc.style = 0;
	wc.lpfnWndProc = DefWindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   (LPCSTR)(void*)s_WindowProc, &wc.hInstance); // gameview shouldn't be used from a dll path, but why not
	wc.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(0));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = "arlib";
	RegisterClass(&wc);
	
	parent = CreateWindow("arlib", windowtitle.c_str().c_str(), WS_NONRESIZ,
	                      CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL, NULL, NULL, NULL);
	SetWindowLongPtr(parent, GWLP_USERDATA, (LONG_PTR)this);
	SetWindowLongPtr(parent, GWLP_WNDPROC, (LONG_PTR)s_WindowProc);
	
	// why are you like this, microsoft
	// if I set a size, it's because I want that size as client area
	// nobody cares about total window size, especially in XP+ where window borders are rounded
	RECT inner;
	RECT outer;
	GetClientRect(parent, &inner);
	GetWindowRect(parent, &outer);
	uint32_t borderwidth = outer.right - outer.left - inner.right;
	uint32_t borderheight = outer.bottom - outer.top - inner.bottom;
	SetWindowPos(parent, NULL, 0, 0, width+borderwidth, height+borderheight,
	             SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOOWNERZORDER|SWP_NOZORDER);
	
	ShowWindow(parent, SW_SHOWNORMAL);
	
	*pparent = (uintptr_t)parent;
	*pchild = (uintptr_t*)&child;
}

bool stopped = false;
function<void()> cb_exit = [this](){ stop(); };
/*public*/ bool running() { return !stopped; }
/*public*/ void stop() { stopped = true; }
/*public*/ void exit_cb(function<void()> cb) { cb_exit = cb; }

/*public*/ bool focused() { return (GetForegroundWindow() == parent); }

~gameview_windows()
{
	DestroyWindow(this->parent);
}



key_t vk_to_key(unsigned vk)
{
	static const uint8_t vk_to_key_raw[256] = { // there are only 256 vks, just hardcode it
		//x0   x1   x2   x3   x4   x5   x6   x7   x8   x9   xA   xB   xC   xD   xE   xF
		0,   0,   0,   0,   0,   0,   0,   0,   0x08,0x09,0,   0,   0x0C,0x0D,0,   0,    // 0x
		0,   0,   0,   0x13,0xAD,0,   0,   0,   0,   0,   0,   0x1B,0,   0,   0,   0,    // 1x
		' ', 0x98,0x99,0x97,0x96,0x94,0x91,0x93,0x92,0,   0,   0,   0xBC,0x95,0x7F,0,    // 2x
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 0,   0,   0,   0,   0,   0,    // 3x
		0,   'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',  // 4x
		'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 0xB7,0xB8,0,   0,   0xC0, // 5x
		0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8C,0x8E,0,   0x8D,0x8A,0x8B, // 6x
		0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0,    // 7x
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    // 8x
		0xAC,0xAE,0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    // 9x
		0xB0,0xAF,0xB2,0xB1,0xB4,0xB3,0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    // Ax
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0x2B,0x2C,0x2D,0x2E,0,    // Bx
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    // Cx
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    // Dx
		0,   0,   0xC3,0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    // Ex
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    // Fx
	};
	return (key_t)vk_to_key_raw[vk];
}
function<void(int scancode, key_t k, bool down)> cb_keys;

#if 0
void calc_keyboard_map()
{
	struct {
		uint8_t arlib;
		uint8_t vk;
	} static const keymap[] = {
		{ K_BACKSPACE, VK_BACK }, { K_TAB, VK_TAB }, { K_CLEAR, VK_CLEAR }, { K_RETURN, VK_RETURN }, { K_PAUSE, VK_PAUSE },
		{ K_ESCAPE, VK_ESCAPE }, { K_SPACE, VK_SPACE }, /*{ K_EXCLAIM,  },*/ /*{ K_QUOTEDBL,  },*/ /*{ K_HASH,  },*/
		/*{ K_DOLLAR,  },*/ /*{ K_AMPERSAND,  },*/ /*{ K_QUOTE,  },*/ /*{ K_LEFTPAREN,  },*/ /*{ K_RIGHTPAREN,  },*/
		/*{ K_ASTERISK,  },*/ { K_PLUS, VK_OEM_PLUS }, { K_COMMA, VK_OEM_COMMA }, { K_MINUS, VK_OEM_MINUS },
		{ K_PERIOD, VK_OEM_PERIOD }, /*{ K_SLASH,  },*/
		{ K_0, '0' }, { K_1, '1' }, { K_2, '2' }, { K_3, '3' }, { K_4, '4' },
		{ K_5, '5' }, { K_6, '6' }, { K_7, '7' }, { K_8, '8' }, { K_9, '9' },
		/*{ K_COLON,  },*/ /*{ K_SEMICOLON,  },*/ /*{ K_LESS,  },*/ /*{ K_EQUALS,  },*/ /*{ K_GREATER,  },*/ /*{ K_QUESTION,  },*/
		/*{ K_AT,  },*/ /*{ K_LEFTBRACKET,  },*/ /*{ K_BACKSLASH,  },*/ /*{ K_RIGHTBRACKET,  },*/ /*{ K_CARET,  },*/
		/*{ K_UNDERSCORE,  },*/ /*{ K_BACKQUOTE,  },*/
		{ K_a, 'A' }, { K_b, 'B' }, { K_c, 'C' }, { K_d, 'D' }, { K_e, 'E' }, { K_f, 'F' }, { K_g, 'G' },
		{ K_h, 'H' }, { K_i, 'I' }, { K_j, 'J' }, { K_k, 'K' }, { K_l, 'L' }, { K_m, 'M' }, { K_n, 'N' },
		{ K_o, 'O' }, { K_p, 'P' }, { K_q, 'Q' }, { K_r, 'R' }, { K_s, 'S' }, { K_t, 'T' }, { K_u, 'U' },
		{ K_v, 'V' }, { K_w, 'W' }, { K_x, 'X' }, { K_y, 'Y' }, { K_z, 'Z' },
		{ K_DELETE, VK_DELETE },
		
		{ K_KP0, VK_NUMPAD0 }, { K_KP1, VK_NUMPAD1 }, { K_KP2, VK_NUMPAD2 }, { K_KP3, VK_NUMPAD3 }, { K_KP4, VK_NUMPAD4 },
		{ K_KP5, VK_NUMPAD5 }, { K_KP6, VK_NUMPAD6 }, { K_KP7, VK_NUMPAD7 }, { K_KP8, VK_NUMPAD8 }, { K_KP9, VK_NUMPAD9 },
		{ K_KP_PERIOD, VK_DECIMAL }, { K_KP_DIVIDE, VK_DIVIDE }, { K_KP_MULTIPLY, VK_MULTIPLY },
		{ K_KP_MINUS, VK_SUBTRACT }, { K_KP_PLUS, VK_ADD }, /*{ K_KP_ENTER,  },*/ /*{ K_KP_EQUALS,  },*/
		
		{ K_UP, VK_UP }, { K_DOWN, VK_DOWN }, { K_RIGHT, VK_RIGHT }, { K_LEFT, VK_LEFT },
		{ K_INSERT, VK_INSERT }, { K_HOME, VK_HOME }, { K_END, VK_END }, { K_PAGEUP, VK_PRIOR }, { K_PAGEDOWN, VK_NEXT },
		
		{ K_F1, VK_F1 },   { K_F2, VK_F2 },   { K_F3, VK_F3 },   { K_F4, VK_F4 },   { K_F5, VK_F5 },
		{ K_F6, VK_F6 },   { K_F7, VK_F7 },   { K_F8, VK_F8 },   { K_F9, VK_F9 },   { K_F10, VK_F10 },
		{ K_F11, VK_F11 }, { K_F12, VK_F12 }, { K_F13, VK_F13 }, { K_F14, VK_F14 }, { K_F15, VK_F15 },
		
		{ K_NUMLOCK, VK_NUMLOCK }, { K_CAPSLOCK, VK_CAPITAL }, { K_SCROLLOCK, VK_SCROLL },
		{ K_RSHIFT, VK_RSHIFT }, { K_LSHIFT, VK_LSHIFT }, { K_RCTRL, VK_RCONTROL }, { K_LCTRL, VK_LCONTROL },
		{ K_RALT, VK_RMENU }, { K_LALT, VK_LMENU }, /*{ K_RMETA,  },*/ /*{ K_LMETA,  },*/
		{ K_LSUPER, VK_LWIN }, { K_RSUPER, VK_RWIN }, /*{ K_MODE,  },*/ /*{ K_COMPOSE,  },*/
		
		/*{ K_HELP,  },*/ { K_PRINT, VK_SNAPSHOT }, /*{ K_SYSREQ,  },*/
		/*{ K_BREAK,  },*/ /*{ K_MENU,  },*/ { K_POWER, VK_SLEEP },
		/*{ K_EURO,  },*/ /*{ K_UNDO,  },*/ { K_OEM_102, VK_OEM_102 },
	};
	
	uint8_t n[256] = {};
	for (size_t i : range(ARRAY_SIZE(keymap))) n[keymap[i].vk] = keymap[i].arlib;
	puts(tostringhex(n));
}
#endif

/*public*/ void keys_cb(function<void(int scancode, key_t k, bool down)> cb)
{
	cb_keys = cb;
}



function<void(int x, int y, uint8_t buttons)> cb_mouse;

/*public*/ void mouse_cb(function<void(int x, int y, uint8_t buttons)> cb)
{
	cb_mouse = cb;
	SetWindowLongPtr(child, GWLP_USERDATA, (LONG_PTR)this); // can't do this in constructor, child is only set by opengl/ctx-windows.cpp
	SetWindowLongPtr(child, GWLP_WNDPROC, (LONG_PTR)s_WindowProc);
}



static LRESULT CALLBACK s_WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	gameview_windows * This=(gameview_windows*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	return This->WindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	// handled by parent window
	case WM_CLOSE: cb_exit(); break;
	case WM_KEYDOWN:
	case WM_KEYUP:
		if ((lParam&0xC0000000) != 0x40000000) // repeat
			cb_keys((lParam>>16)&255, vk_to_key(wParam), !(lParam&0x80000000));
		break;
	
	// handled by child window
	case WM_LBUTTONUP:
	case WM_LBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MOUSEMOVE:
		{
			static_assert(MK_LBUTTON == 1);
			static_assert(MK_RBUTTON == 2);
			static_assert(MK_MBUTTON == 16);
			TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, child, HOVER_DEFAULT };
			TrackMouseEvent(&tme);
			cb_mouse(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), (wParam&(MK_LBUTTON|MK_RBUTTON)) | (wParam&MK_MBUTTON)>>2);
		}
		break;
	case WM_MOUSELEAVE: cb_mouse(-0x8000000, -0x8000000, 0); break;
	
	// handled by both
	default: return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
	return 0;
}



/*public*/ void tmp_step(bool wait) { runloop::global()->step(wait); }

};

gameview* gameview::create(uint32_t width, uint32_t height, cstring windowtitle, uintptr_t* parent, uintptr_t** child) {
	return new gameview_windows(width, height, windowtitle, parent, child); }

void _window_process_events();
void _window_process_events()
{
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

#endif
