#pragma once
#include "../global.h"
#include "../gui/window.h"

#if !defined(_WIN32) || __has_include(<GL/glext.h>)
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include "../deps/gl.h"
#include "../deps/glext.h"
#endif

#ifndef GLAPIENTRY
#ifdef _WIN32
#define GLAPIENTRY APIENTRY
#else
#define GLAPIENTRY
#endif
#endif

//This isn't the object you want. Use aropengl instead, it contains all OpenGL functions as function pointer members, so you can do
//aropengl gl;
//gl.create(widget_viewport*, aropengl::t_ver_3_3);
//gl.ClearColor(0,0,0,0);
//gl.Clear(GL_COLOR_BUFFER_BIT);
//All members on this object are available as well.
class aropengl_base : nocopy {
public:
	enum {
		t_ver_1_0 = 100, t_ver_1_1 = 110, t_ver_1_2 = 120, t_ver_1_3 = 130, t_ver_1_4 = 140, t_ver_1_5 = 150,
		t_ver_2_0 = 200, t_ver_2_1 = 210,
		t_ver_3_0 = 300, t_ver_3_1 = 310, t_ver_3_2 = 320, t_ver_3_3 = 330,
		t_ver_4_0 = 400, t_ver_4_1 = 410, t_ver_4_2 = 420, t_ver_4_3 = 430, t_ver_4_4 = 440, t_ver_4_5 = 450, t_ver_4_6 = 460,
		
		t_opengl_es      = 0x001000, // Probably not supported.
		t_debug_context  = 0x002000, // Requests a debug context. Doesn't actually enable debugging,
		                             // use gl.enableDefaultDebugger or gl.DebugMessageControl/etc.
		//WGL/GLX/etc allow attaching depth/stencil buffers to the output, but it's special cased in all kinds of ways.
		//Therefore, it's not supported here. Create an FBO and stick them there.
		
#ifdef AROPENGL_D3DSYNC
		//Direct3D vsync is an advanced feature that uses WGL_NV_DX_interop and
		//  D3DSWAPEFFECT_FLIPEX to ensure smooth framerate on Windows.
		//(Despite the name, it works on both Intel, AMD and Nvidia.)
		//Advantages:
		//- Less stuttering, especially with DWM enabled (at least on some computers, sometimes vsync is already smooth)
		//Disadvantages:
		//- Requires Windows 7 or newer
		//- May not work on all graphics cards and drivers
		//- Poorly tested driver path, may be slow or buggy (in fact, I believe I found a Nvidia driver bug while creating it)
		//- You may not render to the default framebuffer, 0; you must render to gl.defaultFramebuffer()
		//    (if you don't use framebuffers, you can ignore this; defaultFramebuffer is bound on creation)
		//- You must call gl.notifyResize() whenever the window is resized (whether by the application or the user),
		//    in addition to gl.Viewport/etc (notifyResize is optional if created from an Arlib widget_viewport, gl.Viewport isn't)
		//- Swap intervals other than 0 and 1 are not supported, not even -1
		//- May be slightly slower, especially with vsync off; it does an extra render pass
		//    (this pass contains only a single Direct3DDevice9Ex->StretchRect, so it's fast, but nonzero)
		//The flag is ignored on non-Windows systems.
		//It is safe to use gl.outputFramebuffer and gl.notifyResize on non-d3dsync objects, even outside Windows.
# ifdef _WIN32
		t_direct3d_vsync = 0x004000,
# else
		t_direct3d_vsync = 0,
#  undef AROPENGL_D3DSYNC
# endif
#endif
	};
	
	// TODO: add query VRAM feature, using GLX_MESA_query_renderer
	// there doesn't seem to be any equivalent in WGL; there is one in D3D (DXGI_ADAPTER_DESC), but it requires opening a D3D context
	// TODO: make GL work without the GUI, going for straight GLX and ignoring GTK
	// probably requires rewriting the runloop system, definitely requires XAddConnectionWatch (XCB can't GL, and manually is even worse)
	// probably easier on Windows first, but that definitely requires a runloop rewrite
	// for X, requires deciding who's responsible for XOpenDisplay, and how to pass it to the other side (needed to process other events)
	class context : nocopy {
	public:
		//this is basically the common subset of WGL/GLX/etc
		//you want the outer class, as it offers proper extension/symbol management
		static context* create(uintptr_t parent, uintptr_t* window, uint32_t flags);
		
