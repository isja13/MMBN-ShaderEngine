#ifndef D3D11VIEW_H
#define D3D11VIEW_H

#include "d3d11devicechild.h"

#define ID3D11VIEW_DECL(b) \
    ID3D11Resource *&get_resource();  \
    ID3D11Resource *get_resource() const;  \
 \
    virtual void STDMETHODCALLTYPE GetResource( \
        ID3D11Resource **ppResource \
    ); \
 \
    ID3D11DEVICECHILD_DECL(b)

#endif
