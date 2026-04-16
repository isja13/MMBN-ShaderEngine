#include "dxgiswapchain.h"
#include "d3d11device.h"
#include "d3d11devicecontext.h"
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

#define DRAWINDEXED_DIAG 0

#if DRAWINDEXED_DIAG
#define DIAG_LOG(fmt, ...) do { \
    FILE* f = fopen("drawindexed_diag.log", "a"); \
    if (f) { fprintf(f, fmt, ##__VA_ARGS__); fclose(f); } \
} while(0)
#else
#define DIAG_LOG(fmt, ...) do {} while(0)
#endif

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

    ID3D11Texture2D* bb_copy = NULL;
    ID3D11ShaderResourceView* bb_copy_srv = NULL;
    ID3D11RenderTargetView* bb_rtv = NULL;

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
            ensure_present_resources(width, height, format);
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
        if (bb_copy_srv) { bb_copy_srv->Release(); bb_copy_srv = NULL; }
        if (bb_copy) { bb_copy->Release();     bb_copy = NULL; }
        if (bb_rtv) { bb_rtv->Release();      bb_rtv = NULL; }
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

    void ensure_present_resources(UINT width, UINT height, DXGI_FORMAT fmt) {
        if (bb_copy_srv) { bb_copy_srv->Release(); bb_copy_srv = NULL; }
        if (bb_copy) { bb_copy->Release();     bb_copy = NULL; }
        if (bb_rtv) { bb_rtv->Release();      bb_rtv = NULL; }

        auto* dev = device->get_inner();

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = width;
        td.Height = height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = fmt;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        dev->CreateTexture2D(&td, NULL, &bb_copy);

        if (bb_copy) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format = fmt;
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MipLevels = 1;
            dev->CreateShaderResourceView(bb_copy, &sd, &bb_copy_srv);
        }

        ID3D11Texture2D* bb = NULL;
        inner->GetBuffer(0, IID_ID3D11Texture2D, (void**)&bb);
        if (bb) {
            dev->CreateRenderTargetView(bb, NULL, &bb_rtv);
            bb->Release();
        }
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

HRESULT STDMETHODCALLTYPE MyIDXGISwapChain::Present(UINT sync_interval, UINT flags) {
    HRESULT ret = 0;

    if (impl->device) {
        impl->device->present();

        if constexpr (ENABLE_SLANG_SHADER) {
            auto* my_ctx = impl->device->get_my_context();
            if (my_ctx) {
                auto& pss = my_ctx->get_present_shader_state();
                DIAG_LOG("[PRESENT_GATE] fired=%d\n",
                    (int)pss.fired
                );
                if (pss.fired) {
                    auto* d3d11 = pss.ds
                        ? impl->device->get_d3d11_ds()
                        : impl->device->get_d3d11_gba();

                    if (d3d11 && d3d11->shader_preset) {
                        UINT fw = impl->is_config_display
                            ? impl->display_width : impl->cached_width;
                        UINT fh = impl->is_config_display
                            ? impl->display_height : impl->cached_height;

                        if (!impl->bb_copy || !impl->bb_copy_srv || !impl->bb_rtv)
                            impl->ensure_present_resources(fw, fh, impl->cached_format);

                        if (impl->bb_copy && impl->bb_copy_srv && impl->bb_rtv) {
                            ID3D11Texture2D* bb = NULL;
                            impl->inner->GetBuffer(0, IID_ID3D11Texture2D, (void**)&bb);
                            if (bb) {
                                impl->context->CopyResource(impl->bb_copy, bb);
                                bb->Release();
                            }
                            DIAG_LOG("[PRESENT] fired=%d d3d11=%p shader_preset=%p bb_copy=%p bb_rtv=%p fw=%u fh=%u out=(%d,%d,%u,%u)\n",
                                (int)pss.fired,
                                (void*)d3d11,
                                d3d11 ? (void*)d3d11->shader_preset : NULL,
                                (void*)impl->bb_copy,
                                (void*)impl->bb_rtv,
                                fw, fh,
                                pss.out_x, pss.out_y, pss.out_w, pss.out_h
                            );
                            // Input texture is bb_copy — the fully composited frame
                            d3d11_texture_t texture = {};
                            texture.handle = impl->bb_copy;
                            // Dimensions are the swapchain dimensions — full composited frame
                            texture.desc.Width = fw;
                            texture.desc.Height = fh;
                            texture.desc.Format = impl->cached_format;
                            texture.desc.Usage = D3D11_USAGE_DEFAULT;

                            // Viewport output rect is the game region within the swapchain
                            video_viewport_t vp = {
                            .x = 0,
                            .y = 0,
                            .width = pss.sc_width,
                            .height = pss.sc_height,
                            .full_width = pss.sc_width,
                            .full_height = pss.sc_height
                            };
                            my_d3d11_update_viewport(d3d11, impl->bb_rtv, &vp);

                            UINT ns = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                            D3D11_RECT saved_sc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
                            impl->context->RSGetScissorRects(&ns, saved_sc);
                            D3D11_RECT scissor = {
                                0, 0, (LONG)pss.sc_width, (LONG)pss.sc_height };
                            impl->context->RSSetScissorRects(1, &scissor);

                            my_d3d11_gfx_frame(d3d11, &texture, impl->device->get_frame_count());

                            impl->context->RSSetScissorRects(ns, saved_sc);
                        }
                    }

                    pss.fired = false;
                    pss.out_x = pss.out_y = pss.out_w = pss.out_h = 0;
                }
            }
        }
    }

    if (impl->overlay) {
        ret = impl->overlay->present(sync_interval, flags);
        if (ret == S_FALSE)
            ret = impl->inner->Present(sync_interval, flags);
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

    /*
    if (FAILED(ret) || !*surface) return ret;

    if (riid == IID_ID3D11Texture2D) {
        D3D11_TEXTURE2D_DESC desc = {};
        ((ID3D11Texture2D*)*surface)->GetDesc(&desc);

        D3D11_TEXTURE2D_DESC desc_orig = {};
        desc_orig.Width = impl->cached_width;
        desc_orig.Height = impl->cached_height;

        auto bb = new MyID3D11Texture2D(
            (ID3D11Texture2D**)surface,
            &desc_orig,
            xorshift128p(),
            this
        );
        bb->get_desc() = desc;
        impl->bbs.insert(bb);
    }
    */

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