#include "d3d11shaderresourceview.h"
#include "d3d11texture2d.h"
#include "d3d11view_impl.h"
#include "log.h"

#define LOGGER default_logger
#define LOG_MFUN(_, ...) LOG_MFUN_DEF(MyID3D11ShaderResourceView, ## __VA_ARGS__)

class MyID3D11ShaderResourceView::Impl {
    friend class MyID3D11ShaderResourceView;

    IUNKNOWN_PRIV(ID3D11ShaderResourceView)
    ID3D11VIEW_PRIV
    D3D11_SHADER_RESOURCE_VIEW_DESC desc;

    Impl(
        ID3D11ShaderResourceView** inner,
        const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc,
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

ID3D11VIEW_IMPL(MyID3D11ShaderResourceView, ID3D11ShaderResourceView)

D3D11_SHADER_RESOURCE_VIEW_DESC &MyID3D11ShaderResourceView::get_desc() {
    return impl->desc;
}

const D3D11_SHADER_RESOURCE_VIEW_DESC &MyID3D11ShaderResourceView::get_desc() const {
    return impl->desc;
}

MyID3D11ShaderResourceView::MyID3D11ShaderResourceView(
    ID3D11ShaderResourceView** inner,
    const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc,
    ID3D11Resource* resource
) :
    impl(new Impl(inner, pDesc, resource))
{

    // Cache the real pointer
    ID3D11ShaderResourceView* real_srv = *inner;

    LOG_MFUN(_,
        LOG_ARG(real_srv)
    );

    {
        std::lock_guard<std::mutex> lock(cached_srvs_map_mutex);
        cached_srvs_map[real_srv] = this;
    }

    // replace it
    *inner = this;
    register_wrapper(this);
}

MyID3D11ShaderResourceView::~MyID3D11ShaderResourceView() {
    unregister_wrapper(this);
    LOG_MFUN();
    {
        std::lock_guard<std::mutex> lock(cached_srvs_map_mutex);
        cached_srvs_map.erase(impl->inner);
    }
    if (impl->desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D) {
        auto* tex = as_wrapper<MyID3D11Texture2D>(impl->resource);
       if (tex) tex->get_srvs().erase(this);
    }
    delete impl;
}

void STDMETHODCALLTYPE MyID3D11ShaderResourceView::GetDesc(
    D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc
) {
    LOG_MFUN();
    if (pDesc) *pDesc = impl->desc;
}

std::unordered_map<ID3D11ShaderResourceView*, MyID3D11ShaderResourceView*> cached_srvs_map;
std::mutex cached_srvs_map_mutex;
