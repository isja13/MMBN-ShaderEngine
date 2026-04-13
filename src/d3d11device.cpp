#include "d3d11device.h"
#include "d3d11devicecontext.h"
#include "d3d11pixelshader.h"
#include "d3d11vertexshader.h"
#include "d3d11buffer.h"
#include "d3d11texture1d.h"
#include "d3d11texture2d.h"
#include "d3d11samplerstate.h"
#include "d3d11depthstencilstate.h"
#include "d3d11inputlayout.h"
#include "d3d11rendertargetview.h"
#include "d3d11shaderresourceview.h"
#include "d3d11depthstencilview.h"
#include "conf.h"
#include "log.h"
#include "overlay.h"
#include "tex.h"
#include "unknown_impl.h"
#include "../smhasher/MurmurHash3.h"
#include "../RetroArch/gfx/drivers/d3d11.h"
#include "../RetroArch/gfx/common/d3d11_common.h"
#include "../RetroArch/gfx/include/dxsdk/d3d11.h"
#include "half/include/half.hpp"
#include <d3dcompiler.h>

#define LOGGER default_logger
#define LOG_MFUN(_, ...) LOG_MFUN_DEF(MyID3D11Device, ## __VA_ARGS__)

#define TEX_FORMAT DXGI_FORMAT_R8G8B8A8_UNORM  

#define NOISE_WIDTH 256
#define NOISE_HEIGHT 256
#define GBA_WIDTH 240
#define GBA_HEIGHT 160

#define DS_WIDTH 256
#define DS_HEIGHT 192

#define GBA_WIDTH_FILTERED (GBA_WIDTH * 2)
#define GBA_HEIGHT_FILTERED (GBA_HEIGHT * 2)
#define DS_WIDTH_FILTERED (DS_WIDTH * 2)
#define DS_HEIGHT_FILTERED (DS_HEIGHT * 2)

#define PS_BYTECODE_LENGTH_T1_THRESHOLD 1000
#define PS_HASH_T1 0xdfeed8d5    // upscale/composite pass
#define PS_HASH_T2 0xa459320b   //gba -> composite
#define PS_HASH_T3 0x557a81f3  // screen mask

#define SO_B_LEN (sizeof(float) * 4 * 4 * 6 * 100)
#define MAX_SAMPLERS 16
#define MAX_SHADER_RESOURCES 128
#define MAX_CONSTANT_BUFFERS 15

namespace {

bool almost_equal(UINT a, UINT b) { return (a > b ? a - b : b - a) <= 1; }

UINT64 xorshift128p_state[2] = {};

bool xorshift128p_state_init() {
    if (!(
        QueryPerformanceCounter((LARGE_INTEGER *)&xorshift128p_state[0]) &&
        QueryPerformanceCounter((LARGE_INTEGER *)&xorshift128p_state[1]))
    ) {
        xorshift128p_state[0] = GetTickCount64();
        xorshift128p_state[1] = GetTickCount64();
    }
    return xorshift128p_state[0] && xorshift128p_state[1];
}

bool xorshift128p_state_init_status = xorshift128p_state_init();

void get_resolution_mul(
    UINT &render_width,
    UINT &render_height,
    UINT width,
    UINT height
) {
    UINT width_quo = render_width / width;
    UINT width_rem = render_width % width;
    if (width_rem) ++width_quo;
    UINT height_quo = render_height / height;
    UINT height_rem = render_height % height;
    if (height_rem) ++height_quo;
    render_width = width * width_quo;
    render_height = height * height_quo;
}

}

// From wikipedia
UINT64 xorshift128p() {
    UINT64 *s = xorshift128p_state;
    UINT64 a = s[0];
    UINT64 const b = s[1];
    s[0] = b;
    a ^= a << 23;       // a
    a ^= a >> 17;       // b
    a ^= b ^ (b >> 26); // c
    s[1] = a;
    return a + b;
}

class MyID3D11Device::Impl {
    friend class MyID3D11Device;
    friend class MyID3D11DeviceContext;

    IUNKNOWN_PRIV(ID3D11Device)
        MyID3D11DeviceContext* context = NULL;
    OverlayPtr overlay = { NULL };
    Config* config = NULL;

    d3d11_video_t* d3d11_gba = NULL;
    d3d11_video_t* d3d11_ds = NULL;

    UINT64 frame_count = 0;

    bool render_interp = false;
    bool render_linear = false;
    bool render_enhanced = false;
    UINT linear_test_width = 0;
    UINT linear_test_height = 0;

    void update_config() {
        if (!config) return;

        if (config->linear_test_updated) {
            config->begin_config();

            linear_test_width = config->linear_test_width;
            linear_test_height = config->linear_test_height;

            config->linear_test_updated = false;
            config->end_config();
        }

#define GET_SET_CONFIG_BOOL(v, m) do { \
    bool v = config->v; \
    if (render_ ## v != v) { \
        render_ ## v = v; \
        if (v) overlay(m " enabled"); else overlay(m " disabled"); \
    } \
} while (0)

        GET_SET_CONFIG_BOOL(interp, "Interp fix");
        GET_SET_CONFIG_BOOL(linear, "Force linear filtering");
        GET_SET_CONFIG_BOOL(enhanced, "Enhanced Type 1 filter");

#define SLANG_SHADERS \
    X(gba) \
    X(ds) \

#define X(v) \
    config->slang_shader_ ## v ## _updated ||

