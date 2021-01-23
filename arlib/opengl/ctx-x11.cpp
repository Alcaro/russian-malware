#ifdef ARGUIPROT_X11
#include "aropengl.h"
#ifdef ARGUI_NONE
#include "../game.h"
#endif

//I could use GtkGLArea, its GTK integration may yield slightly better results.
//but probably not much. and it's quite different from GLX and WGL:
//- it's a GtkWidget rather than X11 Window, which would make the widget_viewport implementation somewhat different
//  maybe make it a GtkGrid with one or zero children?
//- not only does it have an equivalent of gl.outputFramebuffer(), but it doesn't even export the actual framebuffer name -
//    only gtk_gl_area_attach_buffers(), which attaches it (and does some seemingly-irrelevant configuration)
//  could be worked around by going behind Gtk, via glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &GLint)
//- it very strongly recommends drawing only from some weird "render" signal callback, rather than where I want
//  it's unclear if it's possible to ignore that, it's unclear if it'll work in the future, and
//    it's unclear how that interacts with vblank
//conclusion: it's intended for animations and other simple stuff, or maybe programs built from the ground up around gtk.
//  gaming is uncomfortable. it's too different. it's not worth the effort.

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <dlfcn.h>

namespace {

#define GLX_SYMS() \
	/* GLX 1.0 */ \
	GLX_SYM(funcptr, GetProcAddress, (const GLubyte * procName)) \
	GLX_SYM(void, SwapBuffers, (Display* dpy, GLXDrawable drawable)) \
	GLX_SYM(Bool, MakeCurrent, (Display* dpy, GLXDrawable drawable, GLXContext ctx)) \
	GLX_SYM(Bool, QueryVersion, (Display* dpy, int* major, int* minor)) \
	GLX_SYM(GLXContext, GetCurrentContext, ()) \
	/* GLX 1.3 */ \
	GLX_SYM(GLXFBConfig*, ChooseFBConfig, (Display* dpy, int screen, const int * attrib_list, int * nelements)) \
	GLX_SYM(XVisualInfo*, GetVisualFromFBConfig, (Display* dpy, GLXFBConfig config)) \
	GLX_SYM(int, GetFBConfigAttrib, (Display* dpy, GLXFBConfig config, int attribute, int* value)) \
	GLX_SYM(void, DestroyContext, (Display* dpy, GLXContext ctx)) \

struct {
#define GLX_SYM(ret, name, args) ret (*name) args;
	GLX_SYMS()
#undef GLX_SYM
	PFNGLXSWAPINTERVALSGIPROC SwapIntervalSGI;
	void* lib;
} static glx;
#define GLX_SYM(ret, name, args) "glX" #name "\0"
static const char glx_proc_names[] = { GLX_SYMS() };
#undef GLX_SYM

static bool libLoad()
{
	glx.lib = dlopen("libGL.so", RTLD_LAZY);
	if (!glx.lib) return false;
	
	const char * names = glx_proc_names;
	funcptr* functions=(funcptr*)&glx;
	while (*names)
	{
		*functions = (funcptr)dlsym(glx.lib, names);
		if (!*functions) return false;
		
		functions++;
		names += strlen(names)+1;
	}
	
	return true;
}

static void libUnload()
{
	// trying to close this yields segfaults in XCloseDisplay
	//if (glx.lib) dlclose(glx.lib);
}



class aropengl_x11 : public aropengl::context {
public:
	GLXContext ctx;
	Window win;
	bool current;
	
