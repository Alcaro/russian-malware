#pragma once

//Inspired by
// http://www.codeproject.com/Articles/136799/ Lightweight Generic C++ Callbacks (or, Yet Another Delegate Implementation)
//but rewritten using C++11 (and later C++17) features, to remove code duplication and 6-arg limits, improve error messages,
// and add some new features

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
		if (std::is_trivially_copyable_v<Tl> && sizeof(Tl) <= sizeof(void*))
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
	//In typical cases (a member function, or a lambda binding [this] or nothing), this is safe.
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
	// or a primitive type (integer or pointer - no struct, float, or other funny stuff) of the same size as the original
	//Usage: function<void(void*)> x; function<void(int*)> y = x.reinterpret<void(int*)>();
	//template<typename T>
	//function<T> reinterpret()
	//{
	//	
	//}
};

// for simple cases, a lambda capturing [this] is generally wiser than bind_ptr/bind_this

#if __cplusplus >= 201703L

// this isn't a constructor, so I don't think c++17/20 deduction guides can simplify it any further
// and fn needs to be a constant expression, not sure if deduction guides can do that
template<auto fn, typename Tc, typename Tr, typename... Ta>
function<Tr(Ta...)> function_binder(const Tc* ctx, Tr(Tc::*)(Ta...) const)
{
	return {
		[](const Tc* ctx, Ta... args)->Tr { return (ctx->*fn)(std::forward<Ta>(args)...); },
		ctx };
}
template<auto fn, typename Tc, typename Tr, typename... Ta>
function<Tr(Ta...)> function_binder(      Tc* ctx, Tr(Tc::*)(Ta...)      ) // extra overload for mutable lambdas
{
	return {
		[](      Tc* ctx, Ta... args)->Tr { return (ctx->*fn)(std::forward<Ta>(args)...); },
		ctx };
}

#define bind_ptr(fn, ptr) (function_binder<fn>(ptr, fn))
#define bind_this(fn) bind_ptr(fn, this) // reminder: bind_this(&classname::function), not bind_this(function)

#else

template<typename Tc, typename Tr, typename... Ta>
struct function_binder_inner {
	template<Tr(Tc::*fn)(Ta...)>
	function<Tr(Ta...)> get(Tc* ctx)
	{
		return {
			[](Tc* ctx, Ta... args)->Tr { return (ctx->*fn)(std::forward<Ta>(args)...); },
			ctx };
	}
	
	template<Tr(Tc::*fn)(Ta...) const>
	function<Tr(Ta...)> get(const Tc* ctx)
	{
		return {
			[](const Tc* ctx, Ta... args)->Tr { return (ctx->*fn)(std::forward<Ta>(args)...); },
			ctx };
	}
};
template<typename Tc, typename Tr, typename... Ta>
function_binder_inner<Tc, Tr, Ta...>
function_binder(Tr(Tc::*)(Ta...))
{ return {}; }
template<typename Tc, typename Tr, typename... Ta>
function_binder_inner<Tc, Tr, Ta...>
function_binder(Tr(Tc::*)(Ta...) const)
{ return {}; }

#define bind_ptr(fn, ptr) (function_binder(fn).template get<fn>(ptr))
#define bind_this(fn) bind_ptr(fn, this)

#endif

//while the function template can be constructed from a lambda, I want bind_lambda(...).decompose(...) to work
//so I need something slightly more complex than #define bind_lambda(...) { __VA_ARGS__ }
template<typename Tl, typename Tr, typename... Ta>
function<Tr(Ta...)> bind_lambda_core(Tl&& l, Tr (Tl::*f)(Ta...) const)
{ return l; }
template<typename Tl, typename Tr, typename... Ta>
function<Tr(Ta...)> bind_lambda_core(Tl&& l, Tr (Tl::*f)(Ta...))
{ return l; }
template<typename Tl>
decltype(bind_lambda_core<Tl>(std::declval<Tl>(), &Tl::operator()))
bind_lambda(Tl&& l)
{
	return bind_lambda_core<Tl>(std::move(l), &Tl::operator());
}

