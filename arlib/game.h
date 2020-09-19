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
// there is no guarantee that the keys exist on anyone's keyboard
enum key_t : uint8_t {
	K_UNKNOWN        = 0,   K_FIRST          = 0,
	K_BACKSPACE      = 8,   K_TAB            = 9,   K_CLEAR          = 12,  K_RETURN         = 13,  K_PAUSE          = 19,
	K_ESCAPE         = 27,  K_SPACE          = ' ', K_EXCLAIM        = '!', K_QUOTEDBL       = '"', K_HASH           = '#',
	K_DOLLAR         = '$', K_AMPERSAND      = '&', K_QUOTE          = '\'',K_LEFTPAREN      = '(', K_RIGHTPAREN     = ')',
	K_ASTERISK       = '*', K_PLUS           = '+', K_COMMA          = ',', K_MINUS          = '-', K_PERIOD         = '.',
	K_SLASH          = '/', K_0              = '0', K_1              = '1', K_2              = '2', K_3              = '3',
	K_4              = '4', K_5              = '5', K_6              = '6', K_7              = '7', K_8              = '8',
	K_9              = '9', K_COLON          = ':', K_SEMICOLON      = ';', K_LESS           = '<', K_EQUALS         = '=',
	K_GREATER        = '>', K_QUESTION       = '?', K_AT             = '@', K_LEFTBRACKET    = '[', K_BACKSLASH      = '\\',
	K_RIGHTBRACKET   = ']', K_CARET          = '^', K_UNDERSCORE     = '_', K_BACKQUOTE      = '`', K_a              = 'a',
	K_b              = 'b', K_c              = 'c', K_d              = 'd', K_e              = 'e', K_f              = 'f',
	K_g              = 'g', K_h              = 'h', K_i              = 'i', K_j              = 'j', K_k              = 'k',
	K_l              = 'l', K_m              = 'm', K_n              = 'n', K_o              = 'o', K_p              = 'p',
	K_q              = 'q', K_r              = 'r', K_s              = 's', K_t              = 't', K_u              = 'u',
	K_v              = 'v', K_w              = 'w', K_x              = 'x', K_y              = 'y', K_z              = 'z',
	K_LEFTBRACE      = '{', K_BAR            = '|', K_RIGHTBRACE     = '}', K_TILDE          = '~', K_DELETE         = 127,
	
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
