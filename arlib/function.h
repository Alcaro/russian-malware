#pragma once

//Inspired by
// http://www.codeproject.com/Articles/136799/ Lightweight Generic C++ Callbacks (or, Yet Another Delegate Implementation)
//but rewritten using C++11 (and later C++17) features, to remove code duplication and 6-arg limits, and improve error messages

#include <stddef.h>
#include <string.h>
#include <utility>
#include <type_traits>

#ifdef __GNUC__
#define LIKELY(expr)    __builtin_expect(!!(expr), true)
#define UNLIKELY(expr)  __builtin_expect(!!(expr), false)
#else
#define LIKELY(expr)    (expr)
#define UNLIKELY(expr)  (expr)
#endif

#if __cplusplus < 201703L
#error need C++17
#endif

// TODO: create a cfunction class, like string/cstring
// or maybe ofunction, like image/oimage

template<typename T> class function;
template<typename Tr, typename... Ta>
class function<Tr(Ta...)> {
	typedef Tr(*Tfp)(void* ctx, Ta... args);
	typedef Tr(*Tfpr)(Ta... args);
	
	struct refcount
	{
		size_t count;
		void(*destruct)(void* ctx);
	};
	
	// context first, so a buffer overflow into this object can't redirect the function without resetting the context
	// I think that's the order that makes exploitation harder, though admittedly I don't have any numbers on that
	void* ctx;
	Tfp func;
	refcount* ref;
	
	class dummy {};
	
	static Tr empty(void* ctx, Ta... args) { return Tr(); }
	static Tr freewrap(void* ctx, Ta... args) { return ((Tfpr)ctx)(std::forward<Ta>(args)...); }
	
	void add_ref()
	{
		if (LIKELY(!ref)) return;
		ref->count++;
	}
	
	void unref()
	{
		if (LIKELY(!ref)) return;
		if (!--ref->count)
		{
			bool do_del = ((void*)ref != ctx); // ref==ctx happens if binding a lambda capturing a lot
			ref->destruct(ctx);
			if (do_del)
				delete ref;
		}
		ref = NULL;
	}
	
	void init_free(Tfpr fp)
	{
		if (fp)
		{
			func = freewrap;
			ctx = (void*)fp;
			ref = NULL;
		}
		else
		{
			func = empty;
			ctx = (void*)empty;
			ref = NULL;
		}
	}
	
	void init_ptr(Tfp fp, void* ctx)
	{
		this->func = fp;
		this->ctx = ctx;
		this->ref = NULL;
	}
	
	template<typename Tl>
	typename std::enable_if_t< std::is_convertible_v<Tl, Tfpr>>
	init_lambda(Tl lambda)
	{
		init_free(lambda);
	}
	
	template<typename Tl>
	typename std::enable_if_t<!std::is_convertible_v<Tl, Tfpr>>
	init_lambda(Tl lambda)
	{
		if (std::is_trivially_move_constructible_v<Tl> &&
		    std::is_trivially_destructible_v<Tl> &&
		    sizeof(Tl) <= sizeof(void*))
		{
			void* obj = NULL;
			memcpy(&obj, &lambda, sizeof(lambda));
			
			auto wrap = [](void* ctx, Ta... args)
			{
				alignas(Tl) char l[sizeof(void*)];
				memcpy(l, &ctx, sizeof(void*));
				return (*(Tl*)l)(std::forward<Ta>(args)...);
			};
			init_ptr(wrap, obj);
		}
		else
		{
			class holder {
				refcount rc;
				Tl l;
			public:
				holder(Tl l) : l(l) { rc.count = 1; rc.destruct = &holder::destruct; }
				static Tr call(holder* self, Ta... args) { return self->l(std::forward<Ta>(args)...); }
				static void destruct(void* self) { delete (holder*)self; }
			};
			this->func = (Tfp)&holder::call;
			this->ctx = new holder(std::move(lambda));
			this->ref = (refcount*)this->ctx;
		}
	}
	
public:
	function() { init_free(NULL); }
	function(const function& rhs) : ctx(rhs.ctx), func(rhs.func), ref(rhs.ref) { add_ref(); }
	function(function&& rhs)      : ctx(rhs.ctx), func(rhs.func), ref(rhs.ref) { rhs.ref = NULL; }
	function& operator=(const function& rhs)
		{ unref(); func = rhs.func; ctx = rhs.ctx; ref = rhs.ref; add_ref(); return *this; }
	function& operator=(function&& rhs)
		{ unref(); func = rhs.func; ctx = rhs.ctx; ref = rhs.ref; rhs.ref = NULL; return *this; }
	~function() { unref(); }
	
