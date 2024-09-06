#pragma once

//Inspired by
// http://www.codeproject.com/Articles/136799/ Lightweight Generic C++ Callbacks (or, Yet Another Delegate Implementation)
//but rewritten using C++11 (and later C++17) features, to remove code duplication and 6-arg limits, improve error messages,
// and add some new features

#include <stddef.h>
#include <string.h>
#include <utility>
#include <type_traits>

template<typename T> class function;
template<typename Tr, typename... Ta>
class function<Tr(Ta...)> {
protected:
	typedef Tr(*Tfp)(void* ctx, Ta... args);
	typedef Tr(*Tfpr)(Ta... args);
	
	// context first, so a buffer overflow into this object can't redirect the function without resetting the context
	// I think that's the order that makes exploitation harder, though admittedly I don't have any numbers on that
	void* ctx;
	Tfp func;
	
	class dummy {};
	
	static Tr empty(void* ctx, Ta... args) { return Tr(); }
	static Tr freewrap(void* ctx, Ta... args) { return ((Tfpr)ctx)(std::forward<Ta>(args)...); }
	
	void init_free(Tfpr fp)
	{
		func = freewrap;
		ctx = (void*)fp;
	}
	
	void init_null()
	{
		func = empty;
		ctx = (void*)empty;
	}
	
	void init_ptr(Tfp fp, void* ctx)
	{
		this->func = fp;
		this->ctx = ctx;
	}
	
	template<typename Tl>
	typename std::enable_if_t<std::is_convertible_v<Tl, Tfpr>>
	init_lambda(Tl lambda)
	{
		init_free(lambda);
	}
	
	// copy ctor is fine, lambda objects are fine, but making functions wrap each other means I mismatched an argument type or something
	// this will also fail the sizeof test below, but this extra check gives better errors
	template<typename Tri, typename... Tai>
	void init_lambda(function<Tri(Tai...)>) = delete;
	
	template<typename Tl>
	typename std::enable_if_t<!std::is_convertible_v<Tl, Tfpr>>
	init_lambda(Tl lambda)
	{
		static_assert(std::is_trivially_copyable_v<Tl> && sizeof(Tl) <= sizeof(void*));
		
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
	
public:
	function() { init_null(); }
	function(const function& rhs) = default;
	function(function&& rhs) = default;
	function& operator=(const function& rhs) = default;
	function& operator=(function&& rhs) = default;
	
	function(Tfpr fp) { init_free(fp); }
	function(std::nullptr_t) { init_null(); }
	
	template<typename Tl>
	function(Tl lambda,
	         typename std::enable_if_t<
	            std::is_invocable_r_v<Tr, Tl, Ta...>
	         , dummy> ignore = dummy())
	{
		init_lambda(std::forward<Tl>(lambda));
	}
	
	// can't take member pointers, that's UB
	template<typename Tl, typename Tc>
	function(Tl lambda,
	         Tc* ctx,
	         typename std::enable_if_t<
	            std::is_invocable_r_v<Tr, Tl, Tc*, Ta...>
	         , dummy> ignore = dummy())
	{
		Tr(*func)(Tc*, Ta...) = lambda;
		this->func = (Tfp)func;
		this->ctx = (void*)ctx;
	}
	
	Tr operator()(Ta... args) const { return func(ctx, std::forward<Ta>(args)...); }
protected:
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
	//(7) the callee does something significant with a falsy function - more than just not calling it
	//many of which are extremely unlikely. In particular, the combination 2+4 seems quite impossible to me.
	//Alternatively, you could craft a false positive by abusing decompose(), but if you do that, you're asking for trouble.
	bool isTrue() const
	{
		return (func != empty || (void*)func != ctx);
	}
public:
	explicit operator bool() const { return isTrue(); }
	bool operator!() const { return !isTrue(); }
	
protected:
	struct binding {
		Tfp fp;
		void* ctx;
	};
public:
	//Splits a function object into a function pointer and a context argument.
	//Calling the pointer, with the context as first argument, is equivalent to calling the function object directly
	// (possibly modulo a few move constructor calls).
	//If the function is a big_function, the function object must be kept alive during any use of the binding.
	binding decompose()
	{
		return { func, ctx };
	}
};

template<typename T> struct remove_function_cruft;
template<typename Tc, typename Tr, typename... Ta> struct remove_function_cruft<Tr (Tc::*)(Ta...) const> { using type = Tr(Ta...); };
template<typename Tc, typename Tr, typename... Ta> struct remove_function_cruft<Tr (Tc::*)(Ta...)      > { using type = Tr(Ta...); };
template<typename T> function(T lambda) -> function<typename remove_function_cruft<decltype(&T::operator())>::type>;

// A big_function is like a normal function, but it can bind more than one pointer, or anything that takes a copy constructor.
template<typename T> class big_function;
template<typename Tr, typename... Ta>
class big_function<Tr(Ta...)> : public function<Tr(Ta...)> {
	typedef Tr(*Tfp)(void* ctx, Ta... args);
	typedef Tr(*Tfpr)(Ta... args);
	
