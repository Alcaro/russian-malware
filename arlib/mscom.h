#pragma once
// This file is not strictly part of Arlib. It doesn't use Arlib's objects, naming conventions, or portability promises.
// It can be used together with Arlib, but only if global.h (or the entire arlib.h) is included first.
// It's GCC only - it doesn't even work under Clang, because it doesn't implement <tr2/type_traits>.
// I'll be happy to replace tr2 once a replacement (such as reflection) exists, but as of writing, it's not in the C++ standard.
#include <tr2/type_traits>
// if I want Clang support before a tr2 replacement shows up,
//  I will create a specializable template using/struct stating what an interface's parent is
// the .cpp is required to specialize it for every relevant interface
// under gcc, there will be static_asserts that compare it with tr2 and scream if the answer is wrong

#include <stdint.h>
#include <unknwn.h>
#include <type_traits>
// These objects are named after Microsoft's ATL. Behavior, however, is wildly different (except CComPtr).

// This file makes no attempt to implement or support
// - Tear-offs (sub-interfaces implemented as separate objects, allocated when QI'd) - useless in this year. Just allocate it upfront.
// - Custom interfaces - needs an IDL compiler
// - DllCanUnloadNow - unavoidable race window between the dll's refcount and ctor entry / dtor exit
// (though it doesn't particularly stop you either)

// Available AddRef/Release strategies:
// - I want to implement these functions myself -> CComObjectRoot (the others use this as base class)
// - I want a normal object, created with new, with atomic refcounting -> CComObject (nonatomic unless ARLIB_THREAD is enabled)
// - I want a normal object, created with new, with non-atomic refcounting -> CComObjectNoLock (same as above if ARLIB_THREAD is disabled)
// - I want this object to be part of another, and share its parent's refcount -> CComContainedObject
// - I want lifetime managed by something else, with AddRef/Release being noops -> CComObjectStack (because 'something' is usually stack)

template<typename T>
class CComPtr {
	void assign(T* ptr)
	{
		if (ptr)
			ptr->AddRef();
		if (p)
			p->Release();
		p = ptr;
	}
	void assign(nullptr_t)
	{
		if (p)
			p->Release();
		p = nullptr;
	}
public:
	T* p;
	
	CComPtr() { p = nullptr; }
	CComPtr(T* ptr) { p = nullptr; assign(ptr); }
	~CComPtr() { assign(nullptr); }
	CComPtr(const CComPtr&) = delete;
	CComPtr(CComPtr&&) = delete;
	void operator=(const CComPtr&) = delete;
	void operator=(CComPtr&&) = delete;
	
	CComPtr& operator=(T* ptr)
	{
		assign(ptr);
		return *this;
	}
	T** operator&()
	{
		assign(nullptr);
		return &p;
	}
	T* operator->() { return p; }
	operator T*() { return p; }
	
	// This default class context means 'create the object in the current process, no remoting or marshalling'.
	HRESULT CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext = CLSCTX_INPROC_SERVER)
	{
		assign(nullptr);
		return ::CoCreateInstance(rclsid, pUnkOuter, dwClsContext, IID_PPV_ARGS(&p));
	}
};

// Implements QueryInterface. Does not implement AddRef and Release.
template<typename... Tifaces>
class CComObjectRoot : public Tifaces... {
	template<typename Ti>
	static bool inner_qi(Ti* self, REFIID riid, void** ppvObject)
	{
		static_assert(std::is_base_of_v<IUnknown, Ti>);
		if (riid == __uuidof(Ti))
		{
			self->AddRef();
			*(Ti**)ppvObject = self;
			return true;
		}
		
		if constexpr (std::is_same_v<Ti, IUnknown>)
			return false;
		else
			return inner_qi<typename std::tr2::direct_bases<Ti>::type::first::type>(self, riid, ppvObject);
	}
protected:
	// If you want the set of supported interfaces to be dynamic, or not implemented on the same object (for example due to aggregation),
	//  you can override QueryInterface. You're welcome to call this for the static parts.
	HRESULT NonDelegatingQueryInterface(REFIID riid, void** ppvObject)
	{
		static_assert(sizeof...(Tifaces) > 0);
		*ppvObject = nullptr;
		if ((inner_qi<Tifaces>(this, riid, ppvObject) || ...))
			return S_OK;
		return E_NOINTERFACE;
	}
	
public:
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
	{
		return NonDelegatingQueryInterface(riid, ppvObject);
	}
};

