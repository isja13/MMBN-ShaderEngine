#ifndef D3D11BUFFER_H
#define D3D11BUFFER_H

#include "main.h"
#include "d3d11resource.h"
#include <mutex>

class MyID3D11Buffer : public ID3D11Buffer {
    class Impl;
    Impl *impl;

public:
    MyID3D11Buffer(
        ID3D11Buffer** inner,
        const D3D11_BUFFER_DESC* pDesc,
        UINT64 id,
        const D3D11_SUBRESOURCE_DATA* pInitialData,
        ID3D11DeviceContext* context
    );

     ~MyID3D11Buffer();

    ID3D11RESOURCE_DECL(ID3D11Buffer)
    D3D11_BUFFER_DESC &get_desc();
    const D3D11_BUFFER_DESC &get_desc() const;
    bool &get_cached_state();
    bool get_cached_state() const;
    char *&get_cached();
    char *get_cached() const;

    virtual HRESULT STDMETHODCALLTYPE Map(
        D3D11_MAP MapType,
        UINT MapFlags,
        void **ppData
    );

    virtual void STDMETHODCALLTYPE Unmap(
    );

    virtual void STDMETHODCALLTYPE GetDesc(
        D3D11_BUFFER_DESC *pDesc
    );
};

extern std::unordered_map<ID3D11Buffer *, MyID3D11Buffer *> cached_bs_map;
extern std::mutex cached_bs_map_mutex;

#endif
