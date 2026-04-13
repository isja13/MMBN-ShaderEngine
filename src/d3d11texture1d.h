#ifndef D3D11TEXTURE1D_H
#define D3D11TEXTURE1D_H

#include "main.h"
#include "d3d11resource.h"

class MyID3D11Texture1D : public ID3D11Texture1D {
    class Impl;
    Impl *impl;

public:
    MyID3D11Texture1D(
        ID3D11Texture1D **inner,
        const D3D11_TEXTURE1D_DESC *pDesc,
        UINT64 id
    );

     ~MyID3D11Texture1D();

    ID3D11RESOURCE_DECL(ID3D11Texture1D)
    D3D11_TEXTURE1D_DESC &get_desc();
    const D3D11_TEXTURE1D_DESC &get_desc() const;

    virtual void STDMETHODCALLTYPE GetDesc(
        D3D11_TEXTURE1D_DESC *pDesc
    );
};

#endif