// Implements QueryInterface, AddRef and Release. The latter two store a refcount in this object.
// The object must be created with new.
namespace {
template<typename... Tifaces>
class CComObjectNoLock : public CComObjectRoot<Tifaces...> {
private:
	ULONG refcount = 1;
protected:
	uint32_t spare; // To avoid some padding. A child class can use this for anything; this object doesn't use it.
public:
	ULONG STDMETHODCALLTYPE AddRef() override final
	{
		return ++refcount;
	}
	ULONG STDMETHODCALLTYPE Release() override final
	{
		uint32_t new_refcount = --refcount;
		if (!new_refcount)
		{
			refcount++; // to prevent double delete if the dtor ends up QIing this object (unlikely, but not unthinkable)
			// gcc can devirtualize this, but only if it's in an anon namespace (clang can't, bug 94924)
			// the drawback is the namespace makes it impossible to pass the object across translation units,
			// but the entire COM is platform specific, you shouldn't have cross-TU platform-specific pieces
			delete this;
		}
		return new_refcount;
	}
	virtual ~CComObjectNoLock() {}
};
}

#ifdef ARLIB_THREAD
// Implements QueryInterface, AddRef and Release. The latter two store a refcount in this object, and process it atomically.
// Only the refcounts are atomic; the implemented interfaces need to take locks. The object must be created with new.
namespace {
template<typename... Tifaces>
class CComObject : public CComObjectRoot<Tifaces...> {
private:
	LONG refcount = 1;
protected:
	uint32_t spare;
public:
	ULONG STDMETHODCALLTYPE AddRef() override final
	{
		return __atomic_add_fetch(&refcount, 1, __ATOMIC_RELAXED);
	}
	ULONG STDMETHODCALLTYPE Release() override final
	{
		// needs to be release, to block the sequence
		// initial refcount = 2
		// thread 1 - claim mutex
		// thread 1 - do random stuff
		// thread 1 - release mutex
		// thread 1 - release reference
		// thread 2 - release reference
		// from moving 1's release to inside the random stuff, causing thread 2 to enter dtor while 1 is running
		// (could be solved by demanding the dtor takes a mutex, but that's too easy to miss)
		
		// there's a bunch of COM objects that require breaking ref cycles by holding pointers without a reference
		// (DirectShow filters shouldn't hold refs to the graph, and aggregated objects shouldn't hold a ref to the outer object)
		// but they're playing with fire already, and have their own rules for how to keep the fire safe
		uint32_t new_refcount = __atomic_sub_fetch(&refcount, 1, __ATOMIC_RELEASE);
		if (!new_refcount)
		{
			__atomic_add_fetch(&refcount, 1, __ATOMIC_ACQUIRE); // so thread 2 sees 1's writes, and for same reason as in NoLock
			delete this;
		}
		return new_refcount;
	}
	virtual ~CComObject() {}
};
}
#else
#define CComObject CComObjectNoLock
#endif

// Implements QueryInterface, AddRef and Release. The latter two call AddRef/Release on the object this one is contained in.
// Intended for use with member objects, such as the IPins in an IBaseFilter, where the inner is a direct member of the outer.
// Uses CRTP; the parent must have a parent() function that returns an IUnknown (or a child thereof - usually the entire object).
template<typename Tparent, typename... Tifaces>
class CComContainedObject : public CComObjectRoot<Tifaces...> {
public:
	// would be nice to replace with deducing this,
	// but that only works if the function is called from the child class, not if it implements a virtual
	ULONG STDMETHODCALLTYPE AddRef() override final
	{
		static_assert(std::is_base_of_v<CComContainedObject, Tparent>);
		return ((Tparent*)this)->parent()->AddRef();
	}
	ULONG STDMETHODCALLTYPE Release() override final
	{
		return ((Tparent*)this)->parent()->Release();
	}
};

// Implements QueryInterface, AddRef and Release. The latter two simply do nothing.
// Intended for use with objects declared on the stack, whose lifetime is strictly limited.
// (Global variables work too. Global COM objects are rare, but make sense for IClassFactory.)
template<typename... Tifaces>
class CComObjectStack : public CComObjectRoot<Tifaces...> {
public:
	ULONG STDMETHODCALLTYPE AddRef() override final { return 2; }
	ULONG STDMETHODCALLTYPE Release() override final { return 1; }
};

// If the enumerated object is a COM interface, the accessor can be a member function
//  IPin* fn(size_t idx) (or whatever the enumerator should return)
// which returns the interface without adding a reference, or NULL if out of range.
// If the enumerator returns something else, the accessor must be a member function
//  bool fn(size_t idx, AM_MEDIA_TYPE* * elem)
// which, if the requested item is in range, adds a reference or otherwise copies the value, writes to elem, and returns true.
// If the requested item is out of range, must return false without writing to elem.
// If elem is null, the function must return whether that's in range, without copying the value.
// (The latter form is available for COM objects too, if you like the extra typing.)
namespace {
template<typename Tiface, auto accessor>
class CComEnum final : public CComObject<Tiface> {
public:
	static_assert(std::is_member_pointer_v<decltype(accessor)>);
	// feels like there should be a few type traits for this, but...
	using Tparent = decltype([]<typename Tval, typename Tparent>(Tval Tparent::*)->Tparent{}(accessor));
	// todo: this won't work with IEnumObjects, it takes an extra RIID before Ti* (it presumably QI's the objects, not just AddRef)
	// (but there's few or no usecases for implementing that interface anyways, no known interface refers to it)
	// (everything needing an object collection takes IObjectArray instead)
	// (the only known implementation is CLSID_EnumerableObjectCollection (which also implements IObjectCollection and IObjectArray))
	using Tret = decltype([]<typename Ti>(HRESULT STDMETHODCALLTYPE (Tiface::*)(ULONG, Ti*, ULONG*))->Ti{}(&Tiface::Next));
	
