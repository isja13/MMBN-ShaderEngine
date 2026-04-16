#ifndef D3D11RESOURCE_IMPL_H
#define D3D11RESOURCE_IMPL_H

#include "d3d11devicechild_impl.h"

#define ID3D11RESOURCE_PRIV \
    UINT64 id = 0;

#define ID3D11RESOURCE_INIT(n) \
    id(n)

#define ID3D11RESOURCE_IMPL(d, b, t) \
    UINT64 d::get_id() const { \
        return impl->id; \
    } \
 \
    void STDMETHODCALLTYPE d::GetType( \
        D3D11_RESOURCE_DIMENSION *rType \
    ) { \
        /* DON'T LOG HERE - called from D3D11 internals */ \
        if (rType) *rType = t; \
    } \
 \
    void STDMETHODCALLTYPE d::SetEvictionPriority( \
        UINT EvictionPriority \
    ) { \
        LOG_MFUN(); \
        impl->inner->SetEvictionPriority(EvictionPriority); \
    } \
 \
    UINT STDMETHODCALLTYPE d::GetEvictionPriority() { \
        LOG_MFUN(); \
        return impl->inner->GetEvictionPriority(); \
    } \
 \
    ID3D11DEVICECHILD_IMPL(d, b)

#endif
