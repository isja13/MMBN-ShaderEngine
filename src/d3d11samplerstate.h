#ifndef D3D11SAMPLERSTATE_H
#define D3D11SAMPLERSTATE_H

#include "main.h"
#include "d3d11devicechild.h"
#include <mutex>

class MyID3D11SamplerState : public ID3D11SamplerState {
    class Impl;
    Impl *impl;

public:
    ID3D11DEVICECHILD_DECL(ID3D11SamplerState);

    ID3D11SamplerState *&get_linear();
    ID3D11SamplerState *get_linear() const;

    MyID3D11SamplerState(
        ID3D11SamplerState **inner,
        const D3D11_SAMPLER_DESC *pDesc,
        ID3D11SamplerState *linear = NULL
    );

     ~MyID3D11SamplerState();
    D3D11_SAMPLER_DESC &get_desc();
    const D3D11_SAMPLER_DESC &get_desc() const;

    virtual void STDMETHODCALLTYPE GetDesc(
        D3D11_SAMPLER_DESC *pDesc
    );
};

extern std::unordered_map<ID3D11SamplerState *, MyID3D11SamplerState *> cached_sss_map;
extern std::mutex cached_sss_map_mutex;

#endif
