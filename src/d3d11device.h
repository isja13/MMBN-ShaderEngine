#ifndef D3D11DEVICE_H
#define D3D11DEVICE_H

#include "main.h"
#include "unknown.h"
#include "../RetroArch/gfx/drivers/d3d11.h"

class Overlay;
class Config;

struct TextureAndViews;
struct TextureAndDepthViews;
struct TextureViewsAndBuffer;

class MyID3D11Device : public ID3D11Device {
    template<class T> friend struct LogItem;
    friend class MyID3D11DeviceContext;
    class Impl;
    Impl* impl;

public:
    MyID3D11Device(
        ID3D11Device** inner,
        UINT width,
        UINT height
    );

     ~MyID3D11Device();

    IUNKNOWN_DECL(ID3D11Device)

    void set_overlay(Overlay* overlay);
    void set_config(Config* config);

    void present();
    void resize_buffers(UINT width, UINT height);
    void resize_orig_buffers(UINT width, UINT height);

    ID3D11DeviceContext* get_context() const;
    Config* get_config() const;

    d3d11_video_t* get_d3d11_slot(int i) const;
    UINT64 get_frame_count() const;

    struct FilterTemp {
        ID3D11SamplerState* sampler_nn;
        ID3D11SamplerState* sampler_linear;
        ID3D11SamplerState* sampler_wrap;
        TextureAndViews* tex_nn_gba;
        TextureAndViews* tex_nn_ds;
        TextureAndDepthViews* tex_t2;
        std::vector<TextureViewsAndBuffer*> tex_1_gba;
        std::vector<TextureViewsAndBuffer*> tex_1_ds;
        // KEY PS resources
        ID3D11Texture2D* key_staging;       // 1x1 staging for corner readback
        ID3D11Buffer* key_cb;               // constant buffer for key color + threshold
        ID3D11PixelShader* key_ps;          // compiled key PS
        ID3D11BlendState* key_bs;           // blend state with BlendEnable=1
        ID3D11VertexShader* key_vs;         // widescreen text stretch VS
        ID3D11Buffer* key_vs_cb = nullptr;

    };
    FilterTemp& get_filter_temp();

    bool set_render_tex_views_and_update(ID3D11Resource* r, bool need_vp = false);
    // ID3D11Device

    virtual HRESULT STDMETHODCALLTYPE CreateBuffer(
        const D3D11_BUFFER_DESC* pDesc,
        const D3D11_SUBRESOURCE_DATA* pInitialData,
        ID3D11Buffer** ppBuffer
    );

    virtual HRESULT STDMETHODCALLTYPE CreateTexture1D(
        const D3D11_TEXTURE1D_DESC* pDesc,
        const D3D11_SUBRESOURCE_DATA* pInitialData,
        ID3D11Texture1D** ppTexture1D
    );

    virtual HRESULT STDMETHODCALLTYPE CreateTexture2D(
        const D3D11_TEXTURE2D_DESC* pDesc,
        const D3D11_SUBRESOURCE_DATA* pInitialData,
        ID3D11Texture2D** ppTexture2D
    );

    virtual HRESULT STDMETHODCALLTYPE CreateTexture3D(
        const D3D11_TEXTURE3D_DESC* pDesc,
        const D3D11_SUBRESOURCE_DATA* pInitialData,
        ID3D11Texture3D** ppTexture3D
    );

    virtual HRESULT STDMETHODCALLTYPE CreateShaderResourceView(
        ID3D11Resource* pResource,
        const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc,
        ID3D11ShaderResourceView** ppSRView
    );

    virtual HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(
        ID3D11Resource* pResource,
        const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc,
        ID3D11UnorderedAccessView** ppUAView
    );

    virtual HRESULT STDMETHODCALLTYPE CreateRenderTargetView(
        ID3D11Resource* pResource,
        const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
        ID3D11RenderTargetView** ppRTView
    );

    virtual HRESULT STDMETHODCALLTYPE CreateDepthStencilView(
        ID3D11Resource* pResource,
        const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
        ID3D11DepthStencilView** ppDepthStencilView
    );

    virtual HRESULT STDMETHODCALLTYPE CreateInputLayout(
        const D3D11_INPUT_ELEMENT_DESC* pInputElementDescs,
        UINT NumElements,
        const void* pShaderBytecodeWithInputSignature,
        SIZE_T BytecodeLength,
        ID3D11InputLayout** ppInputLayout
    );

    virtual HRESULT STDMETHODCALLTYPE CreateVertexShader(
        const void* pShaderBytecode,
        SIZE_T BytecodeLength,
        ID3D11ClassLinkage* pClassLinkage,
        ID3D11VertexShader** ppVertexShader
    );

    virtual HRESULT STDMETHODCALLTYPE CreateGeometryShader(
        const void* pShaderBytecode,
        SIZE_T BytecodeLength,
        ID3D11ClassLinkage* pClassLinkage,
        ID3D11GeometryShader** ppGeometryShader
    );

