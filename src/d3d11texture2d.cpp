#include "d3d11texture2d.h"
#include "dxgiswapchain.h"
#include "d3d11resource_impl.h"
#include "d3d11device.h"
#include "log.h"

#define LOGGER default_logger
#define LOG_MFUN(_, ...) LOG_MFUN_DEF(MyID3D11Texture2D, ## __VA_ARGS__)

class MyID3D11Texture2D::Impl {
    friend class MyID3D11Texture2D;

    IUNKNOWN_PRIV(ID3D11Texture2D)
    ID3D11RESOURCE_PRIV
    D3D11_TEXTURE2D_DESC desc = {};
    UINT orig_width = 0;
    UINT orig_height = 0;
    std::unordered_set<MyID3D11RenderTargetView *> rtvs;
    std::unordered_set<MyID3D11ShaderResourceView *> srvs;
    std::unordered_set<MyID3D11DepthStencilView *> dsvs;
    MyIDXGISwapChain *sc = NULL;

    Impl(
        ID3D11Texture2D **inner,
        const D3D11_TEXTURE2D_DESC *pDesc,
        UINT64 id,
        MyIDXGISwapChain *sc
    ) :
        IUNKNOWN_INIT(*inner),
        ID3D11RESOURCE_INIT(id),
        desc(*pDesc),
        orig_width(pDesc->Width),
        orig_height(pDesc->Height),
        sc(sc)
    {}

    ~Impl() {}
};

//ID3D11RESOURCE_IMPL(MyID3D11Texture2D, ID3D11Texture2D, D3D11_RESOURCE_DIMENSION_TEXTURE2D)

UINT64 MyID3D11Texture2D::get_id() const {
    return impl->id;
}

void STDMETHODCALLTYPE MyID3D11Texture2D::GetType(
    D3D11_RESOURCE_DIMENSION* rType
) {
    // DON'T LOG HERE - called from D3D11 internals
    if (rType) *rType = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
}

void STDMETHODCALLTYPE MyID3D11Texture2D::SetEvictionPriority(
    UINT EvictionPriority
) {
    LOG_MFUN();
    impl->inner->SetEvictionPriority(EvictionPriority);
}

UINT STDMETHODCALLTYPE MyID3D11Texture2D::GetEvictionPriority() {
    LOG_MFUN();
    return impl->inner->GetEvictionPriority();
}

void STDMETHODCALLTYPE MyID3D11Texture2D::GetDevice(
    ID3D11Device** ppDevice
) {
    LOG_MFUN();
    if (!ppDevice) return;
    *ppDevice = nullptr;

    ID3D11Device* innerDev = nullptr;
    impl->inner->GetDevice(&innerDev);
    if (!innerDev) return;

    auto wrappedDev = as_wrapper<MyID3D11Device>(innerDev);
    if (wrappedDev) {
        innerDev->Release();
        *ppDevice = wrappedDev;
        wrappedDev->AddRef();
    }
    else {
        *ppDevice = innerDev;
    }
}

HRESULT STDMETHODCALLTYPE MyID3D11Texture2D::GetPrivateData(
    REFGUID guid,
    UINT* pDataSize,
    void* pData
) {
    LOG_MFUN();
    return impl->inner->GetPrivateData(guid, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE MyID3D11Texture2D::SetPrivateData(
    REFGUID guid,
    UINT DataSize,
    const void* pData
) {
    LOG_MFUN();
    return impl->inner->SetPrivateData(guid, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE MyID3D11Texture2D::SetPrivateDataInterface(
    REFGUID guid,
    const IUnknown* pData
) {
    LOG_MFUN();
    return impl->inner->SetPrivateDataInterface(guid, pData);
}

ID3D11Texture2D*& MyID3D11Texture2D::get_inner() {
    return impl->inner;
}

ULONG STDMETHODCALLTYPE MyID3D11Texture2D::AddRef() {
    impl->inner->AddRef();
    ULONG ret = (ULONG)InterlockedIncrement(&impl->wrapper_ref);
    LOG_MFUN(_, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE MyID3D11Texture2D::Release() {
    impl->inner->Release();
    ULONG ret = (ULONG)InterlockedDecrement(&impl->wrapper_ref);
    LOG_MFUN(_, ret);
    if (ret == 0) {
        delete this;
    }
    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Texture2D::QueryInterface(
    REFIID riid,
    void** ppvObject
) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;

    // Stage 3: wrapper only for D3D11 family
    if (riid == IID_IUnknown ||
        riid == IID_ID3D11DeviceChild ||
        riid == IID_ID3D11Resource ||
        riid == IID_ID3D11Texture2D)
    {
        *ppvObject = static_cast<ID3D11Texture2D*>(this);
        AddRef();
        LOG_MFUN(_, LOG_ARG(riid), LOG_ARG(*ppvObject), S_OK);
        return S_OK;
    }

    // DXGI / WinRT-facing (and anything else): raw forward
    HRESULT ret = impl->inner->QueryInterface(riid, ppvObject);
    if (ret == S_OK) {
        LOG_MFUN(_, LOG_ARG(riid), LOG_ARG(*ppvObject), ret);
    }
    else {
        LOG_MFUN(_, LOG_ARG(riid), ret);
    }
    return ret;
}

D3D11_TEXTURE2D_DESC &MyID3D11Texture2D::get_desc() {
    return impl->desc;
}

const D3D11_TEXTURE2D_DESC &MyID3D11Texture2D::get_desc() const {
    return impl->desc;
}

UINT &MyID3D11Texture2D::get_orig_width() {
    return impl->orig_width;
}

UINT &MyID3D11Texture2D::get_orig_height() {
    return impl->orig_height;
}

std::unordered_set<MyID3D11RenderTargetView *> &MyID3D11Texture2D::get_rtvs() {
    return impl->rtvs;
}

std::unordered_set<MyID3D11ShaderResourceView *> &MyID3D11Texture2D::get_srvs() {
    return impl->srvs;
}

std::unordered_set<MyID3D11DepthStencilView *> &MyID3D11Texture2D::get_dsvs() {
    return impl->dsvs;
}

MyIDXGISwapChain *&MyID3D11Texture2D::get_sc() {
    return impl->sc;
}

MyID3D11Texture2D::MyID3D11Texture2D(
    ID3D11Texture2D **inner,
    const D3D11_TEXTURE2D_DESC *pDesc,
    UINT64 id,
    MyIDXGISwapChain *sc
) :
    impl(new Impl(inner, pDesc, id, sc))
{
    LOG_MFUN(_,
        LOG_ARG(*inner),
        LOG_ARG_TYPE(id, NumHexLogger)
    );
    *inner = this;
    register_wrapper(this);
}

MyID3D11Texture2D::~MyID3D11Texture2D() {
    unregister_wrapper(this);
    LOG_MFUN();
    if (auto sc = impl->sc) { sc->get_bbs().erase(this); }
    delete impl;
}

void STDMETHODCALLTYPE MyID3D11Texture2D::GetDesc(
    D3D11_TEXTURE2D_DESC *pDesc
) {
    LOG_MFUN();
    if (pDesc) {
        *pDesc = impl->desc;
        pDesc->Width = impl->orig_width;
        pDesc->Height = impl->orig_height;
    }
}
