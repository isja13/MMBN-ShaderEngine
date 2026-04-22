#include "dxgiswapchain.h"
#include "d3d11device.h"
#include "overlay.h"
#include "d3d11texture2d.h"
#include "d3d11rendertargetview.h"
#include "d3d11shaderresourceview.h"
#include "d3d11depthstencilview.h"
#include "conf.h"
#include "log.h"
#include "tex.h"
#include "unknown_impl.h"
#include <initguid.h>

#define LOGGER default_logger
#define LOG_MFUN(_, ...) LOG_MFUN_DEF(MyIDXGISwapChain, ## __VA_ARGS__)

extern UINT64 xorshift128p();

class MyIDXGISwapChain::Impl {
    friend class MyIDXGISwapChain;

    IUNKNOWN_PRIV(IDXGISwapChain)

        UINT cached_width = 0;
    UINT cached_height = 0;
    UINT cached_flags = 0;
    UINT cached_buffer_count = 0;
    DXGI_FORMAT cached_format = DXGI_FORMAT_UNKNOWN;
    bool is_config_display = false;
    UINT display_width = 0;
    UINT display_height = 0;
    UINT display_flags = 0;
    UINT display_buffer_count = 0;
    DXGI_FORMAT display_format = DXGI_FORMAT_UNKNOWN;
    bool render_3d_updated = false;
    bool display_updated = false;
    OverlayPtr overlay = { NULL };
    Config* config = NULL;
    MyID3D11Device* device = NULL;
    ID3D11DeviceContext* context = NULL;
    std::unordered_set<MyID3D11Texture2D*> bbs;
    DXGI_SWAP_CHAIN_DESC desc = {};

    HRESULT my_resize_buffers(
        UINT width,
        UINT height,
        UINT flags,
        UINT buffer_count,
        DXGI_FORMAT format
    ) {
        HRESULT ret;
        if (overlay) {
            ret = overlay->resize_buffers(
                buffer_count,
                width,
                height,
                format,
                flags
            );
        }
        else {
            ret = inner->ResizeBuffers(
                buffer_count,
                width,
                height,
                format,
                flags
            );
        }
        if (ret == S_OK) {
            inner->GetDesc(&desc);
            if (device) device->resize_buffers(width, height);
        }
        return ret;
    }

    Impl(
        const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
        IDXGISwapChain** inner
    ) :
        IUNKNOWN_INIT(*inner),
        cached_width(pSwapChainDesc->BufferDesc.Width),
        cached_height(pSwapChainDesc->BufferDesc.Height),
        cached_flags(pSwapChainDesc->Flags),
        cached_buffer_count(pSwapChainDesc->BufferCount),
        cached_format(pSwapChainDesc->BufferDesc.Format),
        device(nullptr),
        context(nullptr),
        desc(*pSwapChainDesc)
    {
    }

    ~Impl() {
        for (auto b : bbs) { b->get_sc() = NULL; }
        if (overlay) overlay->set_display(NULL, NULL, NULL);
        if (device) device->Release();
    }

