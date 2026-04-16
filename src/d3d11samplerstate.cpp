#include "d3d11samplerstate.h"
#include "d3d11devicechild_impl.h"
#include "log.h"

#define LOGGER default_logger
#define LOG_MFUN(_, ...) LOG_MFUN_DEF(MyID3D11SamplerState, ## __VA_ARGS__)

class MyID3D11SamplerState::Impl {
    friend class MyID3D11SamplerState;

    IUNKNOWN_PRIV(ID3D11SamplerState)

    D3D11_SAMPLER_DESC desc = {};
    ID3D11SamplerState *linear = NULL;

    Impl(
        ID3D11SamplerState** inner,
        const D3D11_SAMPLER_DESC* pDesc,
        ID3D11SamplerState* linear
    ) :
        IUNKNOWN_INIT(*inner),
        linear(linear)
    {
        if (pDesc) {
            desc = *pDesc;
        }
        else {
            (*inner)->GetDesc(&desc);
        }
    }

    ~Impl() { if (linear) linear->Release(); }
};

ID3D11DEVICECHILD_IMPL(MyID3D11SamplerState, ID3D11SamplerState)

D3D11_SAMPLER_DESC &MyID3D11SamplerState::get_desc() {
    return impl->desc;
}

const D3D11_SAMPLER_DESC &MyID3D11SamplerState::get_desc() const {
    return impl->desc;
}

MyID3D11SamplerState::MyID3D11SamplerState(
    ID3D11SamplerState** inner,
    const D3D11_SAMPLER_DESC* pDesc,
    ID3D11SamplerState* linear
) :
    impl(new Impl(inner, pDesc, linear))
{
    ID3D11SamplerState* real_ss = *inner;

    LOG_MFUN(_, LOG_ARG(real_ss));

    {
        std::lock_guard<std::mutex> lock(cached_sss_map_mutex);
        cached_sss_map.emplace(real_ss, this);
    }

    *inner = this;
    register_wrapper(this);
}

MyID3D11SamplerState::~MyID3D11SamplerState() {
    unregister_wrapper(this);
    LOG_MFUN();

    {
        std::lock_guard<std::mutex> lock(cached_sss_map_mutex);
        cached_sss_map.erase(impl->inner);
    }

    delete impl;
}

ID3D11SamplerState *&MyID3D11SamplerState::get_linear() {
    return impl->linear;
}

ID3D11SamplerState *MyID3D11SamplerState::get_linear() const {
    return impl->linear;
}

void STDMETHODCALLTYPE MyID3D11SamplerState::GetDesc(
    D3D11_SAMPLER_DESC *pDesc
) {
    LOG_MFUN();
    if (pDesc) *pDesc = impl->desc;
}

std::unordered_map<ID3D11SamplerState*, MyID3D11SamplerState*> cached_sss_map;
std::mutex cached_sss_map_mutex;
