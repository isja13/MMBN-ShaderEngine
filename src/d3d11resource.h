#ifndef D3D11RESOURCE_H
#define D3D11RESOURCE_H

#include "d3d11devicechild.h"

#define ID3D11RESOURCE_DECL(b) \
    UINT64 get_id() const; \
 \
    virtual void STDMETHODCALLTYPE GetType( \
        D3D11_RESOURCE_DIMENSION *rType \
    ); \
 \
    virtual void STDMETHODCALLTYPE SetEvictionPriority( \
        UINT EvictionPriority \
    ); \
 \
    virtual UINT STDMETHODCALLTYPE GetEvictionPriority(); \
 \
    ID3D11DEVICECHILD_DECL(b)

#endif
