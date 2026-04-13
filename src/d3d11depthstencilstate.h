#ifndef D3D11DEPTHSTENCILSTATE_H
#define D3D11DEPTHSTENCILSTATE_H

#include "main.h"
#include "d3d11devicechild.h"
#include <mutex>

class MyID3D11DepthStencilState : public ID3D11DepthStencilState {
    class Impl;
    Impl *impl;

public:
    ID3D11DEVICECHILD_DECL(ID3D11DepthStencilState);

    MyID3D11DepthStencilState(
        ID3D11DepthStencilState **inner,
        const D3D11_DEPTH_STENCIL_DESC *pDesc
    );

     ~MyID3D11DepthStencilState();
    D3D11_DEPTH_STENCIL_DESC &get_desc();
    const D3D11_DEPTH_STENCIL_DESC &get_desc() const;

    virtual void STDMETHODCALLTYPE GetDesc(
        D3D11_DEPTH_STENCIL_DESC *pDesc
    );
};

extern std::unordered_map<ID3D11DepthStencilState *, MyID3D11DepthStencilState *> cached_dsss_map;
extern std::mutex cached_dsss_map_mutex;

#endif
