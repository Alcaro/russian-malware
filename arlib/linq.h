//#include "global.h"

#ifndef LINQ_BASE_INCLUDED
#define LINQ_BASE_INCLUDED

namespace linq {
template<typename T, typename Titer> class t_base;
template<typename T, typename Tsrc, typename Tconv> class t_select;
template<typename T, typename Tsrc, typename Tconv> class t_select_idx;
template<typename T, typename Tsrc, typename Tpred> class t_where;
template<typename T, typename Tsrc> class t_linq;
}

//'Tbase' is the base class with .begin() and .end(), including template argument.
//For example: template<typename T> class arrayview : public linqbase<arrayview<T>>
template<typename Tbase>
class linqbase {
	//doesn't exist, only used because the real impl() needs a 'this' and decltype doesn't have that
	//dummy template parameters are to ensure it doesn't refer to Tbase::begin() before Tbase is properly defined
	template<typename _> static const Tbase& decltype_impl();
	
	Tbase& impl() { return *(Tbase*)this; }
	const Tbase& impl() const { return *(Tbase*)this; }
	
	template<typename _>
	class alias {
	public:
		typedef decltype(decltype_impl<_>().begin()) iter;
		typedef typename std::decay<decltype(*decltype_impl<_>().begin())>::type T;
		typedef linq::t_base<T, iter> src;
		typedef linq::t_linq<T, src> linq_t;
	};
	
	template<typename _>
	typename alias<_>::linq_t as_linq() const
	{
		return typename alias<_>::linq_t(typename alias<_>::src(impl().begin(), impl().end()));
	}
public:
	template<typename Tconv, typename T2 = typename std::result_of<Tconv(typename alias<Tconv>::T)>::type>
	//TODO: use full-auto return type when switching to C++14
	//TODO: move t_select into t_base when switching to full-auto return type, then kill namespace and rename them
	auto select(Tconv conv) const -> linq::t_linq<T2, linq::t_select<T2, typename alias<Tconv>::src, Tconv>>
	{
		return as_linq<void>().select(conv);
	}
	template<typename Tconv, typename T2 = typename std::result_of<Tconv(size_t, typename alias<Tconv>::T)>::type>
	
	auto select(Tconv conv) const -> linq::t_linq<T2, linq::t_select_idx<T2, typename alias<Tconv>::src, Tconv>>
	{
		return as_linq<void>().select_idx(conv);
	}
	
	template<typename Tpred>
	auto where(Tpred pred) const ->
		linq::t_linq<typename alias<Tpred>::T, linq::t_where<typename alias<Tpred>::T, typename alias<Tpred>::src, Tpred>>
	{
		return as_linq<void>().where(pred);
	}
	
	template<typename Tpred>
	typename alias<Tpred>::T first(Tpred pred, typename alias<Tpred>::T otherwise = typename alias<Tpred>::T()) const
	{
		return as_linq<void>().first(pred, otherwise);
	}
};
#endif


#ifndef LINQ_BASE
#pragma once
#include "array.h"
#include "set.h"

//This namespace is considered private. Do not store or create any instance, other than what the linqbase functions return.
namespace linq {

template<typename T, typename Titer>
class t_base : nocopy {
public:
	Titer b;
	Titer e;
	
	t_base(t_base&& other) : b(std::move(other.b)), e(std::move(other.e)) {}
	t_base(Titer b, Titer e) : b(b), e(e) {}
	bool hasValue() { return b != e; }
	void moveNext() { ++b; }
	auto get() -> decltype(*std::declval<Titer>()) { return *b; }
};

template<typename T, typename Tsrc, typename Tconv>
class t_select : nocopy {
public:
	Tsrc base;
	Tconv conv;
	
	t_select(Tsrc&& base, Tconv conv) : base(std::move(base)), conv(conv) {}
	bool hasValue() { return base.hasValue(); }
	void moveNext() { base.moveNext(); }
	auto get() -> decltype(std::declval<Tconv>()(std::declval<Tsrc>().get())) { return conv(base.get()); }
};

template<typename T, typename Tsrc, typename Tconv>
class t_select_idx : nocopy {
public:
	Tsrc base;
	Tconv conv;
	size_t n;
	
