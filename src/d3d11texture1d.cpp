#include "d3d11texture1d.h"
#include "d3d11resource_impl.h"
#include "log.h"

#define LOGGER default_logger
#define LOG_MFUN(_, ...) LOG_MFUN_DEF(MyID3D11Texture1D, ## __VA_ARGS__)

class MyID3D11Texture1D::Impl {
    friend class MyID3D11Texture1D;

    IUNKNOWN_PRIV(ID3D11Texture1D)
    ID3D11RESOURCE_PRIV
    D3D11_TEXTURE1D_DESC desc = {};

    Impl(
        ID3D11Texture1D **inner,
        const D3D11_TEXTURE1D_DESC *pDesc,
        UINT64 id
    ) :
        IUNKNOWN_INIT(*inner),
        ID3D11RESOURCE_INIT(id),
        desc(*pDesc)
    {}
};

ID3D11RESOURCE_IMPL(MyID3D11Texture1D, ID3D11Texture1D, D3D11_RESOURCE_DIMENSION_TEXTURE1D)

D3D11_TEXTURE1D_DESC &MyID3D11Texture1D::get_desc() {
    return impl->desc;
}

const D3D11_TEXTURE1D_DESC &MyID3D11Texture1D::get_desc() const {
    return impl->desc;
}

MyID3D11Texture1D::MyID3D11Texture1D(
    ID3D11Texture1D **inner,
    const D3D11_TEXTURE1D_DESC *pDesc,
    UINT64 id
) :
    impl(new Impl(inner, pDesc, id))
{
    LOG_MFUN(_,
        LOG_ARG(*inner),
        LOG_ARG_TYPE(id, NumHexLogger)
    );
    *inner = this;
    register_wrapper(this);
}

MyID3D11Texture1D::~MyID3D11Texture1D() {
    unregister_wrapper(this);
    LOG_MFUN();
    delete impl;
}

void STDMETHODCALLTYPE MyID3D11Texture1D::GetDesc(
    D3D11_TEXTURE1D_DESC *pDesc
) {
    LOG_MFUN();
    if (pDesc) *pDesc = impl->desc;
}