        if constexpr (ENABLE_SLANG_SHADER) {
            if (SLANG_SHADERS false) {

#undef X

#define X(v) \
    bool slang_shader_ ## v ## _updated = \
        config->slang_shader_ ## v ## _updated; \
    std::string slang_shader_ ## v; \
    if (slang_shader_ ## v ## _updated) { \
        slang_shader_ ## v = config->slang_shader_ ## v; \
        config->slang_shader_ ## v ## _updated = false; \
    }

                config->begin_config();

                SLANG_SHADERS

                    config->end_config();

#undef X

#define X(v) \
    if (slang_shader_ ## v ## _updated) { \
        if (!d3d11_ ## v) { \
            if (!(d3d11_ ## v = my_d3d11_gfx_init( \
                inner, \
                TEX_FORMAT \
            ))) { \
                overlay( \
                    "Failed to initialize slang shader " \
                    #v \
                ); \
            } \
        } \
        if (d3d11_ ## v) { \
            if (!slang_shader_ ## v.size()) { \
                my_d3d11_gfx_set_shader( \
                    d3d11_ ## v, \
                    NULL \
                ); \
                overlay("Slang shader " #v " disabled"); \
            } else if (my_d3d11_gfx_set_shader( \
                d3d11_ ## v, \
                slang_shader_ ## v.c_str() \
            )) { \
                overlay( \
                    "Slang shader " #v " set to ", \
                    slang_shader_ ## v \
                ); \
            } else { \
                overlay( \
                    "Failed to set slang shader " #v " to ", \
                    slang_shader_ ## v \
                ); \
            } \
        } \
    }

                SLANG_SHADERS

#undef X

#undef SLANG_SHADERS

            }
        }
    }

    void create_sampler(
        D3D11_FILTER filter,
        ID3D11SamplerState*& sampler,
        D3D11_TEXTURE_ADDRESS_MODE address =
        D3D11_TEXTURE_ADDRESS_CLAMP
    ) {
        D3D11_SAMPLER_DESC desc = {
            .Filter = filter,
            .AddressU = address,
            .AddressV = address,
            .AddressW = address,
            .MipLODBias = 0,
            .MaxAnisotropy = 16,
            .ComparisonFunc = D3D11_COMPARISON_NEVER,
            .BorderColor = {},
            .MinLOD = 0,
            .MaxLOD = D3D11_FLOAT32_MAX
        };
        inner->CreateSamplerState(&desc, &sampler);
    }

    void create_texture(
        UINT width,
        UINT height,
        ID3D11Texture2D*& texture,
        DXGI_FORMAT format = TEX_FORMAT,
        UINT bind_flags =
        D3D11_BIND_SHADER_RESOURCE |
        D3D11_BIND_RENDER_TARGET
    ) {
        D3D11_TEXTURE2D_DESC desc = {
            .Width = width,
            .Height = height,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = format,
            .SampleDesc = {.Count = 1, .Quality = 0},
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = bind_flags,
            .CPUAccessFlags = 0,
            .MiscFlags = 0
        };
        inner->CreateTexture2D(&desc, NULL, &texture);
    }

    void create_tex_and_views(
        UINT width,
        UINT height,
        TextureAndViews* tex
    ) {
        create_texture(width, height, tex->tex);
        create_srv(tex->tex, tex->srv);
        create_rtv(tex->tex, tex->rtv);
        tex->width = width;
        tex->height = height;
    }

    void create_rtv(
        ID3D11Texture2D* tex,
        ID3D11RenderTargetView*& rtv,
        DXGI_FORMAT format = TEX_FORMAT
    ) {
        D3D11_RENDER_TARGET_VIEW_DESC desc = {
            .Format = format,
            .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
        };
        desc.Texture2D.MipSlice = 0;
        inner->CreateRenderTargetView(tex, &desc, &rtv);
    }

    void create_srv(
        ID3D11Texture2D* tex,
        ID3D11ShaderResourceView*& srv,
        DXGI_FORMAT format = TEX_FORMAT
    ) {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc = {
            .Format = format,
            .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
        };
        D3D11_TEXTURE2D_DESC tex_desc;
        tex->GetDesc(&tex_desc);
        desc.Texture2D.MostDetailedMip = 0;
        desc.Texture2D.MipLevels = tex_desc.MipLevels;
        inner->CreateShaderResourceView(tex, &desc, &srv);
    }

    void create_dsv(
        ID3D11Texture2D* tex,
        ID3D11DepthStencilView*& dsv,
        DXGI_FORMAT format
    ) {
        D3D11_DEPTH_STENCIL_VIEW_DESC desc = {
            .Format = format,
            .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D,
        };
        desc.Texture2D.MipSlice = 0;
        inner->CreateDepthStencilView(tex, &desc, &dsv);
    }

    bool set_render_tex_views_and_update(
        MyID3D11Texture2D* tex,
        UINT width,
        UINT height,
        UINT orig_width,
        UINT orig_height,
        bool need_vp
    ) {
        if constexpr (ENABLE_CUSTOM_RESOLUTION) {
            if (
                need_vp &&
                need_render_vp &&
                (
                    render_width != width ||
                    render_height != height ||
                    render_orig_width != orig_width ||
                    render_orig_height != orig_height
                    )
                ) return false;
            if (
                !almost_equal(tex->get_orig_width(), orig_width) ||
                !almost_equal(tex->get_orig_height(), orig_height)
                ) return false;
            D3D11_TEXTURE2D_DESC desc = tex->get_desc();
            if (
                !almost_equal(desc.Width, width) ||
                !almost_equal(desc.Height, height)
                ) {
                if (tex->get_sc()) return false;

                desc.Width = width;
                desc.Height = height;
                auto& t = tex->get_inner();
                t->Release();
                inner->CreateTexture2D(&desc, NULL, &t);

                for (MyID3D11RenderTargetView* rtv : tex->get_rtvs()) {
                    auto& v = rtv->get_inner();
                    cached_rtvs_map.erase(v);
                    v->Release();
                    inner->CreateRenderTargetView(t, &rtv->get_desc(), &v);
                    cached_rtvs_map.emplace(v, rtv);
                }
                for (MyID3D11ShaderResourceView* srv : tex->get_srvs()) {
                    auto& v = srv->get_inner();
                    cached_srvs_map.erase(v);
                    v->Release();
                    inner->CreateShaderResourceView(t, &srv->get_desc(), &v);
                    cached_srvs_map.emplace(v, srv);
                }
                for (MyID3D11DepthStencilView* dsv : tex->get_dsvs()) {
                    auto& v = dsv->get_inner();
                    cached_dsvs_map.erase(v);
                    v->Release();
                    inner->CreateDepthStencilView(t, &dsv->get_desc(), &v);
                    cached_dsvs_map.emplace(v, dsv);
                }

                tex->get_desc() = desc;
            }
            if (
                almost_equal(width, orig_width) &&
                almost_equal(height, orig_height)
                ) return false;
            if (need_vp && !need_render_vp) {
                render_width = width;
                render_height = height;
                render_orig_width = orig_width;
                render_orig_height = orig_height;
                need_render_vp = true;
            }
            return true;
        }
        return false;
    }

    bool set_render_tex_views_and_update(
        ID3D11Resource* r,
        bool need_vp = false
    ) {
        if constexpr (ENABLE_CUSTOM_RESOLUTION) {
            D3D11_RESOURCE_DIMENSION type;
            if (
                r->GetType(&type),
                type != D3D11_RESOURCE_DIMENSION_TEXTURE2D
                ) return false;
            auto tex = as_wrapper<MyID3D11Texture2D, ID3D11Resource>(r);
            if (!tex) return false;
            bool ret = set_render_tex_views_and_update(
                tex,
                render_size.sc_width,
                render_size.sc_height,
                cached_size.sc_width,
                cached_size.sc_height,
                need_vp
            );
            return ret;
        }
        return false;
    }

    void create_tex_and_views_nn(
        TextureAndViews* tex,
        UINT width,
        UINT height
    ) {
        UINT render_width = render_size.render_width;
        UINT render_height = render_size.render_height;
        get_resolution_mul(
            render_width,
            render_height,
            width,
            height
        );
        create_tex_and_views(
            render_width,
            render_height,
            tex
        );
    }

    void create_tex_and_view_1(
        TextureViewsAndBuffer* tex,
        UINT render_width,
        UINT render_height,
        UINT width,
        UINT height
    ) {
        create_texture(render_width, render_height, tex->tex);
        create_srv(tex->tex, tex->srv);
        create_rtv(tex->tex, tex->rtv);
        tex->width = render_width;
        tex->height = render_height;
        float ps_cb_data[4] = {
            (float)width,
            (float)height,
            (float)(1.0 / width),
            (float)(1.0 / height)
        };
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = sizeof(ps_cb_data),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = 0,
            .MiscFlags = 0
        };
        D3D11_SUBRESOURCE_DATA data = {
            .pSysMem = ps_cb_data,
            .SysMemPitch = 0,
            .SysMemSlicePitch = 0
        };
        inner->CreateBuffer(&desc, &data, &tex->ps_cb);
    }

    void create_tex_and_view_1_v(
        std::vector<TextureViewsAndBuffer*>& tex_v,
        UINT width,
        UINT height
    ) {
        bool last = false;
        do {
            UINT width_next = width * 2;
            UINT height_next = height * 2;
            last =
                width_next >= render_size.render_width &&
                height_next >= render_size.render_height;
            TextureViewsAndBuffer* tex =
                new TextureViewsAndBuffer{};
            create_tex_and_view_1(
                tex,
                width_next,
                height_next,
                width,
                height
            );
            tex_v.push_back(tex);
            width = width_next;
            height = height_next;
        } while (!last);
    }

    void create_tex_and_depth_views_2(
        UINT width,
        UINT height,
        TextureAndDepthViews* tex
    ) {
        UINT render_width = render_size.render_width;
        UINT render_height = render_size.render_height;
        get_resolution_mul(
            render_width,
            render_height,
            width,
            height
        );
        create_tex_and_views(
            render_width,
            render_height,
            tex
        );
        create_texture(
            tex->width,
            tex->height,
            tex->tex_ds,
            DXGI_FORMAT_R24G8_TYPELESS,
            D3D11_BIND_DEPTH_STENCIL
        );
        create_dsv(
            tex->tex_ds,
            tex->dsv,
            DXGI_FORMAT_D24_UNORM_S8_UINT
        );
    }

    MyID3D11Device::FilterTemp filter_temp = {};

    void filter_temp_init() {
        filter_temp_shutdown();
        create_sampler(
            D3D11_FILTER_MIN_MAG_MIP_POINT,
            filter_temp.sampler_nn
        );
        create_sampler(
            D3D11_FILTER_MIN_MAG_MIP_LINEAR,
            filter_temp.sampler_linear
        );
        create_sampler(
            D3D11_FILTER_MIN_MAG_MIP_POINT,
            filter_temp.sampler_wrap,
            D3D11_TEXTURE_ADDRESS_WRAP
        );
        filter_temp.tex_nn_gba = new TextureAndViews{};
        create_tex_and_views_nn(
            filter_temp.tex_nn_gba,
            GBA_WIDTH,
            GBA_HEIGHT
        );
        filter_temp.tex_nn_ds = new TextureAndViews{};
        create_tex_and_views_nn(
            filter_temp.tex_nn_ds,
            DS_WIDTH,
            DS_HEIGHT
        );
        filter_temp.tex_t2 = new TextureAndDepthViews{};
        create_tex_and_depth_views_2(
            NOISE_WIDTH,
            NOISE_HEIGHT,
            filter_temp.tex_t2
        );
        create_tex_and_view_1_v(
            filter_temp.tex_1_gba,
            GBA_WIDTH_FILTERED,
            GBA_HEIGHT_FILTERED
        );
        create_tex_and_view_1_v(
            filter_temp.tex_1_ds,
            DS_WIDTH_FILTERED,
            DS_HEIGHT_FILTERED
        );
        // KEY PS: 1x1 staging texture for corner pixel readback
        {
            D3D11_TEXTURE2D_DESC sd = {};
            sd.Width = 1;
            sd.Height = 1;
            sd.MipLevels = 1;
            sd.ArraySize = 1;
            sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.SampleDesc.Count = 1;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            inner->CreateTexture2D(&sd, NULL, &filter_temp.key_staging);
        }

        // KEY PS: constant buffer (float3 key_color + float threshold = 16 bytes)
        {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = 16;
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            inner->CreateBuffer(&bd, NULL, &filter_temp.key_cb);
        }

        // KEY PS: blend state with BlendEnable=1 SRC_ALPHA/INV_SRC_ALPHA
        {
            D3D11_BLEND_DESC bd = {};
            bd.RenderTarget[0].BlendEnable = TRUE;
            bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            inner->CreateBlendState(&bd, &filter_temp.key_bs);
        }

        // KEY PS: compile inline HLSL
        {
            static const char* key_ps_src =
                "Texture2D t0 : register(t0);\n"
                "SamplerState s0 : register(s0);\n"
                "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
                "    float4 c = t0.Sample(s0, uv);\n"
                "    float4 key = t0.Sample(s0, float2(0.0, 0.0));\n"
                "    float diff = length(c.rgb - key.rgb);\n"
                "    if (diff < 0.15) discard;\n"
                "    return float4(c.rgb, 1.0);\n"
                "}\n";

            ID3DBlob* blob = nullptr;
            ID3DBlob* err = nullptr;
            HRESULT hr = D3DCompile(
                key_ps_src, strlen(key_ps_src),
                NULL, NULL, NULL,
                "main", "ps_5_0",
                0, 0, &blob, &err
            );
            if (SUCCEEDED(hr) && blob) {
                inner->CreatePixelShader(
                    blob->GetBufferPointer(),
                    blob->GetBufferSize(),
                    NULL,
                    &filter_temp.key_ps
                );
                blob->Release();
            }
            if (err) err->Release();
        }
    }

    void filter_temp_shutdown() {
        if (filter_temp.sampler_nn)
            filter_temp.sampler_nn->Release();
        if (filter_temp.sampler_linear)
            filter_temp.sampler_linear->Release();
        if (filter_temp.sampler_wrap)
            filter_temp.sampler_wrap->Release();
        if (filter_temp.tex_nn_gba)
            delete filter_temp.tex_nn_gba;
        if (filter_temp.tex_nn_ds)
            delete filter_temp.tex_nn_ds;
        if (filter_temp.tex_t2)
            delete filter_temp.tex_t2;
        for (auto tex : filter_temp.tex_1_gba)
            delete tex;
        for (auto tex : filter_temp.tex_1_ds)
            delete tex;
        if (filter_temp.key_staging) filter_temp.key_staging->Release();
        if (filter_temp.key_cb) filter_temp.key_cb->Release();
        if (filter_temp.key_ps) filter_temp.key_ps->Release();
        if (filter_temp.key_bs) filter_temp.key_bs->Release();
        filter_temp = {};
    }

    struct Size {
        UINT sc_width;
        UINT sc_height;
        UINT render_width;
        UINT render_height;
        void resize(UINT width, UINT height) {
            sc_width = width;
            sc_height = height;
            render_width = sc_height * 4 / 3;
            render_height = sc_height;
        }
    } cached_size = {}, render_size = {};

    UINT render_width = 0;
    UINT render_height = 0;
    UINT render_orig_width = 0;
    UINT render_orig_height = 0;
    bool need_render_vp = false;

    void present() {
        context->clear_filter();
        update_config();
        ++frame_count;
    }

    void resize_buffers(UINT width, UINT height) {
        render_size.resize(width, height);
        context->sync_render_size(width, height);
        context->sync_cached_size(cached_size.sc_width, cached_size.sc_height);
        context->clear_filter();
        update_config();
        filter_temp_init();
        frame_count = 0;
    }

    Impl(
        ID3D11Device** inner,
        UINT width,
        UINT height,
        MyID3D11Device* outer
    ) : IUNKNOWN_INIT(*inner)
    {
        ID3D11DeviceContext* inner_context = NULL;
        this->inner->GetImmediateContext(&inner_context);

        context = new MyID3D11DeviceContext(inner_context, outer);

        cached_size.resize(width, height);
        resize_buffers(width, height);
    }

    ~Impl() {
        filter_temp_shutdown();
        if (d3d11_gba) my_d3d11_gfx_free(d3d11_gba);
        if (d3d11_ds) my_d3d11_gfx_free(d3d11_ds);

        if (context) context->Release();
    }
};

