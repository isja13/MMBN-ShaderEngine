#ifndef D3D11DEPTHSTENCILVIEW_H
#define D3D11DEPTHSTENCILVIEW_H

#include "main.h"
#include "d3d11view.h"
#include <mutex>

class MyID3D11DepthStencilView : public ID3D11DepthStencilView {
    class Impl;
    Impl *impl;

public:
    MyID3D11DepthStencilView(
        ID3D11DepthStencilView **inner,
        const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc,
        ID3D11Resource *resource
    );

     ~MyID3D11DepthStencilView();

    ID3D11VIEW_DECL(ID3D11DepthStencilView)
    D3D11_DEPTH_STENCIL_VIEW_DESC &get_desc();
    const D3D11_DEPTH_STENCIL_VIEW_DESC &get_desc() const;

    virtual void STDMETHODCALLTYPE GetDesc(
        D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc
    );
};

extern std::unordered_map<ID3D11DepthStencilView *, MyID3D11DepthStencilView *> cached_dsvs_map;
extern std::unordered_set<MyID3D11DepthStencilView*> known_dsvs;
extern std::mutex cached_dsvs_map_mutex;
extern std::mutex known_dsvs_mutex;

#endif