//I could make this a compile error if the lambda can't safely decompose, but no need. it'll be immediately caught at runtime anyways
//swapped decompose function is also possible, but also a waste of time, better use the two-lambda version (or g_signal_connect_swapped)
template<typename Tl>
auto decompose_lambda(Tl&& l) { return bind_lambda(std::move(l)).decompose(); }

//Implementation detail of the below.
template<bool found, typename Tl1> void* decompose_get_userdata()
{
	static_assert(found);
	return NULL;
}
template<bool found, typename Tl1, typename Tn, typename... Ta>
void* decompose_get_userdata(Tn&& arg, Ta&&... args)
{
	if constexpr (std::is_invocable_v<Tl1, Tn>)
	{
		static_assert(!found);
		alignas(Tl1) char l1[sizeof(void*)] = {};
		void* ret = (*(Tl1*)l1)(arg);
		decompose_get_userdata<true, Tl1>(args...);
		return ret;
	}
	else
	{
		return decompose_get_userdata<found, Tl1>(args...);
	}
}
template<bool large, typename Tl1, typename Tl2, typename Tr, typename... Ta>
auto decompose_lambda_explicit_inner_2(Tl2&& exec)
{
	typedef Tr(*Tfp)(Ta... args);
	Tfp inner_exec = [](Ta... args)->Tr {
		void* ctx;
		alignas(Tl1) char l1[sizeof(void*)] = {};
		if constexpr (std::is_invocable_v<Tl1, Ta...>)
			ctx = (*(Tl1*)l1)(args...);
		else
			ctx = decompose_get_userdata<false, Tl1>(args...);
		
		if constexpr (!large)
		{
			alignas(Tl2) char l2[sizeof(void*)];
			memcpy(l2, &ctx, sizeof(void*));
			return (*(Tl2*)l2)(std::forward<Ta>(args)...);
		}
		else
		{
			return (*(Tl2*)ctx)(std::forward<Ta>(args)...);
		}
	};
	
	if constexpr (!large)
	{
		void* ctx = NULL;
		memcpy(&ctx, &exec, sizeof(exec));
		
		struct binding { Tfp fp; void* ctx; };
		return (binding){ inner_exec, ctx };
	}
	else
	{
		struct binding { Tfp fp; Tl2 ctx; };
		return (binding){ inner_exec, std::move(exec) };
	}
}
template<bool large, typename Tl1, typename Tl2, typename Tr, typename... Ta>
auto decompose_lambda_explicit_inner(Tl2&& exec, Tr (Tl2::*f)(Ta...))
{
	return decompose_lambda_explicit_inner_2<large, Tl1, Tl2, Tr, Ta...>(std::move(exec));
}
template<bool large, typename Tl1, typename Tl2, typename Tr, typename... Ta>
auto decompose_lambda_explicit_inner(Tl2&& exec, Tr (Tl2::*f)(Ta...) const)
{
	return decompose_lambda_explicit_inner_2<large, Tl1, Tl2, Tr, Ta...>(std::move(exec));
}

//Like the usual decompose_lambda, but the resulting function pointer has userdata somewhere other than the first argument.
//Instead, this function takes two lambdas. Both take same arguments, but the first binds nothing and returns void* userdata.
//Alternatively, the first lambda can take only one argument, which must match exactly one argument (doesn't matter which) in the second.
template<typename Tl1, typename Tl2>
auto decompose_lambda(Tl1&& extract, Tl2&& exec)
{
	return decompose_lambda_explicit_inner<false, Tl1, Tl2>(std::move(exec), &Tl2::operator());
	
	static_assert(std::is_trivially_copyable_v<Tl2>); // putting these asserts higher makes it fail to deduce return type
	static_assert(sizeof(Tl2) <= sizeof(void*));
}
//Like the above, but returns { function pointer, the second lambda }, not { fp, void* }. Use a pointer to the lambda as userdata.
//The drawback over the above is lifetime management; the advantage is the lambda can bind multiple things,
// and can value bind nontrivial objects.
template<typename Tl1, typename Tl2>
auto decompose_lambda_large(Tl1&& extract, Tl2&& exec)
{
	return decompose_lambda_explicit_inner<true, Tl1, Tl2>(std::move(exec), &Tl2::operator());
}