    virtual HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(
        const void* pShaderBytecode,
        SIZE_T BytecodeLength,
        const D3D11_SO_DECLARATION_ENTRY* pSODeclaration,
        UINT NumEntries,
        const UINT* pBufferStrides,
        UINT NumStrides,
        UINT RasterizedStream,
        ID3D11ClassLinkage* pClassLinkage,
        ID3D11GeometryShader** ppGeometryShader
    );

    virtual HRESULT STDMETHODCALLTYPE CreatePixelShader(
        const void* pShaderBytecode,
        SIZE_T BytecodeLength,
        ID3D11ClassLinkage* pClassLinkage,
        ID3D11PixelShader** ppPixelShader
    );

    virtual HRESULT STDMETHODCALLTYPE CreateHullShader(
        const void* pShaderBytecode,
        SIZE_T BytecodeLength,
        ID3D11ClassLinkage* pClassLinkage,
        ID3D11HullShader** ppHullShader
    );

    virtual HRESULT STDMETHODCALLTYPE CreateDomainShader(
        const void* pShaderBytecode,
        SIZE_T BytecodeLength,
        ID3D11ClassLinkage* pClassLinkage,
        ID3D11DomainShader** ppDomainShader
    );

    virtual HRESULT STDMETHODCALLTYPE CreateComputeShader(
        const void* pShaderBytecode,
        SIZE_T BytecodeLength,
        ID3D11ClassLinkage* pClassLinkage,
        ID3D11ComputeShader** ppComputeShader
    );

    virtual HRESULT STDMETHODCALLTYPE CreateClassLinkage(
        ID3D11ClassLinkage** ppLinkage
    );

    virtual HRESULT STDMETHODCALLTYPE CreateBlendState(
        const D3D11_BLEND_DESC* pBlendStateDesc,
        ID3D11BlendState** ppBlendState
    );

    virtual HRESULT STDMETHODCALLTYPE CreateDepthStencilState(
        const D3D11_DEPTH_STENCIL_DESC* pDepthStencilDesc,
        ID3D11DepthStencilState** ppDepthStencilState
    );

    virtual HRESULT STDMETHODCALLTYPE CreateRasterizerState(
        const D3D11_RASTERIZER_DESC* pRasterizerDesc,
        ID3D11RasterizerState** ppRasterizerState
    );

    virtual HRESULT STDMETHODCALLTYPE CreateSamplerState(
        const D3D11_SAMPLER_DESC* pSamplerDesc,
        ID3D11SamplerState** ppSamplerState
    );

    virtual HRESULT STDMETHODCALLTYPE CreateQuery(
        const D3D11_QUERY_DESC* pQueryDesc,
        ID3D11Query** ppQuery
    );

    virtual HRESULT STDMETHODCALLTYPE CreatePredicate(
        const D3D11_QUERY_DESC* pPredicateDesc,
        ID3D11Predicate** ppPredicate
    );

    virtual HRESULT STDMETHODCALLTYPE CreateCounter(
        const D3D11_COUNTER_DESC* pCounterDesc,
        ID3D11Counter** ppCounter
    );

    virtual HRESULT STDMETHODCALLTYPE CreateDeferredContext(
        UINT ContextFlags,
        ID3D11DeviceContext** ppDeferredContext
    );

    virtual HRESULT STDMETHODCALLTYPE OpenSharedResource(
        HANDLE hResource,
        REFIID ReturnedInterface,
        void** ppResource
    );

    virtual HRESULT STDMETHODCALLTYPE CheckFormatSupport(
        DXGI_FORMAT Format,
        UINT* pFormatSupport
    );

    virtual HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(
        DXGI_FORMAT Format,
        UINT SampleCount,
        UINT* pNumQualityLevels
    );

    virtual void STDMETHODCALLTYPE CheckCounterInfo(
        D3D11_COUNTER_INFO* pCounterInfo
    );

    virtual HRESULT STDMETHODCALLTYPE CheckCounter(
        const D3D11_COUNTER_DESC* pDesc,
        D3D11_COUNTER_TYPE* pType,
        UINT* pActiveCounters,
        char* name,
        UINT* pNameLength,
        char* units,
        UINT* pUnitsLength,
        char* description,
        UINT* pDescriptionLength
    );

    virtual HRESULT STDMETHODCALLTYPE CheckFeatureSupport(
        D3D11_FEATURE Feature,
        void* pFeatureSupportData,
        UINT FeatureSupportDataSize
    );

    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(
        REFGUID guid,
        UINT* pDataSize,
        void* pData
    );

    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(
        REFGUID guid,
        UINT DataSize,
        const void* pData
    );

    virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
        REFGUID guid,
        const IUnknown* pData
    );

    virtual D3D_FEATURE_LEVEL STDMETHODCALLTYPE GetFeatureLevel();

    virtual UINT STDMETHODCALLTYPE GetCreationFlags();

    virtual HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason();

    virtual void STDMETHODCALLTYPE GetImmediateContext(
        ID3D11DeviceContext** ppImmediateContext
    );

    virtual HRESULT STDMETHODCALLTYPE SetExceptionMode(
        UINT RaiseFlags
    );

    virtual UINT STDMETHODCALLTYPE GetExceptionMode();
};

#endif