#if defined(_WIN32) && defined(__GNUC__) && !defined(__clang__)
#define INITGUID
#include "global.h"
#include "os.h"
#include "mscom.h"
#include "test.h"
#include <dshow.h>

namespace {
DECL_DYLIB_T(ole32_t, CoInitializeEx, CoCreateInstance, CoTaskMemAlloc, CoTaskMemFree);
ole32_t ole32;

class my_filter : public CComObject<IBaseFilter> {
public:
	class my_sink_pin : public CComContainedObject<my_sink_pin, IPin, IMemInputPin> {
	public:
		my_filter* parent() { return container_of<&my_filter::sink>(this); }
		
		HRESULT STDMETHODCALLTYPE Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt) override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt) override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE Disconnect() override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE ConnectedTo(IPin** pPin) override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE ConnectionMediaType(AM_MEDIA_TYPE* pmt) override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE QueryPinInfo(PIN_INFO* pInfo) override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE QueryDirection(PIN_DIRECTION* pPinDir) override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE QueryId(LPWSTR* Id) override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE QueryAccept(const AM_MEDIA_TYPE* pmt) override { return E_OUTOFMEMORY; }
		
		bool get_media_type(size_t idx, AM_MEDIA_TYPE* * elem)
		{
			static const GUID types[] = {
				MEDIASUBTYPE_MPEG1System,
				MEDIASUBTYPE_MPEG1VideoCD,
				MEDIASUBTYPE_MPEG1Video,
				MEDIASUBTYPE_MPEG1Audio,
			};
			if (idx >= ARRAY_SIZE(types))
				return false;
			if (!elem)
				return true;
			
			*elem = (AM_MEDIA_TYPE*)ole32.CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
			(*elem)->majortype = MEDIATYPE_Stream;
			(*elem)->subtype = types[idx];
			return true;
		}
		HRESULT STDMETHODCALLTYPE EnumMediaTypes(IEnumMediaTypes** ppEnum) override
		{
			*ppEnum = new CComEnum<IEnumMediaTypes, &my_sink_pin::get_media_type>(this);
			return S_OK;
		}
		
		HRESULT STDMETHODCALLTYPE QueryInternalConnections(IPin** apPin, ULONG* nPin) override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE EndOfStream() override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE BeginFlush() override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE EndFlush() override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) override { return E_OUTOFMEMORY; }
		
		HRESULT STDMETHODCALLTYPE GetAllocator(IMemAllocator** ppAllocator) override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly) override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE GetAllocatorRequirements(ALLOCATOR_PROPERTIES* pProps) override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE Receive(IMediaSample* pSample) override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE ReceiveMultiple(IMediaSample** pSamples, LONG nSamples, LONG* nSamplesProcessed) override { return E_OUTOFMEMORY; }
		HRESULT STDMETHODCALLTYPE ReceiveCanBlock() override { return E_OUTOFMEMORY; }
	};
	my_sink_pin sink;
	
	HRESULT STDMETHODCALLTYPE GetClassID(CLSID* pClassID) override { return E_OUTOFMEMORY; }
	HRESULT STDMETHODCALLTYPE Stop() override { return E_OUTOFMEMORY; }
	HRESULT STDMETHODCALLTYPE Pause() override { return E_OUTOFMEMORY; }
	HRESULT STDMETHODCALLTYPE Run(REFERENCE_TIME tStart) override { return E_OUTOFMEMORY; }
	HRESULT STDMETHODCALLTYPE GetState(DWORD dwMilliSecsTimeout, FILTER_STATE* State) override { return E_OUTOFMEMORY; }
	HRESULT STDMETHODCALLTYPE SetSyncSource(IReferenceClock* pClock) override { return E_OUTOFMEMORY; }
	HRESULT STDMETHODCALLTYPE GetSyncSource(IReferenceClock** pClock) override { return E_OUTOFMEMORY; }
	
	IPin* get_pin(size_t idx)
	{
		if (idx == 0)
			return &sink;
		return NULL;
	}
	HRESULT STDMETHODCALLTYPE EnumPins(IEnumPins** ppEnum) override
	{
		*ppEnum = new CComEnum<IEnumPins, &my_filter::get_pin>(this);
		return S_OK;
	}
	
	HRESULT STDMETHODCALLTYPE FindPin(LPCWSTR Id, IPin** ppPin) override
	{
		*ppPin = &sink;
		(*ppPin)->AddRef();
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE QueryFilterInfo(FILTER_INFO* pInfo) override { return E_OUTOFMEMORY; }
	HRESULT STDMETHODCALLTYPE JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) override { return E_OUTOFMEMORY; }
	HRESULT STDMETHODCALLTYPE QueryVendorInfo(LPWSTR* pVendorInfo) override { return E_OUTOFMEMORY; }
};

template<typename T, typename T2>
void assert_impl(T2* src, bool expect = true)
{
	IUnknown* dst;
	assert_eq(SUCCEEDED(src->QueryInterface(__uuidof(T), (void**)&dst)), expect);
	if (dst)
		dst->Release();
}

void init_ole32()
{
	if (!ole32.inited())
	{
		ole32.init("ole32.dll");
		ole32.CoInitializeEx(NULL, COINIT_MULTITHREADED|COINIT_DISABLE_OLE1DDE);
	}
}

