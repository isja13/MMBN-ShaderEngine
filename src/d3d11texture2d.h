#ifndef D3D11TEXTURE2D_H
#define D3D11TEXTURE2D_H

#include "main.h"
#include "d3d11resource.h"

class MyID3D11RenderTargetView;
class MyID3D11ShaderResourceView;
class MyID3D11DepthStencilView;
class MyIDXGISwapChain;

class MyID3D11Texture2D : public ID3D11Texture2D {
    class Impl;
    Impl *impl;

public:
    UINT &get_orig_width();
    UINT &get_orig_height();
    std::unordered_set<MyID3D11RenderTargetView *> &get_rtvs();
    std::unordered_set<MyID3D11ShaderResourceView *> &get_srvs();
    std::unordered_set<MyID3D11DepthStencilView *> &get_dsvs();
    MyIDXGISwapChain *&get_sc();

    MyID3D11Texture2D(
        ID3D11Texture2D **inner,
        const D3D11_TEXTURE2D_DESC *pDesc,
        UINT64 id,
        MyIDXGISwapChain *sc = NULL
    );

     ~MyID3D11Texture2D();
    D3D11_TEXTURE2D_DESC &get_desc();
    const D3D11_TEXTURE2D_DESC &get_desc() const;

    ID3D11RESOURCE_DECL(ID3D11Texture2D)

    virtual void STDMETHODCALLTYPE GetDesc(
        D3D11_TEXTURE2D_DESC *pDesc
    );
};

#endif