	CComPtr<Tparent> parent; // Tparent isn't a COM interface, but it has AddRef and Release functions so it's good enough
	// todo: generation counter, if parent has one
	
	bool get(size_t idx, Tret* elem)
	{
		// todo: if this object gets popular enough, I could flip the accessor for a series of member pointers
		// but for now, I'll just keep these functions
		if constexpr (std::is_invocable_r_v<Tret, decltype(accessor), Tparent*, size_t>)
		{
			static_assert(std::is_pointer_v<Tret>);
			static_assert(std::is_base_of_v<IUnknown, std::remove_pointer_t<Tret>>);
			Tret ret = (parent.p->*accessor)(idx);
			if (ret && elem)
			{
				ret->AddRef();
				*elem = ret;
			}
			return ret;
		}
		else
		{
			return (parent.p->*accessor)(idx, elem);
		}
	}
	
	CComEnum(Tparent* parent, size_t pos) : parent(parent) { this->spare = pos; }
public:
	CComEnum(Tparent* parent) : parent(parent) { this->spare = 0; }
	
private:
	HRESULT STDMETHODCALLTYPE Clone(Tiface** ppEnum) override
	{
		*ppEnum = new CComEnum(parent, this->spare);
		return S_OK;
	}
	
	HRESULT STDMETHODCALLTYPE Next(ULONG celt, Tret* rgelt, ULONG* pceltFetched) override
	{
		// these enumerators should leave the output unchanged if the target is out of range
		
		size_t pos = this->spare;
		size_t fetched = 0;
		while (fetched < celt)
		{
			if (get(pos+fetched, rgelt+fetched))
				fetched++;
			else
				break;
		}
		this->spare += fetched;
		if (pceltFetched)
			*pceltFetched = fetched;
		if (fetched == celt)
			return S_OK;
		else
			return S_FALSE;
	}
	
	HRESULT STDMETHODCALLTYPE Reset() override
	{
		this->spare = 0;
		return S_OK;
	}
	
	HRESULT STDMETHODCALLTYPE Skip(ULONG celt) override
	{
		this->spare += celt;
		size_t pos = this->spare;
		if (pos == 0)
			return S_OK;
		else if (get(pos-1, nullptr))
			return S_OK;
		else
			return S_FALSE;
	}
};
}

// todo: put back these
// todo: find some function or member name for the own_iunknown
// NonDelegatingQueryInterface(IUnknown), maybe? And all three functions on own_iunknown just call the NonDelegating versions.
// depends on how well it optimizes
/*
template<typename Tparent>
class CComAggObject final : public Tparent {
	// the MS example makes the actual implementation the inner class, and the own IUnknown the outer class, but that's just silly
	// even with everything implemented manually, it gives lots of code an extra level of indentation
	class own_iunknown_t : public IUnknown {
	public:
		// don't allow customizing the refcount strategy of the inner object, too much effort, no point
		ULONG refcount = 1;
		CComAggObject* parent() { return container_of<&CComAggObject::own_iunknown>(this); }
		
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
		{
			// todo: rewrite and simplify, this is a bit of a mess
			// QueryInterface should AddRef the returned interface, not inner or outer
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
			return __atomic_add_fetch(&refcount, 1, __ATOMIC_RELAXED);
		}
		ULONG STDMETHODCALLTYPE Release() override
		{
			uint32_t new_refcount = __atomic_sub_fetch(&refcount, 1, __ATOMIC_RELEASE);
			if (!new_refcount)
			{
				__atomic_add_fetch(&refcount, 1, __ATOMIC_ACQUIRE);
				delete parent();
			}
		}
	};
	own_iunknown_t own_iunknown;
	IUnknown* parent; // does not hold a reference
	
public:
	static IUnknown* Create(IUnknown* parent)
	{
		// does not support operating without a parent
		// (it should, it's just setting parent to own_iunknown, but not until I find a usecase and can test it)
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

// todo: delete these two
// class objects are only used in DllGetClassObject, which should be a function call that returns a static variable with noop refcounting
// (or multiple statics, if the dll exports multiple objects)
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
			return E_FAIL; // DECLARE_ONLY_AGGREGATABLE returns this (VFW_E_NEED_OWNER is better in DShow, but let's be generic)
		if (riid != IID_IUnknown)
			return E_NOINTERFACE; // this is a pretty weird choice, but MS docs say it's the correct error
		T* ret = new T(pUnkOuter);
		*(IUnknown**)ppvObject = &ret->own_iunknown;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) { return S_OK; }
};
*/
