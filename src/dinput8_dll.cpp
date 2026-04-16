#include <windows.h>
#include <dxgi.h>
#include <d3d11.h>

#include "dinput8_dll.h"
#include "dxgiswapchain.h"
#include "overlay.h"
#include "conf.h"
#include "log.h"
#include "../minhook/include/MinHook.h"
#include <string.h>
#include "d3d11device.h"

#define LOGGER default_logger
#define DEFINE_PROC(r, n, v) \
    typedef r (__stdcall *n ## _t) v; \
    n ## _t p ## n; \
    extern "C" r __stdcall n v

DEFINE_PROC(HRESULT, CreateDXGIFactory, (
    REFIID riid,
    void** ppFactory
    )) {
    HRESULT ret = pCreateDXGIFactory ?
        pCreateDXGIFactory(riid, ppFactory) :
        E_NOTIMPL;
    LOG_FUN(_, ret);
    return ret;
}

DEFINE_PROC(HRESULT, CreateDXGIFactory2, (
    UINT Flags,
    REFIID riid,
    void** ppFactory
    )) {
    HRESULT ret = pCreateDXGIFactory2 ?
        pCreateDXGIFactory2(Flags, riid, ppFactory) :
        E_NOTIMPL;
    LOG_FUN(_, ret);
    return ret;
}

DEFINE_PROC(HRESULT, D3D11CreateDeviceAndSwapChain, (
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext
    )) {
    if (!pD3D11CreateDeviceAndSwapChain) {
        LOG_FUN(_, E_NOTIMPL);
        return E_NOTIMPL;
    }

    HRESULT ret = pD3D11CreateDeviceAndSwapChain(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        pSwapChainDesc, ppSwapChain, ppDevice,
        pFeatureLevel, ppImmediateContext
    );

    if (SUCCEEDED(ret) && ppSwapChain && *ppSwapChain) {
        DXGI_SWAP_CHAIN_DESC actual_desc = {};
        if (FAILED((*ppSwapChain)->GetDesc(&actual_desc))) {
            ZeroMemory(&actual_desc, sizeof(actual_desc));
            if (pSwapChainDesc) actual_desc = *pSwapChainDesc;
        }

        if (actual_desc.BufferDesc.Width == 0 || actual_desc.BufferDesc.Height == 0) {
            ID3D11Texture2D* pBackBuffer = nullptr;
            if (SUCCEEDED((*ppSwapChain)->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer)) && pBackBuffer) {
                D3D11_TEXTURE2D_DESC bbDesc = {};
                pBackBuffer->GetDesc(&bbDesc);
                actual_desc.BufferDesc.Width = bbDesc.Width;
                actual_desc.BufferDesc.Height = bbDesc.Height;
                pBackBuffer->Release();
            }
        }

        auto sc = new MyIDXGISwapChain(&actual_desc, ppSwapChain);

        if (ppDevice && *ppDevice) {
            new MyID3D11Device(ppDevice, actual_desc.BufferDesc.Width, actual_desc.BufferDesc.Height);

            if (ppImmediateContext && *ppImmediateContext) {
                ((IUnknown*)(*ppImmediateContext))->Release();
                *ppImmediateContext = ((MyID3D11Device*)*ppDevice)->get_context();
                (*ppImmediateContext)->AddRef();
            }

            sc->set_device((MyID3D11Device*)*ppDevice);
        }

        default_config->hwnd = actual_desc.OutputWindow;
        sc->set_overlay(default_overlay);
        sc->set_config(default_config);
    }

    LOG_FUN(_, ret);
    return ret;
}

static decltype(&TerminateProcess) pTerminateProcess = NULL;
static BOOL WINAPI MyTerminateProcess(HANDLE h, UINT code) {
    FILE* f = fopen("C:\\crash.log", "w");
    if (f) {
        fprintf(f, "TerminateProcess called with code: %u\n", code);
        fclose(f);
    }
    return pTerminateProcess(h, code);
}

namespace {

    HMODULE base_dll;
    bool MinHook_Initialized;

    void minhook_init() {
        if (MH_Initialize() != MH_OK) {
        }
        else {
            MinHook_Initialized = true;
#define HOOK_PROC(m, n) do { \
    LPVOID pTarget; \
    if (MH_CreateHookApiEx( \
        L ## #m, #n, \
        (LPVOID)&n, \
        (LPVOID *)&p ## n, \
        &pTarget \
    ) != MH_OK) { \
    } else { \
        if (MH_EnableHook(pTarget) != MH_OK) { \
        } \
    } \
} while (0)
            HOOK_PROC(d3d11, D3D11CreateDeviceAndSwapChain);
            {
                LPVOID pTarget;
                if (MH_CreateHookApiEx(
                    L"kernel32", "TerminateProcess",
                    (LPVOID)&MyTerminateProcess,
                    (LPVOID*)&pTerminateProcess,
                    &pTarget
                ) == MH_OK) {
                    MH_EnableHook(pTarget);
                }
            }
        }
    }

    void minhook_shutdown() {
        if (MinHook_Initialized) {
            if (MH_Uninitialize() != MH_OK) {
            }
            MinHook_Initialized = false;
        }
    }

}

void base_dll_init(HINSTANCE hinstDLL) {
    UINT len = GetSystemDirectoryW(NULL, 0);
    len += wcslen(BASE_DLL_NAME);
    wchar_t* BASE_DLL_NAME_FULL = (wchar_t*)_alloca(len * sizeof(wchar_t));
    memset(BASE_DLL_NAME_FULL, 0, len * sizeof(wchar_t));
    GetSystemDirectoryW(BASE_DLL_NAME_FULL, len);
    wcscat(BASE_DLL_NAME_FULL, BASE_DLL_NAME);

    base_dll = LoadLibraryW(BASE_DLL_NAME_FULL);
    if (!base_dll) {
    }
    else if (base_dll == hinstDLL) {
        FreeLibrary(base_dll);
        base_dll = NULL;
    }
    else {
#define LOAD_PROC(n) do { \
    p ## n = (n ## _t)GetProcAddress(base_dll, #n); \
} while (0)
        LOAD_PROC(CreateDXGIFactory);
        LOAD_PROC(CreateDXGIFactory2);

        minhook_init();
    }
}

void base_dll_shutdown() {
    if (base_dll) {
        minhook_shutdown();

        pCreateDXGIFactory = NULL;
        FreeLibrary(base_dll);
        base_dll = NULL;
    }
}