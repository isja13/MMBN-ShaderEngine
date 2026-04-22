#ifndef D3D11RENDERTARGETVIEW_H
#define D3D11RENDERTARGETVIEW_H

#include "main.h"
#include "unknown.h"
#include "d3d11view.h"
#include <mutex>

class MyID3D11RenderTargetView : public ID3D11RenderTargetView {
    class Impl;
    Impl *impl;

public:
    MyID3D11RenderTargetView(
        ID3D11RenderTargetView **inner,
        const D3D11_RENDER_TARGET_VIEW_DESC *pDesc,
        ID3D11Resource *resource
    );

     ~MyID3D11RenderTargetView();

    ID3D11VIEW_DECL(ID3D11RenderTargetView)
    D3D11_RENDER_TARGET_VIEW_DESC &get_desc();
    const D3D11_RENDER_TARGET_VIEW_DESC &get_desc() const;

    virtual void STDMETHODCALLTYPE GetDesc(
        D3D11_RENDER_TARGET_VIEW_DESC *pDesc
    );
};

extern std::unordered_map<ID3D11RenderTargetView *, MyID3D11RenderTargetView *> cached_rtvs_map;
extern std::unordered_set<MyID3D11RenderTargetView*> known_rtvs;
extern std::mutex cached_rtvs_map_mutex;
extern std::mutex known_rtvs_mutex;

#endif