	struct refcount
	{
		size_t count;
		void(*destruct)(void* ctx);
	};
	refcount* ref = NULL;
	
	class dummy {};
	
	void add_ref()
	{
		if (!ref) return;
		ref->count++;
	}
	
	void unref()
	{
		if (!ref) return;
		if (!--ref->count)
		{
			ref->destruct(this->ctx);
			delete ref;
		}
		ref = NULL;
	}
	
	// copy ctor is fine, lambda objects are fine, but making functions wrap each other means I mismatched an argument type or something
	// this will also fail the sizeof test below, but this extra check gives better errors
	template<typename Tri, typename... Tai>
	void init_lambda_big(big_function<Tri(Tai...)>) = delete;
	
	template<typename Tl>
	void init_lambda_big(Tl lambda)
	{
		if constexpr (std::is_trivially_copyable_v<Tl> && sizeof(Tl) <= sizeof(void*))
		{
			this->init_lambda(std::forward<Tl>(lambda));
		}
		else
		{
			class holder {
				refcount rc;
				Tl l;
			public:
				holder(Tl l) : l(l) { rc.count = 1; rc.destruct = &holder::destruct; }
				static Tr call(holder* self, Ta... args) { return self->l(std::forward<Ta>(args)...); }
				static void destruct(void* self) { ((holder*)self)->l.~Tl(); } // don't delete self, unref() does that (todo: check if UB)
			};
			this->func = (Tfp)&holder::call;
			this->ctx = new holder(std::move(lambda));
			this->ref = (refcount*)this->ctx;
		}
	}
	
public:
	big_function() { this->init_null(); }
	big_function(const big_function& rhs) { this->func = rhs.func; this->ctx = rhs.ctx; ref = rhs.ref; add_ref(); }
	big_function(big_function&& rhs)      { this->func = rhs.func; this->ctx = rhs.ctx; ref = rhs.ref; rhs.ref = NULL; }
	big_function& operator=(const big_function& rhs)
		{ unref(); this->func = rhs.func; this->ctx = rhs.ctx; ref = rhs.ref; add_ref(); return *this; }
	big_function& operator=(big_function&& rhs)
		{ unref(); this->func = rhs.func; this->ctx = rhs.ctx; ref = rhs.ref; rhs.ref = NULL; return *this; }
	~big_function() { unref(); }
	
	big_function(Tfpr fp) { this->init_free(fp); }
	big_function(std::nullptr_t) { this->init_null(); }
	
	template<typename Tl>
	big_function(Tl lambda,
	             typename std::enable_if_t<
	                std::is_invocable_r_v<Tr, Tl, Ta...>
	             , dummy> ignore = dummy())
	{
		init_lambda_big(std::forward<Tl>(lambda));
	}
	
	template<typename Tl, typename Tc>
	big_function(Tl lambda,
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
	big_function(Tl lambda,
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
};

// for simple cases, a lambda capturing [this] is generally wiser than bind_ptr/bind_this

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

// I'd delete this if I could, but the pmf must be a template param so a wrapper fptr can be created
#define bind_ptr(fn, ptr) (function_binder<fn>(ptr, fn))
#define bind_this(fn) bind_ptr(fn, this) // reminder: bind_this(&classname::function), not bind_this(function)

template<typename Tl> auto decompose_lambda(Tl&& l) { return function(std::move(l)).decompose(); }

template<typename T>
auto to_fptr(T tl)
{
	static_assert(std::is_trivially_constructible_v<T>);
	return (typename remove_function_cruft<decltype(&T::operator())>::type*)tl;
}

// todo: find which project I used this for
/*
//Implementation detail of the below.
template<bool found, typename Tl1> void* decompose_get_userdata()
{
	static_assert(found);
	return NULL;
}
template<bool found, typename Tl1, typename Tn, typename... Ta>
void* decompose_get_userdata(Tn&& arg, Ta&&... args)
{
	// if Tl1 takes a void*, we only want to give it a void*, not any random pointer
	if constexpr (std::is_invocable_v<Tl1, Tn> &&
		(std::is_same_v<std::remove_reference_t<Tn>, void*> || !std::is_invocable_v<Tl1, void*>))
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
*/