		virtual void makeCurrent(bool make) = 0; // If false, releases the context. The context is current on creation.
		virtual void swapInterval(int interval) = 0;
		virtual void swapBuffers() = 0;
		virtual funcptr getProcAddress(const char * proc) = 0;
#ifdef AROPENGL_D3DSYNC
		virtual
#endif
		void notifyResize(unsigned int width, unsigned int height) {}
		
#ifdef AROPENGL_D3DSYNC
		virtual
#endif
		GLuint outputFramebuffer() { return 0; }
		
		virtual void destroy() = 0;
		//implementations must ensure the destructor is safe even after having its window destroyed
		//the best method is putting everything into destroy() and having the destructor just call that
		virtual ~context() {}
	};
	
	//These functions exist, public and without symNames/symDest parameters, on aropengl objects.
	//Constructors also exist, taking the same parameters.
protected:
	bool create(context* core, const char * symNames, funcptr* symDest);
	
	bool create(uintptr_t parent, uintptr_t* window, uint32_t flags, const char * symNames, funcptr* symDest)
	{
		return create(context::create(parent, window, flags), symNames, symDest);
	}
	
	bool create(widget_viewport* port, uint32_t flags, const char * symNames, funcptr* symDest)
	{
		uintptr_t newwindow;
		if (!create(port->get_parent(), &newwindow, flags, symNames, symDest)) return false;
		this->port = port;
		port->set_child(newwindow,
		                bind_ptr(&aropengl_base::context::notifyResize, this->core),
		                bind_ptr(&aropengl_base::destroy, this));
		return true;
	}
	
public:
	//Must be called after the window is resized. If created from a viewport, this is configured automatically.
	//Make sure to also call gl.Viewport(0, 0, width, height), or OpenGL will do something ridiculous.
	void notifyResize(unsigned int width, unsigned int height)
	{
		core->notifyResize(width, height);
	}
	
	aropengl_base() { core = NULL; port = NULL; }
	explicit operator bool() { return core != NULL; }
	
	~aropengl_base()
	{
		destroy();
	}
	
	//Arlib usually uses underscores, but since OpenGL doesn't, this object follows suit.
	//To ensure no collisions, Arlib-specific functions start with a lowercase (or are C++-only, like operator bool),
	// standard GL functions are uppercase.
	
	//If false, releases the context. The context is current on creation.
	void makeCurrent(bool make) { core->makeCurrent(make); }
	void swapInterval(int interval) { core->swapInterval(interval); }
	void swapBuffers() { core->swapBuffers(); }
	funcptr getProcAddress(const char * proc) { return core->getProcAddress(proc); }
	
	//If the window is resized, use this function to report the new size.
	//Not needed if the object is created from a viewport.
	void notifyResize(GLsizei width, GLsizei height) { core->notifyResize(width, height); }
	//Used for Direct3D sync. If you're not using that, this will return 0; feel free to use that instead.
	GLuint outputFramebuffer() { return core->outputFramebuffer(); }
	
	//Releases all resources owned by the object; the object may not be used after this.
	//Use if the destructor isn't guaranteed to run while the driver's window still exists.
	//Not needed if the object is created from a viewport.
	void destroy()
	{
		if (port)
		{
			port->set_child(0, NULL, NULL);
			port = NULL;
		}
		delete core;
		core = NULL;
	}
	
	//TODO: reenable these
	//bool hasExtension(const char * ext);
	//void enableDefaultDebugger(FILE* out = NULL); //Use only if the context was created with the debug flag.
	
	//note to self: on modern hardware, drawing fullscreen vertex-uniform
	//is faster with one triangle at 0,0 2,0 0,2 than two triangles at 0,0 1,0 0,1 1,1
	//https://michaldrobot.com/2014/04/01/gcn-execution-patterns-in-full-screen-passes/
	
private:
	context* core;
	widget_viewport* port;
};

#ifndef AROPENGL_SLIM
#include "glsym-all.h"
#endif