template<bool my_own>
IBaseFilter* make_filter()
{
	if (my_own)
	{
		return new my_filter();
	}
	else
	{
		// also test against some existing COM object, to ensure that my expectations are correct
		IBaseFilter* ret;
		ole32.CoCreateInstance(CLSID_MPEG1Splitter, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&ret));
		return ret;
	}
}
IPin* get_pin(IBaseFilter* flt)
{
	IPin* ret = nullptr;
	flt->FindPin(L"Input", &ret);
	return ret;
}

template<bool my_own>
void the_test()
{
	init_ole32();
	{
		IBaseFilter* flt = make_filter<my_own>();
		assert_impl<IBaseFilter>(flt);
		assert_impl<IMediaFilter>(flt);
		assert_impl<IPersist>(flt);
		assert_impl<IUnknown>(flt);
		
		IPin* snk = get_pin(flt);
		assert_impl<IPin>(snk);
		assert_impl<IMemInputPin>(snk, my_own);
		assert_impl<IUnknown>(snk);
		
		assert_gt(flt->Release(), 0);
		assert_eq(snk->Release(), 0);
	}
	
	{
		IBaseFilter* flt = make_filter<my_own>();
		
		CComPtr<IEnumPins> pins;
		flt->EnumPins(&pins);
		
		CComPtr<IPin> pin;
		assert_eq(pins->Next(1, &pin, nullptr), S_OK);
		assert_ne((IPin*)pin, nullptr);
		assert_eq(pins->Next(1, &pin, nullptr), S_FALSE);
		assert_eq((IPin*)pin, nullptr);
		
		// should be kept alive by the IEnumPins (but not by the IPin pointer because it's empty)
		assert_gt(flt->Release(), 0);
	}
	
	{
		IBaseFilter* flt = make_filter<my_own>();
		IPin* snk = get_pin(flt);
		
		CComPtr<IEnumMediaTypes> mts;
		snk->EnumMediaTypes(&mts);
		
		AM_MEDIA_TYPE* mt[6] = {};
		ULONG n = 8;
		assert_eq(mts->Next(6, mt, &n), S_FALSE);
		if (n == 0 && !my_own)
			test_skip_force("needs Wine >= 9.8 https://gitlab.winehq.org/wine/wine/-/commit/545b1c67");
		assert_eq(n, 4);
		assert(mt[0] != NULL);
		assert(mt[1] != NULL);
		assert(mt[2] != NULL);
		assert(mt[3] != NULL);
		assert(mt[4] == NULL);
		assert(mt[5] == NULL);
		
		assert(IsEqualGUID(mt[0]->subtype, MEDIASUBTYPE_MPEG1System));
		assert(IsEqualGUID(mt[1]->subtype, MEDIASUBTYPE_MPEG1VideoCD));
		assert(IsEqualGUID(mt[2]->subtype, MEDIASUBTYPE_MPEG1Video));
		assert(IsEqualGUID(mt[3]->subtype, MEDIASUBTYPE_MPEG1Audio));
		
		assert_eq(mts->Next(6, mt, &n), S_FALSE);
		assert_eq(n, 0);
		assert(mt[0] != NULL);
		assert(mt[1] != NULL);
		assert(mt[2] != NULL);
		assert(mt[3] != NULL);
		assert(mt[4] == NULL);
		assert(mt[5] == NULL);
		
		assert(IsEqualGUID(mt[0]->subtype, MEDIASUBTYPE_MPEG1System));
		assert(IsEqualGUID(mt[1]->subtype, MEDIASUBTYPE_MPEG1VideoCD));
		assert(IsEqualGUID(mt[2]->subtype, MEDIASUBTYPE_MPEG1Video));
		assert(IsEqualGUID(mt[3]->subtype, MEDIASUBTYPE_MPEG1Audio));
		
		ole32.CoTaskMemFree(mt[0]);
		ole32.CoTaskMemFree(mt[1]);
		ole32.CoTaskMemFree(mt[2]);
		ole32.CoTaskMemFree(mt[3]);
		
		mts = nullptr;
		snk->Release();
		assert_eq(flt->Release(), 0);
	}
	
	{
		IBaseFilter* flt = make_filter<my_own>();
		IPin* snk = get_pin(flt);
		
		CComPtr<IEnumPins> pins;
		flt->EnumPins(&pins);
		assert_eq(pins->Skip(1), S_OK);
		assert_eq(pins->Skip(1), S_FALSE);
		
		CComPtr<IEnumMediaTypes> mts;
		snk->EnumMediaTypes(&mts);
		assert_eq(mts->Skip(1), S_OK);
		assert_eq(mts->Skip(1), S_OK);
		assert_eq(mts->Skip(1), S_OK);
		assert_eq(mts->Skip(1), S_OK);
		assert_eq(mts->Skip(1), S_FALSE);
		assert_eq(mts->Skip(1), S_FALSE);
		
		pins = nullptr;
		mts = nullptr;
		
		snk->Release();
		assert_eq(flt->Release(), 0);
	}
}
}

test("Microsoft COM - implementation", "", "") { the_test<true>(); }
test("Microsoft COM - the tests", "", "") { the_test<false>(); }
#endif
