#ifndef D3D11SHADERRESOURCEVIEW_H
#define D3D11SHADERRESOURCEVIEW_H

#include "main.h"
#include "unknown.h"
#include "d3d11view.h"
#include <mutex>

class MyID3D11ShaderResourceView : public ID3D11ShaderResourceView {
    class Impl;
    Impl *impl;

public:
    MyID3D11ShaderResourceView(
        ID3D11ShaderResourceView **inner,
        const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc,
        ID3D11Resource *resource
    );

     ~MyID3D11ShaderResourceView();

    ID3D11VIEW_DECL(ID3D11ShaderResourceView)
    D3D11_SHADER_RESOURCE_VIEW_DESC &get_desc();
    const D3D11_SHADER_RESOURCE_VIEW_DESC &get_desc() const;

    virtual void STDMETHODCALLTYPE GetDesc(
        D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc
    );
};

extern std::unordered_map<ID3D11ShaderResourceView *, MyID3D11ShaderResourceView *> cached_srvs_map;
extern std::mutex cached_srvs_map_mutex;

#endif
