#pragma once
#include "global.h" // objbase.h and unknwn.h and whatever else COM needs are included here
// can't swap for objbase.h directly, global.h sets a bunch of #defines to configure what objbase.h should contain
#include <tr2/type_traits>
// These objects are named after Microsoft's ATL. Behavior, however, is different.
// Not included in arlib.h because it's too platform specific, and because <tr2/type_traits> only works in GCC, not in Clang.
// I'll be happy to replace tr2 once a replacement exists, but I am (as of writing) not aware of any such standardization efforts.

#error "rewrite CComObject from toplevel class to a base class that inherits from a typename... of interfaces"
// advantages of being toplevel:
// - can check if QueryInterface is overridden
// - dtor doesn't need to be virtual (todo: check if that gets optimized out)
// advantages of being base:
// - don't need to check if QueryInterface is overridden, that piece is awful
//     I'll implement the virtual QueryInterface as a single call to a protected nonvirtual method (named NonDelegatingQueryInterface)
// - no need for tr2::bases, can pass as template args to CComObject instead
//     except that fails for interfaces inheriting each other, especially IUnknown; I need tr2 for that
//     but it's an improvement, at least; it allows deleting the 'does this use IUnknown' 
// - no need for new CComObject<my_class>(), can just be new my_class()
//     which especially helps if the ctor takes arguments
// - QI doesn't need to check for and ignore things that aren't COM interfaces
//     interfaces go into CComObject's args, other child classes go directly on the parent
// - the main class can be final
// - the 'called virtual function in constructor/destructor' trap is impossible
// - since CComObject's ctor just sets refcount, while the real object does something arbitrarily complex,
//     it's easier to optimize out that vtable
// also delete the void*-returning QueryInterface, having that name without calling AddRef is silly
// also create a suitable enum implementation (IEnumPins, IEnumMediaTypes, etc), with customizable
//    clone function, and a generation counter so the parent can choose to invalidate every iterator
// also create a CComSubObject class that shares its refcount with the parent object via container_of (needs a CRTP)

// can't do it right now; this header's only caller is a now-dead temp dll used only during krkrwine 9.0 development, so I can't test it

template<typename T>
class CComPtr {
	void assign(T* ptr)
	{
		p = ptr;
	}
	void release()
	{
		if (p)
			p->Release();
		p = nullptr;
	}
public:
	T* p;
	
	CComPtr() { p = nullptr; }
	~CComPtr() { release(); }
	CComPtr(const CComPtr&) = delete;
	CComPtr(CComPtr&&) = delete;
	void operator=(const CComPtr&) = delete;
	void operator=(CComPtr&&) = delete;
	
	CComPtr& operator=(T* ptr)
	{
		release();
		assign(ptr);
		return *this;
	}
	T** operator&()
	{
		release();
		return &p;
	}
	T* operator->() { return p; }
	operator T*() { return p; }
	
	HRESULT CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext)
	{
		release();
		return ::CoCreateInstance(rclsid, pUnkOuter, dwClsContext, IID_PPV_ARGS(&p));
	}
	HRESULT CopyTo(T** ppT)
	{
		p->AddRef();
		*ppT = p;
		return S_OK;
	}
};

template<typename Tparent>
class CComObject final : public Tparent {
	// if this fails due to ambiguous base, switch to std::tr2::direct_bases and keep upcasting
	// it'll try IUnknown multiple times; that's fine, it'll get optimized out
	// (may still fail if a direct base is also indirect; if so, delete the duplicate)
	template<typename Tbases>
	static void* inner_qi(Tparent* obj, REFIID riid)
	{
		if constexpr (!Tbases::empty::value)
		{
			if constexpr (std::is_base_of_v<IUnknown, typename Tbases::first::type>)
			{
				if (riid == __uuidof(typename Tbases::first::type))
				{
					typename Tbases::first::type* ret = obj;
					return (void*)ret;
				}
			}
			return inner_qi<typename Tbases::rest::type>(obj, riid);
		}
		return nullptr;
	}
public:
	static void* QueryInterface(Tparent* obj, REFIID riid)
	{
		return inner_qi<typename std::tr2::bases<Tparent>::type>(obj, riid);
	}
	
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
	{
		if constexpr (requires {
				// token soup yay
				// if the parent object's QueryInterface is assignable to a member pointer to IUnknown,
				// then that function is implemented directly on IUnknown, and calling it on the parent would just recurse
				// if it's not assignable, then the parent implemented that function itself, and that implementation should be called
				// (can't just take the pointer and deconstruct it, base IUnknown has an overload)
				*std::declval<HRESULT(STDMETHODCALLTYPE IUnknown:: **)(REFIID, void**)>() = &Tparent::QueryInterface;
			})
		{
			*ppvObject = QueryInterface(this, riid);
			if (!*ppvObject)
				return E_NOINTERFACE;
			AddRef();
			return S_OK;
		}
		else
		{
			return Tparent::QueryInterface(riid, ppvObject);
		}
	}
	
private:
	ULONG refcount = 1;
public:
	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return ++refcount;
	}
	ULONG STDMETHODCALLTYPE Release() override
	{
		uint32_t new_refcount = --refcount;
		if (!new_refcount)
			delete this;
		return new_refcount;
	}
};

template<typename Tparent>
class CComAggObject final : public Tparent {
	class own_iunknown_t : public IUnknown {
	public:
		ULONG refcount = 1;
		CComAggObject* parent() { return container_of<&CComAggObject::own_iunknown>(this); }
		
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
		{
			if (riid == __uuidof(IUnknown))
			{
				this->AddRef();
				*(IUnknown**)ppvObject = this;
				return S_OK;
			}
			*ppvObject = CComObject<Tparent>::QueryInterface(parent(), riid);
			if (!*ppvObject)
				return E_NOINTERFACE;
			parent()->AddRef();
			return S_OK;
		}
		
		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++refcount;
		}
		ULONG STDMETHODCALLTYPE Release() override
		{
			uint32_t new_refcount = --refcount;
			if (!new_refcount)
				delete parent();
			return new_refcount;
		}
	};
	own_iunknown_t own_iunknown;
	IUnknown* parent;
	
public:
	static IUnknown* Create(IUnknown* parent)
	{
		// does not support operating without a parent
		// (probably should, it's probably just setting parent to own_iunknown, but not until I find a usecase and can test it)
		CComAggObject* self = new CComAggObject();
		self->parent = parent;
		return &self->own_iunknown;
	}
	
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
		{ return parent->QueryInterface(riid, ppvObject); }
	ULONG STDMETHODCALLTYPE AddRef() override
		{ return parent->AddRef(); }
	ULONG STDMETHODCALLTYPE Release() override
		{ return parent->Release(); }
};

template<typename T>
class CClassFactory : public IClassFactory {
public:
	HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) override
	{
		if (pUnkOuter != nullptr)
			return CLASS_E_NOAGGREGATION;
		T* obj = new T();
		HRESULT hr = obj->QueryInterface(riid, ppvObject);
		obj->Release();
		return hr;
	}
	HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { return S_OK; } // don't care
};

template<typename T>
class CAggregatingClassFactory : public IClassFactory {
public:
	HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject)
	{
		if (pUnkOuter == nullptr)
			return E_POINTER; // todo: find better error (VFW_E_NEED_OWNER is good in DShow, but is absurd elsewhere)
		if (riid != IID_IUnknown)
			return E_NOINTERFACE;
		T* ret = new T(pUnkOuter);
		*(IUnknown**)ppvObject = &ret->own_iunknown;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) { return S_OK; }
};
