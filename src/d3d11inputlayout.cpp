#include "d3d11inputlayout.h"
#include "d3d11devicechild_impl.h"
#include "log.h"

#define LOGGER default_logger
#define LOG_MFUN(_, ...) LOG_MFUN_DEF(MyID3D11InputLayout, ## __VA_ARGS__)

class MyID3D11InputLayout::Impl {
    friend class MyID3D11InputLayout;

    IUNKNOWN_PRIV(ID3D11InputLayout)
    D3D11_INPUT_ELEMENT_DESC *descs = NULL;
    UINT descs_num = 0;

    Impl(
        ID3D11InputLayout **inner,
        const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs,
        UINT NumElements
    ) :
        IUNKNOWN_INIT(*inner),
        descs(new D3D11_INPUT_ELEMENT_DESC[NumElements]),
        descs_num(NumElements)
    {
        std::copy_n(pInputElementDescs, NumElements, descs);
    }

    ~Impl() { if (descs) delete[] descs; }
};

ID3D11DEVICECHILD_IMPL(MyID3D11InputLayout, ID3D11InputLayout)

UINT &MyID3D11InputLayout::get_descs_num() {
    return impl->descs_num;
}

UINT MyID3D11InputLayout::get_descs_num() const {
    return impl->descs_num;
}

D3D11_INPUT_ELEMENT_DESC *&MyID3D11InputLayout::get_descs() {
    return impl->descs;
}

D3D11_INPUT_ELEMENT_DESC *MyID3D11InputLayout::get_descs() const {
    return impl->descs;
}

MyID3D11InputLayout::MyID3D11InputLayout(
    ID3D11InputLayout **inner,
    const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs,
    UINT NumElements
) :
    impl(new Impl(inner, pInputElementDescs, NumElements))
{
    register_wrapper(this);
    LOG_MFUN(_,
        LOG_ARG(*inner)
    );
    {
        std::lock_guard<std::mutex> lock(cached_ils_map_mutex);
        cached_ils_map.emplace(*inner, this);
    }
    *inner = this;
}

MyID3D11InputLayout::~MyID3D11InputLayout() {
    unregister_wrapper(this);
    LOG_MFUN();
    {
        std::lock_guard<std::mutex> lock(cached_ils_map_mutex);
        cached_ils_map.erase(impl->inner);
    }
    delete impl;
}

std::unordered_map<ID3D11InputLayout*, MyID3D11InputLayout*> cached_ils_map;
std::mutex cached_ils_map_mutex;