	function(Tfpr fp) { init_free(fp); } // function(NULL) hits here
	
	template<typename Tl>
	function(Tl lambda,
	         typename std::enable_if_t<
	            std::is_invocable_r_v<Tr, Tl, Ta...>
	         , dummy> ignore = dummy())
	{
		init_lambda(std::forward<Tl>(lambda));
	}
	
	template<typename Tl, typename Tc>
	function(Tl lambda,
	         Tc* ctx,
	         typename std::enable_if_t<
	            std::is_invocable_r_v<Tr, Tl, Tc*, Ta...>
	         , dummy> ignore = dummy())
	{
		this->func = (Tfp)(Tr(*)(Tc*, Ta...))lambda;
		this->ctx = (void*)ctx;
		this->ref = NULL;
	}
	
	template<typename Tl, typename Tc>
	function(Tl lambda,
	         Tc* ctx,
	         void(*destruct)(void* ctx),
	         typename std::enable_if_t<
	            std::is_invocable_r_v<Tr, Tl, Tc*, Ta...>
	         , dummy> ignore = dummy())
	{
		this->func = (Tfp)(Tr(*)(Tc*, Ta...))lambda;
		this->ctx = (void*)ctx;
		this->ref = new refcount;
		this->ref->count = 1;
		this->ref->destruct = destruct;
	}
	
	Tr operator()(Ta... args) const { return func(ctx, std::forward<Ta>(args)...); }
private:
	//to make null objects callable, 'func' must be a valid function
	//empty() is weak, so it deduplicates, checking for that is easy
	//but empty() could also be deduplicated with some explicit binding that's optimized out, so we need something else too
	//for example, obj=func=empty
	//this will give false positives if
	//(1) the function is created from a lambda
	//(2) the lambda value-binds a size_t
	//(3) the size_t is random-looking or uninitialized, and just accidentally happens to be same as 'empty'
	//(4) the lambda does nothing and is optimized out - in particular, it does not use its bound variable
	//(5) the compiler does not optimize out the unused bind
	//(6) the compiler merges the lambda with 'empty'
	//(7) the callee does anything significant with a falsy function - more than just not calling it
	//many of which are extremely unlikely. In particular, the combination 2+4 seems quite impossible to me.
	//Alternatively, you could craft a false positive by abusing decompose(), but if you do that, you're asking for trouble.
	bool isTrue() const
	{
		return (func != empty || (void*)func != ctx);
	}
public:
	explicit operator bool() const { return isTrue(); }
	bool operator!() const { return !isTrue(); }
	
private:
	struct unsafe_binding {
		bool safe;
		Tfp fp;
		void* ctx;
		operator bool() { return safe; }
	};
	struct binding {
		Tfp fp;
		void* ctx;
	};
public:
	//Splits a function object into a function pointer and a context argument.
	//Calling the pointer, with the context as first argument, is equivalent to calling the function object directly
	// (possibly modulo a few move constructor calls).
	//
	//WARNING: If the object owns memory, it must remain alive during any use of ctx.
	//This function assumes you don't want to keep this object alive.
	//Therefore, if you call this on something that should stay alive, you'll get a crash.
	//
	//The function object owns memory if it refers to a lambda binding more than sizeof(void*) bytes.
	//In typical cases (a member function, or a lambda binding 'this' or nothing), this is safe.
	//If you want to decompose but keep the object alive, use try_decompose.
	binding decompose()
	{
		if (ref) __builtin_trap();
		return { func, ctx };
	}
	