	/*private*/ bool init(uint32_t width, uint32_t height, uintptr_t parent, uintptr_t* window_, uint32_t flags)
	{
		ctx = None;
		win = None;
		current = False;
		
		if (!libLoad()) return false;
		if (glx.GetCurrentContext()) return false;
		
		bool debug = (flags & aropengl::t_debug_context);
		uint32_t version = (flags & 0xFFF);
		
		int glx_major = 0;
		int glx_minor = 0;
		if (!glx.QueryVersion(window_x11.display, &glx_major, &glx_minor)) return false;
		if (glx_major != 1 || glx_minor < 3) return false;
		
		int visual_attribs[] = {
			GLX_X_RENDERABLE,  True,
			GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
			GLX_RED_SIZE,      8,
			GLX_GREEN_SIZE,    8,
			GLX_BLUE_SIZE,     8,
			//GLX_DEPTH_SIZE,    0,
			//GLX_STENCIL_SIZE,  0,
			GLX_DOUBLEBUFFER,  True,
			None
		};
		
		int fbcount;
		GLXFBConfig* fbcs = glx.ChooseFBConfig(window_x11.display, window_x11.screen, visual_attribs, &fbcount);
		if (!fbcs) return false;
		GLXFBConfig fbc = fbcs[0];
		XFree(fbcs);
		
		XVisualInfo* vi = glx.GetVisualFromFBConfig(window_x11.display, fbc);
		
		XSetWindowAttributes swa;
		swa.colormap = XCreateColormap(window_x11.display, parent, vi->visual, AllocNone);
		swa.background_pixmap = None;
		swa.border_pixel      = 0;
		swa.event_mask        = ExposureMask; // to make runloop->step(true) break if someone moves a window on top of ours
		
		win = XCreateWindow(window_x11.display, parent, 0, 0, width, height, 0,
		                    vi->depth, InputOutput, vi->visual, CWBorderPixel|CWColormap|CWEventMask, &swa);
		if (!win) return false;
		
		*window_ = win;
		XFreeColormap(window_x11.display, swa.colormap);
		XFree(vi);
		
		PFNGLXCREATECONTEXTATTRIBSARBPROC glXCreateContextAttribsARB =
			(PFNGLXCREATECONTEXTATTRIBSARBPROC)glx.GetProcAddress((const GLubyte*)"glXCreateContextAttribsARB");
		if (!glXCreateContextAttribsARB) return false;
		
		int context_attribs[] = {
			GLX_CONTEXT_MAJOR_VERSION_ARB, (int)version/100,
			GLX_CONTEXT_MINOR_VERSION_ARB, (int)version/10%10,
			GLX_CONTEXT_FLAGS_ARB, debug ? GLX_CONTEXT_DEBUG_BIT_ARB : 0,
			//GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
			None
		};
		
		ctx = glXCreateContextAttribsARB(window_x11.display, fbc, 0, true, context_attribs);
		if (!ctx) return false;
		makeCurrent(true);
		
		glx.SwapIntervalSGI = (PFNGLXSWAPINTERVALSGIPROC)glx.GetProcAddress((const GLubyte*)"glXSwapIntervalSGI");
		
		return true;
	}
	
	
	
	void makeCurrent(bool make)
	{
		if (make) glx.MakeCurrent(window_x11.display, win, ctx);
		else glx.MakeCurrent(window_x11.display, None, NULL);
	}
	
	funcptr getProcAddress(const char * proc)
	{
		return (funcptr)glx.GetProcAddress((GLubyte*)proc);
	}
	
	void swapInterval(int interval)
	{
		//EXT isn't supported on my glx client/server
		//MESA isn't in my headers
		//that leaves only one
		glx.SwapIntervalSGI(interval);
	}
	
	void swapBuffers()
	{
		glx.SwapBuffers(window_x11.display, win);
	}
	
	void destroy()
	{
		glx.MakeCurrent(window_x11.display, None, NULL);
		glx.DestroyContext(window_x11.display, ctx);
		
		XDestroyWindow(window_x11.display, win);
		libUnload();
	}
	
	~aropengl_x11() { destroy(); }
};

}

aropengl::context* aropengl::context::create(uint32_t width, uint32_t height, uintptr_t parent, uintptr_t* window, uint32_t flags)
{
	aropengl_x11* ret = new aropengl_x11();
	if (ret->init(width, height, parent, window, flags)) return ret;
	
	delete ret;
	return NULL;
}
#endif