d3d11_video_t* MyID3D11Device::get_d3d11_gba() const {
    return impl->d3d11_gba;
}

d3d11_video_t* MyID3D11Device::get_d3d11_ds() const {
    return impl->d3d11_ds;
}

UINT64 MyID3D11Device::get_frame_count() const {
    return impl->frame_count;
}

MyID3D11Device::FilterTemp& MyID3D11Device::get_filter_temp() {
    return impl->filter_temp;
}

void MyID3D11Device::set_overlay(Overlay *overlay) {
    impl->overlay = {overlay};
}
void MyID3D11Device::set_config(Config *config) {
    impl->config = config;
}

void MyID3D11Device::present() {
    impl->present();
}

void MyID3D11Device::resize_buffers(UINT width, UINT height) {
    impl->resize_buffers(width, height);
}

void MyID3D11Device::resize_orig_buffers(UINT width, UINT height) {
    impl->cached_size.resize(width, height);
    impl->context->sync_cached_size(
        impl->cached_size.sc_width,
        impl->cached_size.sc_height
    );
}

IUNKNOWN_IMPL_NO_QI(MyID3D11Device, ID3D11Device)

MyID3D11Device::MyID3D11Device(
    ID3D11Device **inner,
    UINT width,
    UINT height
) :
    impl(new Impl(inner, width, height, this))
{
    if (!xorshift128p_state_init_status) {
        void *key[] = {this, impl};
        MurmurHash3_x86_128(
            &key,
            sizeof(key),
            (uint32_t)(uintptr_t)*inner,
            xorshift128p_state
        );
        xorshift128p_state_init_status = true;
    }
    LOG_MFUN();
    *inner = this;
    register_wrapper(this);
}