	//Like the above, but assumes you will keep the object alive, so it always succeeds.
	//Potentially useful to skip a level of indirection when wrapping a C callback into an Arlib
	// class, but should be avoided under most circumstances. Direct use of a C API can know what's
	// being bound, and can safely use the above instead.
	unsafe_binding try_decompose()
	{
		return { !ref, func, ctx };
	}
	
	//TODO: reenable, and add a bunch of static asserts that every type (including return) is either unchanged,
	// or a primitive type (integer, float or pointer - no structs or funny stuff) of the same size as the original
	//Usage: function<void(void*)> x; function<void(int*)> y = x.reinterpret<void(int*)>();
	//template<typename T>
	//function<T> reinterpret()
	//{
	//	
	//}
};

//std::function can work without this extra class in C++11, but only by doing another level of indirection at runtime
//C++17 template<auto fn> could probably do it without lamehacks, except still needs to get 'this' from somewhere
//bind_ptr could probably be rewritten to fn_wrap<decltype(fn), fn>(ptr),
// but that would force the function->arguments unpacker somewhere else, making the function so complex it doesn't solve anything
//and there's no need to do that, anyways; this one works, everything else is irrelevant
//callers should prefer a lambda with a [this] capture
template<typename Tc, typename Tr, typename... Ta>
class memb_rewrap {
public:
	template<Tr(Tc::*fn)(Ta...)>
	function<Tr(Ta...)> get(Tc* ctx)
	{
		return {
			[](Tc* ctx, Ta... args)->Tr { return (ctx->*fn)(std::forward<Ta>(args)...); },
			ctx };
	}
};
template<typename Tc, typename Tr, typename... Ta>
memb_rewrap<Tc, Tr, Ta...>
fn_wrap(Tr(Tc::*)(Ta...))
{
	return memb_rewrap<Tc, Tr, Ta...>();
}

template<typename Tc, typename Tr, typename... Ta>
class memb_rewrap_const {
public:
	template<Tr(Tc::*fn)(Ta...) const>
	function<Tr(Ta...)> get(const Tc* ctx)
	{
		return {
			[](const Tc* ctx, Ta... args)->Tr { return (ctx->*fn)(std::forward<Ta>(args)...); },
			ctx };
	}
};
template<typename Tc, typename Tr, typename... Ta>
memb_rewrap_const<Tc, Tr, Ta...>
fn_wrap(Tr(Tc::*)(Ta...) const)
{
	return memb_rewrap_const<Tc, Tr, Ta...>();
}

#define bind_ptr(fn, ptr) (fn_wrap(fn).template get<fn>(ptr))
#define bind_this(fn) bind_ptr(fn, this) // reminder: bind_this(&classname::function), not bind_this(function)

//while the function template can be constructed from a lambda, I want bind_lambda(...).decompose(...) to work
//so I need something slightly more complex than #define bind_lambda(...) { __VA_ARGS__ }
template<typename Tl, typename Tr, typename... Ta>
function<Tr(Ta...)> bind_lambda_core(Tl&& l, Tr (Tl::*f)(Ta...) const)
{
	return l;
}
template<typename Tl, typename Tr, typename... Ta> // overload for mutable lambdas
function<Tr(Ta...)> bind_lambda_core(Tl&& l, Tr (Tl::*f)(Ta...))
{
	return l;
}
template<typename Tl>
decltype(bind_lambda_core<Tl>(std::declval<Tl>(), &Tl::operator()))
bind_lambda(Tl&& l)
{
	return bind_lambda_core<Tl>(std::move(l), &Tl::operator());
}

//I could make this a compile error if the lambda can't safely decompose, but no need. it'll be immediately caught at runtime anyways
//I could also create a swapped decompose function, userdata at end rather than start, but that'd have to
// copy half of the function template, and the only one needing userdata at end is GTK which can swap by itself anyways.
template<typename Tl>
auto decompose_lambda(Tl&& l) { return bind_lambda(std::move(l)).decompose(); }
