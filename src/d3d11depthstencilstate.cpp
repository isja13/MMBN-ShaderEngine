#include "d3d11depthstencilstate.h"
#include "d3d11devicechild_impl.h"
#include "log.h"

#define LOGGER default_logger
#define LOG_MFUN(_, ...) LOG_MFUN_DEF(MyID3D11DepthStencilState, ## __VA_ARGS__)

class MyID3D11DepthStencilState::Impl {
    friend class MyID3D11DepthStencilState;

    IUNKNOWN_PRIV(ID3D11DepthStencilState)

    D3D11_DEPTH_STENCIL_DESC desc = {};

    Impl(
        ID3D11DepthStencilState **inner,
        const D3D11_DEPTH_STENCIL_DESC *pDesc
    ) :
        IUNKNOWN_INIT(*inner),
        desc(*pDesc)
    {}

    ~Impl() {}
};

ID3D11DEVICECHILD_IMPL(MyID3D11DepthStencilState, ID3D11DepthStencilState)

D3D11_DEPTH_STENCIL_DESC &MyID3D11DepthStencilState::get_desc() {
    return impl->desc;
}

const D3D11_DEPTH_STENCIL_DESC &MyID3D11DepthStencilState::get_desc() const {
    return impl->desc;
}

MyID3D11DepthStencilState::MyID3D11DepthStencilState(
    ID3D11DepthStencilState** inner,
    const D3D11_DEPTH_STENCIL_DESC* pDesc
) :
    impl(new Impl(inner, pDesc))
{
    ID3D11DepthStencilState* real_dss = *inner;

    LOG_MFUN(_, LOG_ARG(real_dss));

    {
        std::lock_guard<std::mutex> lock(cached_dsss_map_mutex);
        cached_dsss_map.emplace(real_dss, this);
    }

    *inner = this;
    register_wrapper(this);
}

MyID3D11DepthStencilState::~MyID3D11DepthStencilState() {
    unregister_wrapper(this);
    LOG_MFUN();

    {
        std::lock_guard<std::mutex> lock(cached_dsss_map_mutex);
        cached_dsss_map.erase(impl->inner);
    }

    delete impl;
}

void STDMETHODCALLTYPE MyID3D11DepthStencilState::GetDesc(
    D3D11_DEPTH_STENCIL_DESC *pDesc
) {
    LOG_MFUN();
    if (pDesc) *pDesc = impl->desc;
}

std::unordered_map<ID3D11DepthStencilState*, MyID3D11DepthStencilState*> cached_dsss_map;
std::mutex cached_dsss_map_mutex;