	t_select_idx(Tsrc&& base, Tconv conv) : base(std::move(base)), conv(conv), n(0) {}
	bool hasValue() { return base.hasValue(); }
	void moveNext() { base.moveNext(); n++; }
	auto get() -> typename std::result_of<Tconv(size_t, T)>::type { return conv(n, base.get()); }
};

template<typename T, typename Tsrc, typename Tpred>
class t_where : nocopy {
public:
	Tsrc base;
	Tpred pred;
	
	void skipFalse() { while (base.hasValue() && !pred(base.get())) base.moveNext(); }
	
	t_where(Tsrc&& base, Tpred pred) : base(std::move(base)), pred(pred) { skipFalse(); }
	bool hasValue() { return base.hasValue(); }
	void moveNext() { base.moveNext(); skipFalse(); }
	auto get() -> decltype(std::declval<Tsrc>().get()) { return base.get(); }
};

template<typename T, typename Tsrc>
class t_enum : nocopy {
public:
	Tsrc& base;
	
	t_enum(Tsrc& base) : base(base) {}
	bool operator!=(const t_enum& other) { return base.hasValue(); }
	void operator++() { base.moveNext(); }
	auto operator*() -> decltype(std::declval<Tsrc>().get()) { return base.get(); }
};

template<typename T, typename Tsrc> class t_linq : nocopy {
public:
	Tsrc base;
	
	t_linq(Tsrc&& base) : base(std::move(base)) {}
	
	t_enum<T, Tsrc> begin() { return base; }
	t_enum<T, Tsrc> end() { return base; }
	
	template<typename Tconv, typename T2 = typename std::result_of<Tconv(T)>::type>
	auto select(Tconv conv) -> t_linq<T2, linq::t_select<T2, Tsrc, Tconv>>
	{
		return t_linq<T2, linq::t_select<T2, Tsrc, Tconv>>(t_select<T2, Tsrc, Tconv>(std::move(base), std::move(conv)));
	}
	
	template<typename Tconv, typename T2 = typename std::result_of<Tconv(size_t, T)>::type>
	auto select_idx(Tconv conv) -> t_linq<T2, linq::t_select_idx<T2, Tsrc, Tconv>>
	{
		return t_linq<T2, linq::t_select_idx<T2, Tsrc, Tconv>>(t_select_idx<T2, Tsrc, Tconv>(std::move(base), std::move(conv)));
	}
	
	template<typename Tpred>
	auto where(Tpred pred) -> t_linq<T, linq::t_where<T, Tsrc, Tpred>>
	{
		return t_linq<T, linq::t_where<T, Tsrc, Tpred>>(t_where<T, Tsrc, Tpred>(std::move(base), std::move(pred)));
	}
	
	template<typename Tpred>
	T first(Tpred pred, T otherwise = T())
	{
		for (auto item : *this)
		{
			if (pred(item)) return item;
		}
		return otherwise;
	}
	
	T join()
	{
		T ret = base.get();
		base.moveNext();
		while (base.hasValue())
		{
			ret += base.get();
			base.moveNext();
		}
		return ret;
	}
	
	//creepy shit... but gcc demands dependent scopes everywhere to disable this operator for reference T
	//and having the function exist, even without callers, instantiates array<int&> and throws errors
	template<typename T3 = int, typename T2 = typename std::enable_if<!std::is_reference<T>::value || sizeof(T3)==-1, T>::type>
	operator array<T2>()
	{
		array<T> ret;
		for (T&& item : *this) ret.append(item);
		return ret;
	}
	
	template<typename T2 = int>
	typename std::enable_if<!std::is_reference<T>::value || sizeof(T2)==-1, array<T>>::type
	as_array()
	{
		return *this;
	}
	
	template<typename T3 = int, typename T2 = typename std::enable_if<!std::is_reference<T>::value || sizeof(T3)==-1, T>::type>
	operator set<T2>()
	{
		set<T> ret;
		for (T&& item : *this) ret.add(item);
		return ret;
	}
	
	template<typename T2 = int>
	typename std::enable_if<!std::is_reference<T>::value || sizeof(T2)==-1, set<T>>::type
	as_set()
	{
		return *this;
	}
};
}

#endif
#undef LINQ_BASE