MyID3D11Device::~MyID3D11Device() {
    unregister_wrapper(this);
    LOG_MFUN();
    delete impl;
}

#define ENUM_CLASS PIXEL_SHADER_ALPHA_DISCARD
const ENUM_MAP(ENUM_CLASS) PIXEL_SHADER_ALPHA_DISCARD_ENUM_MAP = {
    ENUM_CLASS_MAP_ITEM(UNKNOWN),
    ENUM_CLASS_MAP_ITEM(NONE),
    ENUM_CLASS_MAP_ITEM(EQUAL),
    ENUM_CLASS_MAP_ITEM(LESS),
    ENUM_CLASS_MAP_ITEM(LESS_OR_EQUAL),
};
#undef ENUM_CLASS

template<>
struct LogItem<PIXEL_SHADER_ALPHA_DISCARD> {
    PIXEL_SHADER_ALPHA_DISCARD a;
    void log_item(Logger *l) const {
        l->log_enum(PIXEL_SHADER_ALPHA_DISCARD_ENUM_MAP, a);
    }
};

bool MyID3D11Device::set_render_tex_views_and_update(
    ID3D11Resource* r,
    bool need_vp
) {
    return impl->set_render_tex_views_and_update(r, need_vp);
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::QueryInterface(
    REFIID riid,
    void** ppvObject
) {
    if (!ppvObject)
        return E_POINTER;

    *ppvObject = NULL;

    // Return wrapper for IUnknown
    if (riid == IID_IUnknown) {
        *ppvObject = static_cast<ID3D11Device*>(this);
        AddRef();
        LOG_MFUN(_, LOG_ARG(riid), LOG_ARG(*ppvObject), S_OK);
        return S_OK;
    }

    // Return wrapper for ID3D11Device
    static const IID IID_ID3D11Device_local =
    { 0xdb6f6ddb,0xac77,0x4e88,{0x82,0x53,0x81,0x9d,0xf9,0xbb,0xf1,0x40} };
    if (riid == IID_ID3D11Device_local) {
        *ppvObject = static_cast<ID3D11Device*>(this);
        AddRef();
        LOG_MFUN(_, LOG_ARG(riid), LOG_ARG(*ppvObject), S_OK);
        return S_OK;
    }

    // Forward ID3D11Device1 — Noesis requires it
    static const IID IID_Device1 =
    { 0xa04bfb29,0x08ef,0x43d6,{0xa4,0x9c,0xa9,0xbd,0xbd,0xcb,0xe6,0x86} };
    static const IID IID_Device2 =
    { 0x9d06dffa,0xd1e5,0x4d07,{0x83,0xa8,0x1b,0xb1,0x23,0xf2,0xf8,0x41} };
    if (riid == IID_Device1 || riid == IID_Device2) {
        HRESULT ret = impl->inner->QueryInterface(riid, ppvObject);
        if (ret == S_OK) {
            LOG_MFUN(_, LOG_ARG(riid), LOG_ARG(*ppvObject), ret);
        }
        else {
            LOG_MFUN(_, LOG_ARG(riid), ret);
        }
        return ret;
    }

    // Everything else — forward to inner
    HRESULT ret = impl->inner->QueryInterface(riid, ppvObject);
    if (ret == S_OK) {
        LOG_MFUN(_, LOG_ARG(riid), LOG_ARG(*ppvObject), ret);
    }
    else {
        LOG_MFUN(_, LOG_ARG(riid), ret);
    }
    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::GetDeviceRemovedReason(
) {
    LOG_MFUN();
    return impl->inner->GetDeviceRemovedReason(
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::SetExceptionMode(
    UINT RaiseFlags
) {
    LOG_MFUN();
    return impl->inner->SetExceptionMode(
        RaiseFlags
    );
}

UINT STDMETHODCALLTYPE MyID3D11Device::GetExceptionMode(
) {
    LOG_MFUN();
    return impl->inner->GetExceptionMode(
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::GetPrivateData(
    REFGUID guid,
    UINT *pDataSize,
    void *pData
) {
    LOG_MFUN();
    return impl->inner->GetPrivateData(
        guid,
        pDataSize,
        pData
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::SetPrivateData(
    REFGUID guid,
    UINT DataSize,
    const void *pData
) {
    LOG_MFUN();
    return impl->inner->SetPrivateData(
        guid,
        DataSize,
        pData
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::SetPrivateDataInterface(
    REFGUID guid,
    const IUnknown *pData
) {
    LOG_MFUN();
    return impl->inner->SetPrivateDataInterface(
        guid,
        pData
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateBuffer(
    const D3D11_BUFFER_DESC *pDesc,
    const D3D11_SUBRESOURCE_DATA *pInitialData,
    ID3D11Buffer **ppBuffer
) {
    HRESULT ret = impl->inner->CreateBuffer(
        pDesc,
        pInitialData,
        ppBuffer
    );
    if (ret == S_OK) {

        FILE* f = fopen("C:\\all_buffers.txt", "a");
        if (f) {
            fprintf(f, "CreateBuffer: inner=%p wrapper will be created\n", *ppBuffer);
            fflush(f);
            fclose(f);
        }
        new MyID3D11Buffer(
            ppBuffer,
            pDesc,
            xorshift128p(),
            pInitialData,
            impl->context
        );
        LOG_MFUN(_,
            LOG_ARG(pDesc),
            LOG_ARG(*ppBuffer),
            ret
        );
    } else {
        LOG_MFUN(_,
            LOG_ARG(pDesc),
            ret
        );
    }
    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateTexture1D(
    const D3D11_TEXTURE1D_DESC *pDesc,
    const D3D11_SUBRESOURCE_DATA *pInitialData,
    ID3D11Texture1D **ppTexture1D
) {
    HRESULT ret = impl->inner->CreateTexture1D(
        pDesc,
        pInitialData,
        ppTexture1D
    );
    if (ret == S_OK) {
        new MyID3D11Texture1D(ppTexture1D, pDesc, xorshift128p());
        LOG_MFUN(_,
            LOG_ARG(pDesc),
            LOG_ARG(*ppTexture1D),
            ret
        );
    } else {
        LOG_MFUN(_,
            LOG_ARG(pDesc),
            ret
        );
    }
    return ret;
}

static bool is_video_or_interop_texture(const D3D11_TEXTURE2D_DESC* d) {
    if (!d) return true;

    switch (d->Format) {
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
    case DXGI_FORMAT_420_OPAQUE:
    case DXGI_FORMAT_NV11:
    case DXGI_FORMAT_YUY2:
    case DXGI_FORMAT_Y210:
    case DXGI_FORMAT_Y216:
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_AI44:
    case DXGI_FORMAT_IA44:
    case DXGI_FORMAT_P8:
    case DXGI_FORMAT_A8P8:
        return true;
    default:
        break;
    }

#ifdef D3D11_BIND_DECODER
    if (d->BindFlags & D3D11_BIND_DECODER) return true;
#endif
#ifdef D3D11_BIND_VIDEO_ENCODER
    if (d->BindFlags & D3D11_BIND_VIDEO_ENCODER) return true;
#endif

    // Shared/keyed mutex surfaces are common interop targets.
    if (d->MiscFlags & D3D11_RESOURCE_MISC_SHARED) return true;
    if (d->MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX) return true;

    return false;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateTexture2D(
    const D3D11_TEXTURE2D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture2D** ppTexture2D
) {
    HRESULT ret = impl->inner->CreateTexture2D(pDesc, pInitialData, ppTexture2D);

    if (SUCCEEDED(ret) && ppTexture2D && *ppTexture2D) {
        if (!is_video_or_interop_texture(pDesc)) {
            new MyID3D11Texture2D(ppTexture2D, pDesc, xorshift128p());
        }

        LOG_MFUN(_, LOG_ARG(pDesc), LOG_ARG(*ppTexture2D), ret);
    }
    else {
        LOG_MFUN(_, LOG_ARG(pDesc), ret);
    }
    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateTexture3D(
    const D3D11_TEXTURE3D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture3D** ppTexture3D
) {
    return impl->inner->CreateTexture3D(pDesc, pInitialData, ppTexture3D);
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateShaderResourceView(
    ID3D11Resource* pResource,
    const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc,
    ID3D11ShaderResourceView** ppSRView
) {
    if (!ppSRView) return E_POINTER;
    *ppSRView = nullptr;

    D3D11_RESOURCE_DIMENSION type = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    if (pResource) {
        pResource->GetType(&type);
    }

    ID3D11Resource* resource_inner = pResource;
    MyID3D11Texture2D* texture_2d = nullptr;

    switch (type) {
    case D3D11_RESOURCE_DIMENSION_BUFFER:
        resource_inner = unwrap<MyID3D11Buffer>(pResource);
        break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        resource_inner = unwrap<MyID3D11Texture1D>(pResource);
        break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
        texture_2d = as_wrapper<MyID3D11Texture2D>(pResource);
        resource_inner = texture_2d ? texture_2d->get_inner() : pResource;
        break;
    default:
        resource_inner = pResource;
        break;
    }

    HRESULT ret = impl->inner->CreateShaderResourceView(resource_inner, pDesc, ppSRView);

    if (SUCCEEDED(ret) && *ppSRView && pResource) {
        if (type == D3D11_RESOURCE_DIMENSION_TEXTURE2D && !texture_2d && resource_inner == pResource) {
            LOG_MFUN(_, LOG_ARG(pResource), LOG_ARG(pDesc), "SKIP - invalid resource", ret);
            return ret;
        }

        MyID3D11ShaderResourceView* srv =
            new MyID3D11ShaderResourceView(ppSRView, pDesc, pResource);

        LOG_MFUN(_, LOG_ARG(pResource), LOG_ARG(pDesc), LOG_ARG(srv), ret);

        if (texture_2d) {
            texture_2d->get_srvs().insert(srv);
        }
    }
    else {
        LOG_MFUN(_, LOG_ARG_TYPE(pResource, MyID3D11Resource_Logger), LOG_ARG(pDesc), ret);
    }

    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateRenderTargetView(
    ID3D11Resource* pResource,
    const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
    ID3D11RenderTargetView** ppRTView
) {
    if (!ppRTView) return E_POINTER;
    *ppRTView = nullptr;

    D3D11_RESOURCE_DIMENSION type = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    if (pResource) {
        pResource->GetType(&type);
    }

    ID3D11Resource* resource_inner = pResource;
    MyID3D11Texture2D* texture_2d = nullptr;

    switch (type) {
    case D3D11_RESOURCE_DIMENSION_BUFFER:
        resource_inner = unwrap<MyID3D11Buffer>(pResource);
        break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        resource_inner = unwrap<MyID3D11Texture1D>(pResource);
        break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
        texture_2d = as_wrapper<MyID3D11Texture2D>(pResource);
        resource_inner = texture_2d ? texture_2d->get_inner() : pResource;
        break;
    default:
        resource_inner = pResource;
        break;
    }

    HRESULT ret = impl->inner->CreateRenderTargetView(resource_inner, pDesc, ppRTView);

    if (SUCCEEDED(ret) && *ppRTView && pResource) {
        MyID3D11RenderTargetView* rtv =
            new MyID3D11RenderTargetView(ppRTView, pDesc, pResource);

        LOG_MFUN(_, LOG_ARG_TYPE(pResource, MyID3D11Resource_Logger), LOG_ARG(pDesc), LOG_ARG(rtv), ret);

        if (texture_2d) {
            texture_2d->get_rtvs().insert(rtv);
        }
    }
    else {
        LOG_MFUN(_, LOG_ARG_TYPE(pResource, MyID3D11Resource_Logger), LOG_ARG(pDesc), ret);
    }

    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateDepthStencilView(
    ID3D11Resource* pResource,
    const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
    ID3D11DepthStencilView** ppDepthStencilView
) {
    if (!ppDepthStencilView) return E_POINTER;
    *ppDepthStencilView = nullptr;

    D3D11_RESOURCE_DIMENSION type = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    if (pResource) {
        pResource->GetType(&type);
    }

    ID3D11Resource* resource_inner = pResource;
    MyID3D11Texture2D* texture_2d = nullptr;

    switch (type) {
    case D3D11_RESOURCE_DIMENSION_BUFFER:
        resource_inner = unwrap<MyID3D11Buffer>(pResource);
        break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        resource_inner = unwrap<MyID3D11Texture1D>(pResource);
        break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
        texture_2d = as_wrapper<MyID3D11Texture2D>(pResource);
        resource_inner = texture_2d ? texture_2d->get_inner() : pResource;
        break;
    default:
        resource_inner = pResource;
        break;
    }

    HRESULT ret = impl->inner->CreateDepthStencilView(resource_inner, pDesc, ppDepthStencilView);

    if (SUCCEEDED(ret) && *ppDepthStencilView && pResource) {
        MyID3D11DepthStencilView* dsv =
            new MyID3D11DepthStencilView(ppDepthStencilView, pDesc, pResource);

        LOG_MFUN(_, LOG_ARG_TYPE(pResource, MyID3D11Resource_Logger), LOG_ARG(pDesc), LOG_ARG(dsv), ret);

        if (texture_2d) {
            texture_2d->get_dsvs().insert(dsv);
        }
    }
    else {
        LOG_MFUN(_, LOG_ARG_TYPE(pResource, MyID3D11Resource_Logger), LOG_ARG(pDesc), ret);
    }

    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateInputLayout(
    const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs,
    UINT NumElements,
    const void *pShaderBytecodeWithInputSignature,
    SIZE_T BytecodeLength,
    ID3D11InputLayout **ppInputLayout
) {
    HRESULT ret = impl->inner->CreateInputLayout(
        pInputElementDescs,
        NumElements,
        pShaderBytecodeWithInputSignature,
        BytecodeLength,
        ppInputLayout
    );
    if (ret == S_OK) {
        new MyID3D11InputLayout(
            ppInputLayout,
            pInputElementDescs,
            NumElements
        );
    }
    LOG_MFUN(_,
        LOG_ARG_TYPE(
            pInputElementDescs,
            ArrayLoggerRef,
            NumElements
        ),
        LogIf<1>{ret == S_OK},
        LOG_ARG_TYPE(
            ppInputLayout,
            ArrayLoggerDeref,
            NumElements
        ),
        ret
    );
    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateVertexShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11VertexShader** ppVertexShader
) {
    HRESULT ret = impl->inner->CreateVertexShader(
        pShaderBytecode,
        BytecodeLength,
        pClassLinkage,
        ppVertexShader
    );
    if (ret == S_OK) {
        ShaderLogger shader_source{pShaderBytecode};
        DWORD hash;
        MurmurHash3_x86_32(
            pShaderBytecode,
            BytecodeLength,
            0,
            &hash
        );
        ID3D11GeometryShader *pGeometryShader = NULL;
        std::vector<D3D11_SO_DECLARATION_ENTRY> decl_entries;
        HRESULT sogs_ret = 0;
if constexpr (ENABLE_LOGGER) {
        decl_entries.push_back(D3D11_SO_DECLARATION_ENTRY{
            .SemanticName = "SV_Position",
            .ComponentCount = 4,
        });
        std::string source = shader_source.source;
        std::regex texcoord_regex(R"(out\s+vec4\s+vs_TEXCOORD(\d+);)");
        std::smatch texcoord_sm;
        while (std::regex_search(
            source,
            texcoord_sm,
            texcoord_regex
        )) {
            decl_entries.push_back(D3D11_SO_DECLARATION_ENTRY{
                .SemanticName = "TEXCOORD",
                .SemanticIndex = std::stoul(texcoord_sm[1]),
                .ComponentCount = 4,
            });
            source = texcoord_sm.suffix();
        }
        sogs_ret = impl->inner->CreateGeometryShaderWithStreamOutput(
            pShaderBytecode,
            BytecodeLength,
            decl_entries.data(),
            decl_entries.size(),
            NULL,
            0,
            D3D11_SO_NO_RASTERIZED_STREAM,
            NULL,
            &pGeometryShader
        );
        if (sogs_ret != S_OK)
            pGeometryShader = NULL;
}
        new MyID3D11VertexShader(
            ppVertexShader,
            hash,
            BytecodeLength,
            shader_source.source,
            std::move(decl_entries),
            pGeometryShader
        );
        LOG_MFUN(_,
            LOG_ARG(sogs_ret),
            LOG_ARG(shader_source),
            LOG_ARG(BytecodeLength),
            LOG_ARG(*ppVertexShader),
            ret
        );
    } else {
        LOG_MFUN(_,
            LOG_ARG(BytecodeLength),
            ret
        );
    }
    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateGeometryShader(
    const void *pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11GeometryShader **ppGeometryShader
) {
    HRESULT ret = impl->inner->CreateGeometryShader(
        pShaderBytecode,
        BytecodeLength,
        pClassLinkage,
        ppGeometryShader
    );
    if (ret == S_OK) {
        LOG_MFUN(_,
            LOG_ARG_TYPE(pShaderBytecode, ShaderLogger),
            LOG_ARG(BytecodeLength),
            LOG_ARG(*ppGeometryShader),
            ret
        );
    } else {
        LOG_MFUN(_,
            LOG_ARG(BytecodeLength),
            ret
        );
    }
    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateGeometryShaderWithStreamOutput(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    const D3D11_SO_DECLARATION_ENTRY* pSODeclaration,
    UINT NumEntries,
    const UINT* pBufferStrides,
    UINT NumStrides,
    UINT RasterizedStream,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11GeometryShader** ppGeometryShader
) {
    LOG_MFUN();
    return impl->inner->CreateGeometryShaderWithStreamOutput(
        pShaderBytecode,
        BytecodeLength,
        pSODeclaration,
        NumEntries,
        pBufferStrides,
        NumStrides,
        RasterizedStream,
        pClassLinkage,
        ppGeometryShader
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreatePixelShader(
    const void *pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11PixelShader **ppPixelShader
) {
    HRESULT ret = impl->inner->CreatePixelShader(
        pShaderBytecode,
        BytecodeLength,
        pClassLinkage,
        ppPixelShader
    );
    if (ret == S_OK) {
        ShaderLogger shader_source{pShaderBytecode};
        DWORD hash;
        MurmurHash3_x86_32(
            pShaderBytecode,
            BytecodeLength,
            0,
            &hash
        );
        new MyID3D11PixelShader(
            ppPixelShader,
            hash,
            BytecodeLength,
            shader_source.source
        );
        LOG_MFUN(_,
            LOG_ARG(shader_source),
            LOG_ARG_TYPE(hash, NumHexLogger),
            LOG_ARG(BytecodeLength),
            LOG_ARG(*ppPixelShader),
            ret
        );
    } else {
        LOG_MFUN(_,
            LOG_ARG(BytecodeLength),
            ret
        );
    }
    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateBlendState(
    const D3D11_BLEND_DESC *pBlendStateDesc,
    ID3D11BlendState **ppBlendState
) {
    HRESULT ret = impl->inner->CreateBlendState(
        pBlendStateDesc,
        ppBlendState
    );
    if (ret == S_OK) {
        LOG_MFUN(_,
            LOG_ARG(pBlendStateDesc),
            LOG_ARG(*ppBlendState)
        );
    } else {
        LOG_MFUN(_,
            LOG_ARG(pBlendStateDesc)
        );
    }
    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateDepthStencilState(
    const D3D11_DEPTH_STENCIL_DESC *pDepthStencilDesc,
    ID3D11DepthStencilState **ppDepthStencilState
) {
    HRESULT ret = impl->inner->CreateDepthStencilState(
        pDepthStencilDesc,
        ppDepthStencilState
    );
    if (ret == S_OK) {
        new MyID3D11DepthStencilState(
            ppDepthStencilState,
            pDepthStencilDesc
        );
        LOG_MFUN(_,
            LOG_ARG(pDepthStencilDesc),
            LOG_ARG(*ppDepthStencilState)
        );
    } else {
        LOG_MFUN(_,
            LOG_ARG(pDepthStencilDesc)
        );
    }
    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateRasterizerState(
    const D3D11_RASTERIZER_DESC *pRasterizerDesc,
    ID3D11RasterizerState **ppRasterizerState
) {
    LOG_MFUN();
    return impl->inner->CreateRasterizerState(
        pRasterizerDesc,
        ppRasterizerState
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateSamplerState(
    const D3D11_SAMPLER_DESC *pSamplerDesc,
    ID3D11SamplerState **ppSamplerState
) {
    HRESULT ret = impl->inner->CreateSamplerState(
        pSamplerDesc,
        ppSamplerState
    );
    if (ret == S_OK && pSamplerDesc) {
        ID3D11SamplerState *linear;
        if (
            pSamplerDesc->Filter ==
                D3D11_FILTER_MIN_MAG_MIP_LINEAR
        ) {
            linear = *ppSamplerState;
            linear->AddRef();
        } else {
            D3D11_SAMPLER_DESC desc = *pSamplerDesc;
            desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            if (S_OK != impl->inner->CreateSamplerState(
                &desc,
                &linear
            )) {
                linear = NULL;
            }
        }
        new MyID3D11SamplerState(
            ppSamplerState,
            pSamplerDesc,
            linear
        );
        LOG_MFUN(_,
            LOG_ARG(pSamplerDesc),
            LOG_ARG(*ppSamplerState),
            ret
        );
    } else {
        LOG_MFUN(_,
            LOG_ARG(pSamplerDesc),
            ret
        );
    }
    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateQuery(
    const D3D11_QUERY_DESC *pQueryDesc,
    ID3D11Query **ppQuery
) {
    LOG_MFUN();
    return impl->inner->CreateQuery(
        pQueryDesc,
        ppQuery
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreatePredicate(
    const D3D11_QUERY_DESC *pPredicateDesc,
    ID3D11Predicate **ppPredicate
) {
    LOG_MFUN();
    return impl->inner->CreatePredicate(
        pPredicateDesc,
        ppPredicate
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateCounter(
    const D3D11_COUNTER_DESC *pCounterDesc,
    ID3D11Counter **ppCounter
) {
    LOG_MFUN();
    return impl->inner->CreateCounter(
        pCounterDesc,
        ppCounter
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CheckFormatSupport(
    DXGI_FORMAT Format,
    UINT *pFormatSupport
) {
    LOG_MFUN();
    return impl->inner->CheckFormatSupport(
        Format,
        pFormatSupport
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::
CheckMultisampleQualityLevels(
    DXGI_FORMAT Format,
    UINT SampleCount,
    UINT *pNumQualityLevels
) {
    LOG_MFUN();
    return impl->inner->CheckMultisampleQualityLevels(
        Format,
        SampleCount,
        pNumQualityLevels
    );
}

void STDMETHODCALLTYPE MyID3D11Device::CheckCounterInfo(
    D3D11_COUNTER_INFO *pCounterInfo
) {
    LOG_MFUN();
    impl->inner->CheckCounterInfo(
        pCounterInfo
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CheckCounter(
    const D3D11_COUNTER_DESC *pDesc,
    D3D11_COUNTER_TYPE *pType,
    UINT *pActiveCounters,
    char *name,
    UINT *pNameLength,
    char *units,
    UINT *pUnitsLength,
    char *description,
    UINT *pDescriptionLength
) {
    LOG_MFUN();
    return impl->inner->CheckCounter(
        pDesc,
        pType,
        pActiveCounters,
        name,
        pNameLength,
        units,
        pUnitsLength,
        description,
        pDescriptionLength
    );
}

UINT STDMETHODCALLTYPE MyID3D11Device::GetCreationFlags(
) {
    LOG_MFUN();
    return impl->inner->GetCreationFlags(
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::OpenSharedResource(
    HANDLE hResource,
    REFIID ReturnedInterface,
    void** ppResource
) {
    HRESULT ret = impl->inner->OpenSharedResource(
        hResource,
        ReturnedInterface,
        ppResource
    );

    if (ret == S_OK && ppResource && *ppResource) {
        static const IID IID_ID3D11Texture2D_local =
        { 0x6f15aaf2, 0xd208, 0x4e89, {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c} };

        if (ReturnedInterface == IID_ID3D11Texture2D_local) {
            ID3D11Texture2D* tex = (ID3D11Texture2D*)(*ppResource);

            D3D11_TEXTURE2D_DESC desc = {};
            tex->GetDesc(&desc);

            if (desc.Format != DXGI_FORMAT_NV12) {
                new MyID3D11Texture2D((ID3D11Texture2D**)ppResource, &desc, xorshift128p());
            }
        }
    }

    return ret;
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateUnorderedAccessView(
    ID3D11Resource* pResource,
    const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc,
    ID3D11UnorderedAccessView** ppUAView
) {
    return impl->inner->CreateUnorderedAccessView(pResource, pDesc, ppUAView);
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateHullShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11HullShader** ppHullShader
) {
    return impl->inner->CreateHullShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppHullShader);
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateDomainShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11DomainShader** ppDomainShader
) {
    return impl->inner->CreateDomainShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppDomainShader);
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateComputeShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11ComputeShader** ppComputeShader
) {
    return impl->inner->CreateComputeShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppComputeShader);
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateClassLinkage(
    ID3D11ClassLinkage** ppLinkage
) {
    return impl->inner->CreateClassLinkage(ppLinkage);
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CreateDeferredContext(
    UINT ContextFlags,
    ID3D11DeviceContext** ppDeferredContext
) {
    return impl->inner->CreateDeferredContext(ContextFlags, ppDeferredContext);
}

HRESULT STDMETHODCALLTYPE MyID3D11Device::CheckFeatureSupport(
    D3D11_FEATURE Feature,
    void* pFeatureSupportData,
    UINT FeatureSupportDataSize
) {
    return impl->inner->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
}

D3D_FEATURE_LEVEL STDMETHODCALLTYPE MyID3D11Device::GetFeatureLevel() {
    return impl->inner->GetFeatureLevel();
}

void STDMETHODCALLTYPE MyID3D11Device::GetImmediateContext(
    ID3D11DeviceContext** ppImmediateContext
) {
    if (!ppImmediateContext)
        return;
    *ppImmediateContext = impl->context;
    if (*ppImmediateContext)
        (*ppImmediateContext)->AddRef();
}

ID3D11DeviceContext* MyID3D11Device::get_context() const {
    return impl->context;
}