#ifdef ARLIB_GAME
#include "global.h"
#include "string.h"
#include "opengl/aropengl.h"

// You can only have one gameview per process.
class gameview : nocopy {
#ifndef _WIN32
static uintptr_t root_parent();
static gameview* create_finish(uintptr_t window, uint32_t width, uint32_t height, cstring windowtitle);
#else
static gameview* create(uint32_t width, uint32_t height, cstring windowtitle, uintptr_t* parent, uintptr_t** child);
#endif

public:
#ifndef _WIN32
template<typename Taropengl>
static gameview* create(Taropengl& gl, uint32_t width, uint32_t height, uint32_t glflags, cstring windowtitle)
{
	uintptr_t window;
	if (!gl.create(width, height, root_parent(), &window, glflags)) return NULL;
	return create_finish(window, width, height, windowtitle);
}
#else
template<typename Taropengl>
static gameview* create(Taropengl& gl, uint32_t width, uint32_t height, uint32_t glflags, cstring windowtitle)
{
	// GL only works on child windows on windows
	uintptr_t parent;
	uintptr_t* child;
	gameview* ret = create(width, height, windowtitle, &parent, &child);
	if (!gl.create(width, height, parent, child, glflags)) { delete ret; return NULL; }
	return ret;
}
#endif

virtual void exit_cb(function<void()> cb) = 0; // Called when the window is closed. Defaults to stop().
virtual bool running() = 0; // Returns false if the window was closed.
virtual void stop() = 0; // Makes running() return false.

virtual bool focused() = 0; // Returns false if something else is focused.

virtual ~gameview() {}


virtual void tmp_step(bool wait) = 0;


// same as libretro.h retro_key, except values 256-383 are shifted down to 128-255 (libretro defines nothing in 128-255)
enum key_t : uint8_t {
	K_UNKNOWN        = 0,   K_FIRST          = 0,
	K_BACKSPACE      = 8,   K_TAB            = 9,   K_CLEAR          = 12,  K_RETURN         = 13,  K_PAUSE          = 19,
	K_ESCAPE         = 27,  K_SPACE          = 32,  K_EXCLAIM        = 33,  K_QUOTEDBL       = 34,  K_HASH           = 35,
	K_DOLLAR         = 36,  K_AMPERSAND      = 38,  K_QUOTE          = 39,  K_LEFTPAREN      = 40,  K_RIGHTPAREN     = 41,
	K_ASTERISK       = 42,  K_PLUS           = 43,  K_COMMA          = 44,  K_MINUS          = 45,  K_PERIOD         = 46,
	K_SLASH          = 47,  K_0              = 48,  K_1              = 49,  K_2              = 50,  K_3              = 51,
	K_4              = 52,  K_5              = 53,  K_6              = 54,  K_7              = 55,  K_8              = 56,
	K_9              = 57,  K_COLON          = 58,  K_SEMICOLON      = 59,  K_LESS           = 60,  K_EQUALS         = 61,
	K_GREATER        = 62,  K_QUESTION       = 63,  K_AT             = 64,  K_LEFTBRACKET    = 91,  K_BACKSLASH      = 92,
	K_RIGHTBRACKET   = 93,  K_CARET          = 94,  K_UNDERSCORE     = 95,  K_BACKQUOTE      = 96,  K_a              = 97,
	K_b              = 98,  K_c              = 99,  K_d              = 100, K_e              = 101, K_f              = 102,
	K_g              = 103, K_h              = 104, K_i              = 105, K_j              = 106, K_k              = 107,
	K_l              = 108, K_m              = 109, K_n              = 110, K_o              = 111, K_p              = 112,
	K_q              = 113, K_r              = 114, K_s              = 115, K_t              = 116, K_u              = 117,
	K_v              = 118, K_w              = 119, K_x              = 120, K_y              = 121, K_z              = 122,
	K_LEFTBRACE      = 123, K_BAR            = 124, K_RIGHTBRACE     = 125, K_TILDE          = 126, K_DELETE         = 127,
	
	K_KP0            = 128, K_KP1            = 129, K_KP2            = 130, K_KP3            = 131, K_KP4            = 132,
	K_KP5            = 133, K_KP6            = 134, K_KP7            = 135, K_KP8            = 136, K_KP9            = 137,
	K_KP_PERIOD      = 138, K_KP_DIVIDE      = 139, K_KP_MULTIPLY    = 140, K_KP_MINUS       = 141, K_KP_PLUS        = 142,
	K_KP_ENTER       = 143, K_KP_EQUALS      = 144,
	
	K_UP             = 145, K_DOWN           = 146, K_RIGHT          = 147, K_LEFT           = 148, K_INSERT         = 149,
	K_HOME           = 150, K_END            = 151, K_PAGEUP         = 152, K_PAGEDOWN       = 153,
	
	K_F1             = 154, K_F2             = 155, K_F3             = 156, K_F4             = 157, K_F5             = 158,
	K_F6             = 159, K_F7             = 160, K_F8             = 161, K_F9             = 162, K_F10            = 163,
	K_F11            = 164, K_F12            = 165, K_F13            = 166, K_F14            = 167, K_F15            = 168,
	
	K_NUMLOCK        = 172, K_CAPSLOCK       = 173, K_SCROLLOCK      = 174, K_RSHIFT         = 175, K_LSHIFT         = 176,
	K_RCTRL          = 177, K_LCTRL          = 178, K_RALT           = 179, K_LALT           = 180, K_RMETA          = 181,
	K_LMETA          = 182, K_LSUPER         = 183, K_RSUPER         = 184, K_MODE           = 185, K_COMPOSE        = 186,
	
	K_HELP           = 187, K_PRINT          = 188, K_SYSREQ         = 189, K_BREAK          = 190, K_MENU           = 191,
	K_POWER          = 192, K_EURO           = 193, K_UNDO           = 194, K_OEM_102        = 195,
	
	K_LAST,
};

// If the system can't distinguish between different keyboards, keyboard ID is -1.
// scancode is guaranteed to exist, but k may be K_UNKNOWN if the scancode doesn't correspond to anything.
// Key repeat events will not show up here.
virtual void keys_cb(function<void(int scancode, key_t key, bool down)> cb) = 0;


// If the user started a drag inside the window, but mouse is currently outside, the arguments may be outside the window.
// If the mouse is outside the window and nothing is held, the arguments will be -0x80000000, -0x80000000, 0.
// Buttons: 0x01-left, 0x02-right, 0x04-middle
virtual void mouse_cb(function<void(int x, int y, uint8_t buttons)> cb) = 0;

// TODO:
// - mouse wheel
// - gamepad
// - resize window
// - fullscreen
// - sound
};

#ifdef ARGUIPROT_X11
struct _XDisplay;
typedef struct _XDisplay Display;
struct window_x11_info {
	Display* display;
	int screen;
};
extern struct window_x11_info window_x11;
#endif
#endif