    void update_config() {
        if constexpr (ENABLE_CUSTOM_RESOLUTION) {
            if (config && config->render_display_updated) {
                config->begin_config();
                if (
                    display_width != config->display_width ||
                    display_height != config->display_height
                    ) {
                    display_width = config->display_width;
                    display_height = config->display_height;
                    display_updated = true;
                }
                config->render_display_updated = false;
                config->end_config();
            }

            if (
                (display_updated || is_config_display) &&
                (
                    display_flags != cached_flags ||
                    display_buffer_count != cached_buffer_count ||
                    display_format != cached_format
                    )
                ) {
                display_flags = cached_flags;
                display_buffer_count = cached_buffer_count;
                display_format = cached_format;
                display_updated = true;
            }

            if (display_updated) {
                context->OMSetRenderTargets(0, NULL, NULL);

                for (auto bb : bbs) {
                    for (MyID3D11RenderTargetView* rtv : bb->get_rtvs()) {
                        if (auto& v = rtv->get_inner()) {
                            cached_rtvs_map.erase(v);
                            v->Release();
                            v = NULL;
                        }
                    }
                    for (MyID3D11ShaderResourceView* srv : bb->get_srvs()) {
                        if (auto& v = srv->get_inner()) {
                            cached_srvs_map.erase(v);
                            v->Release();
                            v = NULL;
                        }
                    }
                    for (MyID3D11DepthStencilView* dsv : bb->get_dsvs()) {
                        if (auto& v = dsv->get_inner()) {
                            cached_dsvs_map.erase(v);
                            v->Release();
                            v = NULL;
                        }
                    }
                    if (auto& b = bb->get_inner()) {
                        b->Release();
                        b = NULL;
                    }
                }

                if (display_width && display_height) {
                    HRESULT ret = my_resize_buffers(
                        display_width,
                        display_height,
                        display_flags,
                        display_buffer_count,
                        display_format
                    );
                    if (ret == S_OK) {
                        is_config_display = true;
                        overlay(
                            "Display resolution set to ",
                            std::to_string(display_width),
                            "x",
                            std::to_string(display_height)
                        );
                    }
                    else {
                        overlay(
                            "Failed to set display resolution to ",
                            std::to_string(display_width),
                            "x",
                            std::to_string(display_height)
                        );
                    }
                }
                else {
                    my_resize_buffers(
                        cached_width,
                        cached_height,
                        cached_flags,
                        cached_buffer_count,
                        cached_format
                    );
                    overlay(
                        "Restoring display resolution to ",
                        std::to_string(cached_width),
                        "x",
                        std::to_string(cached_height)
                    );
                    is_config_display = false;
                }

                for (auto bb : bbs) {
                    auto& b = bb->get_inner();
                    auto d = device->get_inner();
                    inner->GetBuffer(
                        0,
                        IID_ID3D11Texture2D,
                        (void**)&b
                    );
                    for (MyID3D11RenderTargetView* rtv : bb->get_rtvs()) {
                        auto& v = rtv->get_inner();
                        d->CreateRenderTargetView(
                            b,
                            &rtv->get_desc(),
                            &v
                        );
                        cached_rtvs_map.emplace(v, rtv);
                    }
                    for (MyID3D11ShaderResourceView* srv : bb->get_srvs()) {
                        auto& v = srv->get_inner();
                        d->CreateShaderResourceView(
                            b,
                            &srv->get_desc(),
                            &v
                        );
                        cached_srvs_map.emplace(v, srv);
                    }
                    for (MyID3D11DepthStencilView* dsv : bb->get_dsvs()) {
                        auto& v = dsv->get_inner();
                        d->CreateDepthStencilView(
                            b,
                            &dsv->get_desc(),
                            &v
                        );
                        cached_dsvs_map.emplace(v, dsv);
                    }
                    b->GetDesc(&bb->get_desc());
                }

                display_updated = false;
            }
        }
    }

    void set_overlay(MyIDXGISwapChain* sc, Overlay* overlay) {
        this->overlay = { overlay };
        if (device) device->set_overlay(overlay);
        if (overlay) {
            if (display_width && display_height) {
                auto desc = this->desc;
                desc.BufferDesc.Width = display_width;
                desc.BufferDesc.Height = display_height;
                overlay->set_display(&desc, sc, device);
            }
            else {
                overlay->set_display(&desc, sc, device);
            }
        }
    }

    void set_config(Config* config) {
        this->config = config;
        update_config();
        if (device) device->set_config(config);
    }
};

IUNKNOWN_IMPL_NO_QI(MyIDXGISwapChain, IDXGISwapChain)

void MyIDXGISwapChain::set_overlay(Overlay* overlay) {
    impl->set_overlay(this, overlay);
}

void MyIDXGISwapChain::set_config(Config* config) {
    impl->set_config(config);
}

void MyIDXGISwapChain::set_device(MyID3D11Device* device) {
    if (impl->device) impl->device->Release();
    impl->device = device;
    impl->device->AddRef();
    impl->context = device->get_context();
}

std::unordered_set<MyID3D11Texture2D*>& MyIDXGISwapChain::get_bbs() {
    return impl->bbs;
}

MyIDXGISwapChain::MyIDXGISwapChain(
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** inner
) :
    impl(new Impl(pSwapChainDesc, inner))
{
    register_wrapper(this);
    LOG_MFUN(_, LOG_ARG(*inner));
    *inner = this;
}

