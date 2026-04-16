#ifndef D3D11VIEW_IMPL_H
#define D3D11VIEW_IMPL_H

#include "d3d11devicechild_impl.h"

#define ID3D11VIEW_PRIV \
    ID3D11Resource *resource = NULL; \

#define ID3D11VIEW_INIT(n) \
    resource(n)

#define ID3D11VIEW_IMPL(d, b) \
    ID3D11Resource *&d::get_resource() { \
        return impl->resource; \
    } \
 \
    ID3D11Resource *d::get_resource() const { \
        return impl->resource; \
    } \
 \
  void STDMETHODCALLTYPE d::GetResource( \
    ID3D11Resource **ppResource \
) { \
    LOG_MFUN(); \
    if (!ppResource) \
        return; \
    *ppResource = impl->resource; \
    if (*ppResource) \
        (*ppResource)->AddRef(); \
} \
 \
    ID3D11DEVICECHILD_IMPL(d, b)

#endif
