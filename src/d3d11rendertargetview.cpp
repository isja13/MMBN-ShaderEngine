#include "d3d11rendertargetview.h"
#include "d3d11texture2d.h"
#include "d3d11view_impl.h"
#include "log.h"

#define LOGGER default_logger
#define LOG_MFUN(_, ...) LOG_MFUN_DEF(MyID3D11RenderTargetView, ## __VA_ARGS__)

class MyID3D11RenderTargetView::Impl {
    friend class MyID3D11RenderTargetView;

    IUNKNOWN_PRIV(ID3D11RenderTargetView)
    ID3D11VIEW_PRIV
    D3D11_RENDER_TARGET_VIEW_DESC desc;

    Impl(
        ID3D11RenderTargetView** inner,
        const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
        ID3D11Resource* resource
    ) :
        IUNKNOWN_INIT(*inner),
        ID3D11VIEW_INIT(resource),
        desc{}
    {
        if (pDesc) {
            desc = *pDesc;
        }
        else {
            (*inner)->GetDesc(&desc);
        }
        resource->AddRef();
    }

    ~Impl() {
        resource->Release();
    }
};

ID3D11VIEW_IMPL(MyID3D11RenderTargetView, ID3D11RenderTargetView)

D3D11_RENDER_TARGET_VIEW_DESC &MyID3D11RenderTargetView::get_desc() {
    return impl->desc;
}

const D3D11_RENDER_TARGET_VIEW_DESC &MyID3D11RenderTargetView::get_desc() const {
    return impl->desc;
}

MyID3D11RenderTargetView::MyID3D11RenderTargetView(
    ID3D11RenderTargetView **inner,
    const D3D11_RENDER_TARGET_VIEW_DESC *pDesc,
    ID3D11Resource *resource
) :
    impl(new Impl(inner, pDesc, resource))
{
    LOG_MFUN(_,
        LOG_ARG(*inner)
    );
    {
        std::lock_guard<std::mutex> lock(cached_rtvs_map_mutex);
        cached_rtvs_map[*inner] = this;
    }
    {
        std::lock_guard<std::mutex> lock(known_rtvs_mutex);
        known_rtvs.insert(this);
    }
    *inner = this;
    register_wrapper(this);
}

MyID3D11RenderTargetView::~MyID3D11RenderTargetView() {
    unregister_wrapper(this);
    LOG_MFUN();
    {
        std::lock_guard<std::mutex> lock(cached_rtvs_map_mutex);
        cached_rtvs_map.erase(impl->inner);
    }
    {
        std::lock_guard<std::mutex> lock(known_rtvs_mutex);
        known_rtvs.erase(this);
    }
    if (impl->desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D) {
        auto* tex = as_wrapper<MyID3D11Texture2D>(impl->resource);
       if (tex) tex->get_rtvs().erase(this);
    }
    delete impl;
}

void STDMETHODCALLTYPE MyID3D11RenderTargetView::GetDesc(
    D3D11_RENDER_TARGET_VIEW_DESC *pDesc
) {
    LOG_MFUN();
    if (pDesc) *pDesc = impl->desc;
}

std::unordered_map<ID3D11RenderTargetView*, MyID3D11RenderTargetView*> cached_rtvs_map;
std::unordered_set<MyID3D11RenderTargetView*> known_rtvs;
std::mutex cached_rtvs_map_mutex;
std::mutex known_rtvs_mutex;