MyIDXGISwapChain::~MyIDXGISwapChain() {
    unregister_wrapper(this);
    LOG_MFUN();
    auto tmp = impl;
    impl = nullptr;
    delete tmp;
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::Present(
    UINT sync_interval,
    UINT flags
) {
    HRESULT ret = 0;

    if (impl->device) impl->device->present();

    if (impl->overlay) {
        ret = impl->overlay->present(sync_interval, flags);
        if (ret == S_FALSE) {
            ret = impl->inner->Present(sync_interval, flags);
        }
    }
    else {
        ret = impl->inner->Present(sync_interval, flags);
    }

    LOG_MFUN(_, ret);
    LOGGER->next_frame();
    impl->update_config();
    return ret;
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::GetBuffer(
    UINT buffer_idx,
    REFIID riid,
    void** surface
) {
    if (!surface) return E_POINTER;
    *surface = nullptr;

    HRESULT ret = impl->inner->GetBuffer(buffer_idx, riid, surface);

    LOG_MFUN(_,
        LOG_ARG(buffer_idx),
        LOG_ARG(riid),
        LOG_ARG(surface ? *surface : nullptr),
        ret
    );

    return ret;
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::SetFullscreenState(
    WINBOOL fullscreen,
    IDXGIOutput* target
) {
    LOG_MFUN();
    return impl->inner->SetFullscreenState(fullscreen, target);
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::GetFullscreenState(
    WINBOOL* fullscreen,
    IDXGIOutput** target
) {
    LOG_MFUN();
    return impl->inner->GetFullscreenState(fullscreen, target);
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::GetDesc(
    DXGI_SWAP_CHAIN_DESC* desc
) {
    LOG_MFUN();
    if (!impl || !desc) return E_FAIL;
    *desc = impl->desc;
    desc->BufferDesc.Width = impl->cached_width;
    desc->BufferDesc.Height = impl->cached_height;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::ResizeBuffers(
    UINT buffer_count,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT flags
) {
    impl->cached_width = width;
    impl->cached_height = height;
    impl->cached_flags = flags;
    impl->cached_buffer_count = buffer_count;
    impl->cached_format = format;
    impl->update_config();

    HRESULT ret = impl->is_config_display ?
        S_OK :
        impl->my_resize_buffers(width, height, flags, buffer_count, format);

    if (ret == S_OK && impl->device) {
        impl->device->resize_orig_buffers(width, height);
    }

    LOG_MFUN(_,
        LOG_ARG(buffer_count),
        LOG_ARG(width),
        LOG_ARG(height),
        LOG_ARG(format),
        LOG_ARG(flags),
        ret
    );

    return ret;
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::ResizeTarget(
    const DXGI_MODE_DESC* target_mode_desc
) {
    LOG_MFUN();
    return impl->inner->ResizeTarget(target_mode_desc);
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::GetContainingOutput(
    IDXGIOutput** output
) {
    LOG_MFUN();
    return impl->inner->GetContainingOutput(output);
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::GetFrameStatistics(
    DXGI_FRAME_STATISTICS* stats
) {
    LOG_MFUN();
    return impl->inner->GetFrameStatistics(stats);
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::GetLastPresentCount(
    UINT* last_present_count
) {
    LOG_MFUN();
    return impl->inner->GetLastPresentCount(last_present_count);
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::GetDevice(
    REFIID riid,
    void** device
) {
    if (!device) return E_POINTER;
    *device = nullptr;

    if ((riid == IID_ID3D11Device || riid == IID_IUnknown) && impl && impl->device) {
        *device = static_cast<ID3D11Device*>(impl->device);
        ((IUnknown*)*device)->AddRef();
        return S_OK;
    }

    return impl->inner->GetDevice(riid, device);
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::SetPrivateData(
    REFGUID guid,
    UINT data_size,
    const void* data
) {
    LOG_MFUN();
    return impl->inner->SetPrivateData(guid, data_size, data);
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::SetPrivateDataInterface(
    REFGUID guid,
    const IUnknown* object
) {
    LOG_MFUN();
    return impl->inner->SetPrivateDataInterface(guid, object);
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::GetPrivateData(
    REFGUID guid,
    UINT* data_size,
    void* data
) {
    LOG_MFUN();
    return impl->inner->GetPrivateData(guid, data_size, data);
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::GetParent(
    REFIID riid,
    void** parent
) {
    LOG_MFUN();
    return impl->inner->GetParent(riid, parent);
}

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::QueryInterface(
    REFIID riid,
    void** ppvObject
) {
    if (!ppvObject) return E_POINTER;

    if (riid == IID_IUnknown ||
        riid == IID_IDXGIObject ||
        riid == IID_IDXGIDeviceSubObject ||
        riid == IID_IDXGISwapChain)
    {
        *ppvObject = static_cast<IDXGISwapChain*>(this);
        AddRef();
        return S_OK;
    }

    return impl->inner->QueryInterface(riid, ppvObject);
}