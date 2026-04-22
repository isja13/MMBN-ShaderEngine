#ifndef D3D11DEVICECHILD_IMPL_H
#define D3D11DEVICECHILD_IMPL_H

#include "unknown_impl.h"
#include "d3d11device.h"
#include "wrapper_registry.h"

#define ID3D11DEVICECHILD_IMPL(d, b) \
    void STDMETHODCALLTYPE d::GetDevice( \
        ID3D11Device **ppDevice \
    ) { \
        LOG_MFUN(); \
        if (!ppDevice) return; \
        *ppDevice = nullptr; \
        ID3D11Device* innerDev = nullptr; \
        impl->inner->GetDevice(&innerDev); \
        if (!innerDev) return; \
        auto wrappedDev = as_wrapper<MyID3D11Device>(innerDev); \
        if (wrappedDev) { \
            innerDev->Release(); \
            *ppDevice = wrappedDev; \
            wrappedDev->AddRef(); \
        } else { \
            *ppDevice = innerDev; \
        } \
    } \
 \
    HRESULT STDMETHODCALLTYPE d::GetPrivateData( \
        REFGUID guid, \
        UINT *pDataSize, \
        void *pData \
    ) { \
        LOG_MFUN(); \
        return impl->inner->GetPrivateData(guid, pDataSize, pData); \
    } \
 \
    HRESULT STDMETHODCALLTYPE d::SetPrivateData( \
        REFGUID guid, \
        UINT DataSize, \
        const void *pData \
    ) { \
        LOG_MFUN(); \
        return impl->inner->SetPrivateData(guid, DataSize, pData); \
    } \
 \
    HRESULT STDMETHODCALLTYPE d::SetPrivateDataInterface( \
        REFGUID guid, \
        const IUnknown *pData \
    ) { \
        LOG_MFUN(); \
        return impl->inner->SetPrivateDataInterface(guid, pData); \
    } \
 \
    IUNKNOWN_IMPL(d, b)

#endif