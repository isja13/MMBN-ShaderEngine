#include "d3d11devicecontext.h"
#include "d3d11device.h"
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
#include "wrapper_registry.h"

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef __MINGW32__
#include <d3d11.h>
template<> inline const GUID& __mingw_uuidof<ID3D11DeviceChild>() {
    static const GUID id = { 0x1841e5c8,0x16b0,0x489b,{0xbc,0xc8,0x44,0xcf,0xb0,0xd5,0xde,0xae} };
    return id;
}
template<> inline const GUID& __mingw_uuidof<ID3D11DeviceContext>() {
    static const GUID id = { 0xc0bfa96c,0xe089,0x44fb,{0x8e,0xaf,0x26,0xf8,0x79,0x61,0x90,0xda} };
    return id;
}
#endif
 
#define LOGGER default_logger
#define LOG_MFUN(_, ...) LOG_MFUN_DEF(MyID3D11DeviceContext, ## __VA_ARGS__)

#define SO_B_LEN (sizeof(float) * 4 * 4 * 6 * 100)
#define MAX_SAMPLERS 16
#define MAX_SHADER_RESOURCES 128
#define MAX_CONSTANT_BUFFERS 15

#define GBA_WIDTH 240
#define GBA_HEIGHT 160
#define DS_WIDTH 256
#define DS_HEIGHT 192
#define PS_BYTECODE_LENGTH_T1_THRESHOLD 1000
#define PS_HASH_T1 0xdfeed8d5    // upscale/composite pass
#define PS_HASH_T2 0xa459320b   //gba -> composite
#define PS_HASH_T3 0x557a81f3  // screen mask

#define DRAWINDEXED_DIAG 0

#if DRAWINDEXED_DIAG
#define DIAG_LOG(fmt, ...) do { \
    FILE* f = fopen("drawindexed_diag.log", "a"); \
    if (f) { fprintf(f, fmt, ##__VA_ARGS__); fclose(f); } \
} while(0)
#else
#define DIAG_LOG(fmt, ...) do {} while(0)
#endif

namespace {

    template<class = void, class...>
    struct DXGIBuffer_Logger;

    template<>
    struct DXGIBuffer_Logger<> {
        const char*& buffer;
    };

    template<class T, class... Ts>
    struct DXGIBuffer_Logger : DXGIBuffer_Logger<Ts...> {};

    struct DXGIBufferType {

        template<size_t N>
        struct Typeless;

        template<size_t N>
        struct Unused;

        template<size_t N>
        struct SInt;

        template<size_t N>
        struct UInt;

        template<size_t N>
        struct SNorm;

        template<size_t N>
        struct UNorm;

        struct Float;
        struct Half;

    };

}

template<class... Ts>
struct LogItem<DXGIBuffer_Logger<Ts...>> : DXGIBufferType {
    DXGIBuffer_Logger<Ts...>& t;
    LogItem(DXGIBuffer_Logger<Ts...>& t) : t(t) {}

    static constexpr bool comp_size(size_t n) {
        return n && n <= 32 && !(n % 8);
    }

    template<size_t N, class... As>
    static void log_comp(Logger* l, DXGIBuffer_Logger<Typeless<N>, As...>& t) {
        static_assert(comp_size(N));
        UINT32 v = 0;
        for (size_t i = 0; i < N; i += 8)
            v |= (UINT32) * (UINT8*)t.buffer++ << i;
        l->log_item(NumHexLogger(v));
    }

    template<size_t N, class... As>
    static void log_comp(Logger* l, DXGIBuffer_Logger<SInt<N>, As...>& t) {
        static_assert(comp_size(N));
        UINT32 v = 0;
        for (size_t i = 0; i < N; i += 8)
            v |= (UINT32) * (UINT8*)t.buffer++ << i;
        v <<= (32 - N);
        auto s = (INT32)v;
        s >>= (32 - N);
        l->log_item(s);
    }

    template<size_t N, class... As>
    static void log_comp(Logger* l, DXGIBuffer_Logger<UInt<N>, As...>& t) {
        static_assert(comp_size(N));
        UINT32 v = 0;
        for (size_t i = 0; i < N; i += 8)
            v |= (UINT32) * (UINT8*)t.buffer++ << i;
        l->log_item(v);
    }

    template<size_t N, class... As>
    static void log_comp(Logger* l, DXGIBuffer_Logger<SNorm<N>, As...>& t) {
        static_assert(comp_size(N));
        UINT32 v = 0;
        for (size_t i = 0; i < N; i += 8)
            v |= (UINT32) * (UINT8*)t.buffer++ << i;
        v <<= (32 - N);
        auto s = (INT32)v;
        s >>= (32 - N);
        auto n = std::max((double)s / (((UINT32)1 << (N - 1)) - 1), -1.);
        l->log_item(n);
    }

    template<size_t N, class... As>
    static void log_comp(Logger* l, DXGIBuffer_Logger<UNorm<N>, As...>& t) {
        static_assert(comp_size(N));
        UINT32 v = 0;
        for (size_t i = 0; i < N; i += 8)
            v |= (UINT32) * (UINT8*)t.buffer++ << i;
        auto n = (double)v / (((UINT64)1 << N) - 1);
        l->log_item(n);
    }

    template<class... As>
    static void log_comp(Logger* l, DXGIBuffer_Logger<Float, As...>& t) {
        auto f = *(float*)t.buffer;
        t.buffer += sizeof(f);
        l->log_item(f);
    }

    template<class... As>
    static void log_comp(Logger* l, DXGIBuffer_Logger<Half, As...>& t) {
        auto f = *(half_float::half*)t.buffer;
        t.buffer += sizeof(f);
        l->log_item((float)f);
    }

    template<class A, class... As>
    void log_comps(Logger* l, DXGIBuffer_Logger<A, As...>& t) {
        log_comp(l, t);
        log_comps_next(l, *(DXGIBuffer_Logger<As...> *) & t);
    }
    template<class A, class... As>
    void log_comps_next(Logger* l, DXGIBuffer_Logger<A, As...>& t) {
        l->log_array_sep();
        log_comps(l, t);
    }
    template<size_t N, class... As>
    void log_comps_next(Logger* l, DXGIBuffer_Logger<Unused<N>, As...>& t) {
        static_assert(comp_size(N));
        t.buffer += N / 8;
        log_comps_next(l, *(DXGIBuffer_Logger<As...> *) & t);
    }

    void log_comps(Logger* l, DXGIBuffer_Logger<>& t) {}
    void log_comps_next(Logger* l, DXGIBuffer_Logger<>& t) {
        log_comps(l, t);
    }

    void log_item(Logger* l) {
        l->log_array_begin();
        log_comps(l, t);
        l->log_array_end();
    }
};

const ENUM_MAP(D3D11_INPUT_CLASSIFICATION) D3D11_INPUT_CLASSIFICATION_ENUM_MAP = {
    ENUM_MAP_ITEM(D3D11_INPUT_PER_VERTEX_DATA),
    ENUM_MAP_ITEM(D3D11_INPUT_PER_INSTANCE_DATA),
};

template<>
struct LogItem<D3D11_INPUT_CLASSIFICATION> {
    const D3D11_INPUT_CLASSIFICATION a;
    void log_item(Logger* l) const {
        l->log_enum(D3D11_INPUT_CLASSIFICATION_ENUM_MAP, a);
    }
};

struct MyVertexBufferElement_Logger {
    const char* buffer;
    const MyID3D11InputLayout* input_layout;
};

template<>
struct LogItem<MyVertexBufferElement_Logger> : DXGIBufferType {
    const MyVertexBufferElement_Logger& a;  
    LogItem(const MyVertexBufferElement_Logger& a) : a(a) {}  
    void log_item(Logger* l) const {
        l->log_struct_begin();
        auto descs = a.input_layout->get_descs();  
        auto cached = a.buffer;  
        for (UINT i = 0; i < a.input_layout->get_descs_num(); ++i, ++descs) {
            if (i) l->log_struct_sep();
            l->log_item(descs->SemanticName);
            l->log_struct_member_access();
            l->log_item(descs->SemanticIndex);
            l->log_assign();
            if (descs->InputSlot) {
                l->log_struct_named(
#define STRUCT descs
                    LOG_STRUCT_MEMBER(InputSlot)
#undef STRUCT
                );
            }
            else {
                if (descs->AlignedByteOffset != D3D11_APPEND_ALIGNED_ELEMENT)
                    cached = a.buffer + descs->AlignedByteOffset;
                if (cached) switch (descs->Format) {
                case DXGI_FORMAT_R32G32B32A32_TYPELESS: {
                    l->log_item(DXGIBuffer_Logger<
                        Typeless<32>, Typeless<32>, Typeless<32>, Typeless<32>
                    >{cached});
                    break;
                }

                case DXGI_FORMAT_R32G32B32A32_FLOAT:
                    l->log_item(DXGIBuffer_Logger<
                        Float, Float, Float, Float
                    >{cached});
                    break;

                case DXGI_FORMAT_R32G32B32A32_UINT:
                    l->log_item(DXGIBuffer_Logger<
                        UInt<32>, UInt<32>, UInt<32>, UInt<32>
                    >{cached});
                    break;

                case DXGI_FORMAT_R32G32B32A32_SINT:
                    l->log_item(DXGIBuffer_Logger<
                        SInt<32>, SInt<32>, SInt<32>, SInt<32>
                    >{cached});
                    break;

                case DXGI_FORMAT_R32G32B32_TYPELESS:
                    l->log_item(DXGIBuffer_Logger<
                        Typeless<32>, Typeless<32>, Typeless<32>
                    >{cached});
                    break;

                case DXGI_FORMAT_R32G32B32_FLOAT:
                    l->log_item(DXGIBuffer_Logger<
                        Float, Float, Float
                    >{cached});
                    break;

                case DXGI_FORMAT_R32G32B32_UINT:
                    l->log_item(DXGIBuffer_Logger<
                        UInt<32>, UInt<32>, UInt<32>
                    >{cached});
                    break;

                case DXGI_FORMAT_R32G32B32_SINT:
                    l->log_item(DXGIBuffer_Logger<
                        SInt<32>, SInt<32>, SInt<32>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                    l->log_item(DXGIBuffer_Logger<
                        Typeless<16>, Typeless<16>, Typeless<16>, Typeless<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16G16B16A16_FLOAT:
                    l->log_item(DXGIBuffer_Logger<
                        Half, Half, Half, Half
                    >{cached});
                    break;

                case DXGI_FORMAT_R16G16B16A16_UNORM:
                    l->log_item(DXGIBuffer_Logger<
                        UNorm<16>, UNorm<16>, UNorm<16>, UNorm<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16G16B16A16_UINT:
                    l->log_item(DXGIBuffer_Logger<
                        UInt<16>, UInt<16>, UInt<16>, UInt<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16G16B16A16_SNORM:
                    l->log_item(DXGIBuffer_Logger<
                        SNorm<16>, SNorm<16>, SNorm<16>, SNorm<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16G16B16A16_SINT:
                    l->log_item(DXGIBuffer_Logger<
                        SInt<16>, SInt<16>, SInt<16>, SInt<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R32G32_TYPELESS:
                    l->log_item(DXGIBuffer_Logger<
                        Typeless<32>, Typeless<32>
                    >{cached});
                    break;

                case DXGI_FORMAT_R32G32_FLOAT:
                    l->log_item(DXGIBuffer_Logger<
                        Float, Float
                    >{cached});
                    break;

                case DXGI_FORMAT_R32G32_UINT:
                    l->log_item(DXGIBuffer_Logger<
                        UInt<32>, UInt<32>
                    >{cached});
                    break;

                case DXGI_FORMAT_R32G32_SINT:
                    l->log_item(DXGIBuffer_Logger<
                        SInt<32>, SInt<32>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8G8B8A8_TYPELESS:
                    l->log_item(DXGIBuffer_Logger<
                        Typeless<8>, Typeless<8>, Typeless<8>, Typeless<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8G8B8A8_UNORM:
                    l->log_item(DXGIBuffer_Logger<
                        UNorm<8>, UNorm<8>, UNorm<8>, UNorm<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8G8B8A8_UINT:
                    l->log_item(DXGIBuffer_Logger<
                        UInt<8>, UInt<8>, UInt<8>, UInt<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8G8B8A8_SNORM:
                    l->log_item(DXGIBuffer_Logger<
                        SNorm<8>, SNorm<8>, SNorm<8>, SNorm<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8G8B8A8_SINT:
                    l->log_item(DXGIBuffer_Logger<
                        SInt<8>, SInt<8>, SInt<8>, SInt<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16G16_TYPELESS:
                    l->log_item(DXGIBuffer_Logger<
                        Typeless<16>, Typeless<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16G16_FLOAT:
                    l->log_item(DXGIBuffer_Logger<
                        Half, Half
                    >{cached});
                    break;

                case DXGI_FORMAT_R16G16_UNORM:
                    l->log_item(DXGIBuffer_Logger<
                        UNorm<16>, UNorm<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16G16_UINT:
                    l->log_item(DXGIBuffer_Logger<
                        UInt<16>, UInt<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16G16_SNORM:
                    l->log_item(DXGIBuffer_Logger<
                        SNorm<16>, SNorm<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16G16_SINT:
                    l->log_item(DXGIBuffer_Logger<
                        SInt<16>, SInt<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R32_TYPELESS:
                    l->log_item(DXGIBuffer_Logger<
                        Typeless<32>
                    >{cached});
                    break;

                case DXGI_FORMAT_D32_FLOAT:
                case DXGI_FORMAT_R32_FLOAT:
                    l->log_item(DXGIBuffer_Logger<
                        Float
                    >{cached});
                    break;

                case DXGI_FORMAT_R32_UINT:
                    l->log_item(DXGIBuffer_Logger<
                        UInt<32>
                    >{cached});
                    break;

                case DXGI_FORMAT_R32_SINT:
                    l->log_item(DXGIBuffer_Logger<
                        SInt<32>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8G8_TYPELESS:
                    l->log_item(DXGIBuffer_Logger<
                        Typeless<8>, Typeless<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8G8_UNORM:
                    l->log_item(DXGIBuffer_Logger<
                        UNorm<8>, UNorm<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8G8_UINT:
                    l->log_item(DXGIBuffer_Logger<
                        UInt<8>, UInt<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8G8_SNORM:
                    l->log_item(DXGIBuffer_Logger<
                        SNorm<8>, SNorm<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8G8_SINT:
                    l->log_item(DXGIBuffer_Logger<
                        SInt<8>, SInt<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16_TYPELESS:
                    l->log_item(DXGIBuffer_Logger<
                        Typeless<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16_FLOAT:
                    l->log_item(DXGIBuffer_Logger<
                        Half
                    >{cached});
                    break;

                case DXGI_FORMAT_D16_UNORM:
                case DXGI_FORMAT_R16_UNORM:
                    l->log_item(DXGIBuffer_Logger<
                        UNorm<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16_UINT:
                    l->log_item(DXGIBuffer_Logger<
                        UInt<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16_SNORM:
                    l->log_item(DXGIBuffer_Logger<
                        SNorm<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R16_SINT:
                    l->log_item(DXGIBuffer_Logger<
                        SInt<16>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8_TYPELESS:
                    l->log_item(DXGIBuffer_Logger<
                        Typeless<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_A8_UNORM:
                case DXGI_FORMAT_R8_UNORM:
                    l->log_item(DXGIBuffer_Logger<
                        UNorm<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8_UINT:
                    l->log_item(DXGIBuffer_Logger<
                        UInt<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8_SNORM:
                    l->log_item(DXGIBuffer_Logger<
                        SNorm<8>
                    >{cached});
                    break;

                case DXGI_FORMAT_R8_SINT:
                    l->log_item(DXGIBuffer_Logger<
                        SInt<8>
                    >{cached});
                    break;

                default:
                    l->log_struct_named(
#define STRUCT descs
                        LOG_STRUCT_MEMBER(Format)
#undef STRUCT
                    );
                    cached = NULL;
                    break;
                }
                else {
                    l->log_struct_named(
#define STRUCT descs
                        LOG_STRUCT_MEMBER(Format)
#undef STRUCT
                    );
                }
            }
        }
        l->log_struct_end();
    }
};

struct MyVertexBuffer_Logger {
    const MyID3D11InputLayout* input_layout;
    const MyID3D11Buffer* vertex_buffer;
    UINT stride;
    UINT offset;
    UINT VertexCount;
    UINT StartVertexLocation;
};

template<>
struct LogItem<MyVertexBuffer_Logger> {
    const MyVertexBuffer_Logger* a;
    void log_item(Logger* l) const {
        l->log_struct_begin();
        auto vb_cached_state = a->vertex_buffer->get_cached_state();
        if (vb_cached_state) {
            if (auto cached = a->vertex_buffer->get_cached()) {
                l->log_struct_sep();
                l->log_item("vb_cached");
                l->log_assign();
                l->log_array_begin();

                cached += a->offset + a->stride * a->StartVertexLocation;
                for (UINT i = 0; i < a->VertexCount; ++i, cached += a->stride) {
                    if (i) l->log_array_sep();
                    l->log_item(
                        MyVertexBufferElement_Logger{ cached, a->input_layout }
                    );
                }

                l->log_array_end();
            }
        }
        else {
            l->log_struct_members_named(
                LOG_ARG(vb_cached_state)
            );
        }
        l->log_struct_end();
    }
};

struct MyIndexedVertexBuffer_Logger {
    const MyID3D11InputLayout* input_layout;
    const MyID3D11Buffer* vertex_buffer;
    UINT stride;
    UINT offset;
    const MyID3D11Buffer* index_buffer;
    DXGI_FORMAT index_format;
    UINT index_offset;
    UINT IndexCount;
    UINT StartIndexLocation;
    INT BaseVertexLocation;
};

template<>
struct LogItem<MyIndexedVertexBuffer_Logger> {
    const MyIndexedVertexBuffer_Logger* a;
    void log_item(Logger* l) const {
        l->log_struct_begin();
        auto vb_cached_state = a->vertex_buffer->get_cached_state();
        auto ib_cached_state = a->index_buffer->get_cached_state();
        if (vb_cached_state && ib_cached_state) {
            auto vb_cached = a->vertex_buffer->get_cached();
            auto ib_cached = a->index_buffer->get_cached();
            if (ib_cached) {
                l->log_struct_sep();
                l->log_item("vb_cached");
                l->log_assign();
                l->log_array_begin();

                bool ib_32 = a->index_format == DXGI_FORMAT_R32_UINT;
                if (ib_32 || a->index_format == DXGI_FORMAT_R16_UINT) {
                    vb_cached +=
                        a->offset + a->stride * a->BaseVertexLocation;
                    ib_cached +=
                        a->index_offset +
                        (ib_32 ? sizeof(UINT32) : sizeof(UINT16)) *
                        a->StartIndexLocation;
                    for (UINT i = 0; i < a->IndexCount; ++i) {
                        if (i) l->log_array_sep();
                        UINT vb_i = ib_32 ?
                            ((UINT32*)ib_cached)[i] :
                            ((UINT16*)ib_cached)[i];
                        l->log_item(
                            MyVertexBufferElement_Logger{
                                vb_cached + a->stride * vb_i,
                                a->input_layout
                            }
                        );
                    }
                }

                l->log_array_end();
            }
        }
        else {
            l->log_struct_members_named(
                LOG_ARG(vb_cached_state),
                LOG_ARG(ib_cached_state)
            );
        }
        l->log_struct_end();
    }
};

namespace {

    struct SOBuffer_Logger {
        const MyID3D11VertexShader* vs;
        const MyID3D11PixelShader* ps;
        const char* data;
        const MyID3D11ShaderResourceView* const* srvs;
        const D3D11_VIEWPORT& vp;
        const UINT64 n;
    };

    struct ConstantBuffer_Logger {
        const std::vector<std::tuple<std::string, std::string>>& uniform_list;
        const MyID3D11Buffer* const* pscbs;
    };

    template<class T>
    void hash_combine(size_t& s, const T& v) {
        s ^= std::hash<T>()(v) + 0x9e3779b9 + (s << 6) + (s >> 2);
    }

    template<class T, size_t I = std::tuple_size_v<T> -1>
    struct tuple_hash {
        static void apply(size_t& s, const T& t) {
            tuple_hash<T, I - 1>::apply(s, t);
            hash_combine(s, std::get<I>(t));
        }
    };

    template<class T>
    struct tuple_hash<T, 0>
    {
        static void apply(size_t& s, const T& t) {
            hash_combine(s, std::get<0>(t));
        }
    };

}

namespace std {

    template<class... Ts>
    struct hash<tuple<Ts...>> {
        size_t operator()(const tuple<Ts...>& t) const {
            size_t s = 0;
            tuple_hash<tuple<Ts...>>::apply(s, t);
            return s;
        }
    };

}

template<>
struct LogItem<SOBuffer_Logger> {
    const SOBuffer_Logger& a;

    struct Pos {
        double x = -1;
        double y = -1;
        operator bool() const {
            return x >= 0 && y >= 0;
        }
        bool operator<(const Pos& r) const {
            return
                std::tie(x, y)
                < std::tie(r.x, r.y);
        }
    };

    void log_item(Logger* l) const {
        l->log_array_begin();
        auto data = (const float*)a.data;
        auto& sampler_map = a.ps->get_texcoord_sampler_map();
        std::unordered_map<std::tuple<std::string, UINT>, std::set<Pos>> coords;
        for (UINT64 i = 0; i < a.n; ++i) {
            if ((const char*)data >= a.data + SO_B_LEN) break;
            if (i) l->log_array_sep();
            l->log_struct_begin();
            bool sep = false;
            for (auto& entry : a.vs->get_decl_entries()) {
                if ((const char*)(data + entry.ComponentCount) >= a.data + SO_B_LEN) {
                    data += entry.ComponentCount;
                    break;
                }
                if (sep) l->log_struct_sep();
                sep = true;
                l->log_item(entry.SemanticName);
                l->log_struct_member_access();
                l->log_item(entry.SemanticIndex);
                l->log_assign();

                UINT width = 0, height = 0;
                bool w =
                    entry.StartComponent < 4 &&
                    entry.ComponentCount + entry.StartComponent >= 4;
                float d = w ? data[3 - entry.StartComponent] : 0;
                if (!d) d = 1;
                bool position =
                    w && entry.SemanticName == std::string("SV_Position");
                if (position) {
                    width = a.vp.Width;
                    height = a.vp.Height;
                }
                bool texcoord =
                    !position &&
                    entry.SemanticName == std::string("TEXCOORD");
                if (texcoord) {
                    if (entry.SemanticIndex >= sampler_map.size()) {
                        texcoord = false;
                    }
                    else {
                        UINT srv_i = sampler_map[entry.SemanticIndex];
                        if (srv_i == (UINT)-1) {
                            texcoord = false;
                        }
                        else {
                            l->log_item(srv_i);
                            auto srv = a.srvs[srv_i];
                            if (
                                !srv ||
                                srv->get_desc().ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D
                                ) {
                                texcoord = false;
                            }
                            else {
                                auto tex = as_wrapper<MyID3D11Texture2D, ID3D11Resource>(srv->get_resource());
                                if (tex) {
                                    width = tex->get_desc().Width;
                                    height = tex->get_desc().Height;
                                }
                            }
                        }
                    }
                }
                if (width && height) {
                    l->log_struct_begin();
                    l->log_item(width);
                    l->log_struct_sep();
                    l->log_item(height);
                    l->log_struct_end();
                    l->log_struct_member_access();
                }
                Pos pixel_pos{};

                l->log_array_begin();
                for (
                    UINT i = 0, c = entry.StartComponent;
                    i < entry.ComponentCount;
                    ++i, ++c
                    ) {
                    if (i) l->log_array_sep();
                    float v = *data++;
                    l->log_item(v);
                    if (c > 1) continue;
                    if (position) {
                        switch (c) {
                        case 0:
                            pixel_pos.x = (v / d + 1) / 2 * width - 0.5;
                            l->log_struct_begin();
                            l->log_item(pixel_pos.x);
                            l->log_struct_end();
                            break;

                        case 1:
                            pixel_pos.y = (-v / d + 1) / 2 * height - 0.5;
                            l->log_struct_begin();
                            l->log_item(pixel_pos.y);
                            l->log_struct_end();
                            break;

                        default:
                            break;
                        }
                    }
                    else if (texcoord) {
                        switch (c) {
                        case 0:
                            pixel_pos.x = v * width - 0.5;
                            l->log_struct_begin();
                            l->log_item(pixel_pos.x);
                            l->log_struct_end();
                            break;

                        case 1:
                            pixel_pos.y = v * height - 0.5;
                            l->log_struct_begin();
                            l->log_item(pixel_pos.y);
                            l->log_struct_end();
                            break;

                        default:
                            break;
                        }
                    }
                }
                l->log_array_end();

                if (pixel_pos)
                    coords[
                        std::make_tuple(
                            std::string(entry.SemanticName),
                            entry.SemanticIndex
                        )
                    ].insert(std::move(pixel_pos));
            }
            l->log_struct_end();
        }
        l->log_array_end();
        l->log_struct_member_access();
        l->log_struct_begin();
        bool sep = false;
        for (auto& item : coords) {
            if (sep) l->log_struct_sep();
            sep = true;
            l->log_item(std::get<0>(item.first));
            l->log_struct_member_access();
            l->log_item(std::get<1>(item.first));
            l->log_assign();
            l->log_array_begin();
            bool sep = false;
            for (auto& pos : item.second) {
                if (sep) l->log_array_sep();
                sep = true;
                l->log_struct_begin();
                l->log_item(pos.x);
                l->log_struct_sep();
                l->log_item(pos.y);
                l->log_struct_end();
            }
            l->log_array_end();
        }
        l->log_struct_end();
    }
};

template<>
struct LogItem<ConstantBuffer_Logger> {
    const ConstantBuffer_Logger& a;
    void log_item(Logger* l) const {
        l->log_array_begin();
        for (auto& item : a.uniform_list) {
            auto type = std::get<0>(item);
            auto name = std::get<1>(item);
        }
        l->log_array_end();
    }
};

struct LinearFilterConditions {
    PIXEL_SHADER_ALPHA_DISCARD alpha_discard;
    std::vector<const D3D11_SAMPLER_DESC*> samplers_descs;
    std::vector<const D3D11_TEXTURE2D_DESC*> texs_descs;
} linear_conditions = {};

class MyID3D11DeviceContext::Impl {
    friend class MyID3D11DeviceContext;
    friend class MyID3D11Device;

    IUNKNOWN_PRIV(ID3D11DeviceContext)
    ID3D11DeviceContext* context = NULL;
    MyID3D11Device* device = NULL;

    static constexpr uint64_t IMPL_MAGIC_ALIVE = 0x494D504C414C4956ull; // "IMPLALIV"
    static constexpr uint64_t IMPL_MAGIC_DEAD = 0x494D504C44454144ull; // "IMPLDEAD"

    uint64_t magic = IMPL_MAGIC_ALIVE;

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

    struct FilterState {
        MyID3D11ShaderResourceView* srv;
        MyID3D11Texture2D* rtv_tex;
        MyID3D11PixelShader* ps;
        MyID3D11VertexShader* vs;
        bool t1;
        MyID3D11SamplerState* psss;
        D3D11_VIEWPORT game_vp = {};
        bool ds;
        UINT start_vertex_location;
        UINT start_index_location;
        ID3D11Buffer* vertex_buffer;
        UINT vertex_stride;
        UINT vertex_offset;
        float key_color[3] = {};
    } filter_state = {};
    bool filter = false;
    bool fired_this_frame = false;
    bool t2_scan_seen = false;
    bool ingame_filter_seen = false;
    bool gba_seen_this_scan = false;

    float ui_scale = 1.0f;

    void clear_filter() {
        filter = false;
        fired_this_frame = false;
        if (filter_state.srv) filter_state.srv->Release();
        if (filter_state.rtv_tex) filter_state.rtv_tex->Release();
        if (filter_state.ps) filter_state.ps->Release();
        if (filter_state.vs) filter_state.vs->Release();
        if (filter_state.psss) filter_state.psss->Release();
        if (filter_state.vertex_buffer) filter_state.vertex_buffer->Release();
        filter_state = {};
    }

    bool render_interp = false;
    bool render_linear = false;
    bool render_enhanced = false;
    UINT linear_test_width = 0;
    UINT linear_test_height = 0;

    LinearFilterConditions linear_conditions = {};

    friend class LogItem<LinearFilterConditions>;
    

    struct LinearRtv {
        UINT width;
        UINT height;
        ID3D11RenderTargetView* rtv;
        ~LinearRtv() {
            if (rtv) rtv->Release();
            rtv = NULL;
        }
    };
    std::deque<LinearRtv> linear_rtvs;

    bool linear_restore = false;
    bool stream_out = false;

    ID3D11Buffer* so_bt = NULL, * so_bs = NULL;
    ID3D11Query* so_q = NULL;

    MyID3D11PixelShader* cached_ps = NULL;
    MyID3D11VertexShader* cached_vs = NULL;
    ID3D11GeometryShader* cached_gs = NULL;
    MyID3D11InputLayout* cached_il = NULL;
    MyID3D11DepthStencilState* cached_dss = NULL;
    MyID3D11SamplerState* cached_pssss[MAX_SAMPLERS] = {};
    ID3D11SamplerState* render_pssss[MAX_SAMPLERS] = {};
    MyID3D11RenderTargetView* cached_rtv = NULL;
    MyID3D11DepthStencilView* cached_dsv = NULL;
    MyID3D11ShaderResourceView* cached_pssrvs[MAX_SHADER_RESOURCES] = {};
    ID3D11ShaderResourceView* render_pssrvs[MAX_SHADER_RESOURCES] = {};
    D3D11_PRIMITIVE_TOPOLOGY cached_pt =
        D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

    struct BlendState {
        ID3D11BlendState* pBlendState;
        FLOAT BlendFactor[4];
        UINT SampleMask;
    } cached_bs = {};

    struct VertexBuffers {
        ID3D11Buffer* ppVertexBuffers[
            D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT
        ] = {};
            MyID3D11Buffer* vertex_buffers[
                D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT
            ] = {};
                UINT pStrides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
                UINT pOffsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
    } cached_vbs = {};

    MyID3D11Buffer* cached_ib = NULL;
    DXGI_FORMAT cached_ib_format = DXGI_FORMAT_UNKNOWN;
    UINT cached_ib_offset = 0;
    ID3D11Buffer* render_pscbs[MAX_CONSTANT_BUFFERS] = {};
    MyID3D11Buffer* cached_pscbs[MAX_CONSTANT_BUFFERS] = {};
    ID3D11Buffer* cached_vscbs[MAX_CONSTANT_BUFFERS] = {};

    UINT render_width = 0;
    UINT render_height = 0;
    UINT render_orig_width = 0;
    UINT render_orig_height = 0;
    D3D11_VIEWPORT render_vp = {};
    D3D11_VIEWPORT cached_vp = {};
    bool need_render_vp = false;
    bool is_render_vp = false;

    bool stream_out_available() {
        return
            !cached_gs &&
            cached_ps && cached_vs && cached_vs->get_sogs() &&
            so_bt && so_bs && so_q && *cached_pssrvs &&
            cached_pt == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    }

    void linear_conditions_begin() {
        linear_restore = false;
        stream_out = false;
        linear_conditions = {};
        if (!render_linear && !LOG_STARTED) return;

        if (cached_ps)
            linear_conditions.alpha_discard =
            cached_ps->get_alpha_discard();

        ID3D11SamplerState* sss[MAX_SAMPLERS] = {};
        auto sss_inner = sss;
        auto pssss = cached_pssss;
        for (
            auto srvs = cached_pssrvs;
            *srvs;
            ++srvs, ++pssss, ++sss_inner
            ) {
            auto srv = *srvs;
            {
                D3D11_TEXTURE2D_DESC* tex_desc = NULL;
                if (srv->get_desc().ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D) {
                    auto* _tex = as_wrapper<MyID3D11Texture2D>(srv->get_resource());
                    if (_tex) tex_desc = &_tex->get_desc();
                }
                linear_conditions.texs_descs.push_back(tex_desc);
            }
            auto ss = *pssss;
            linear_conditions.samplers_descs.push_back(
                ss ? &ss->get_desc() : NULL
            );
            *sss_inner = ss ? ss->get_inner() : NULL;
        }

        if (
            linear_conditions.alpha_discard ==
            PIXEL_SHADER_ALPHA_DISCARD::EQUAL
            ) {
            if (render_linear && linear_conditions.texs_descs.size()) {
                UINT width = linear_conditions.texs_descs[0]->Width;
                UINT height = linear_conditions.texs_descs[0]->Height;

                if (
                    !(
                        (width == 512 || width == 256) &&
                        height == 256
                        ) &&
                    !(
                        width == linear_test_width &&
                        height == linear_test_height
                        )
                    ) {
                    context->PSSetSamplers(0, MAX_SAMPLERS, sss);
                    linear_restore = true;
                }
            }
            if (LOG_STARTED && stream_out_available()) {
                context->GSSetShader(cached_vs->get_sogs(), NULL, 0);
                UINT offset = 0;
                context->SOSetTargets(1, &so_bt, &offset);
                context->Begin(so_q);
                stream_out = true;
            }
        }
    }

    void linear_conditions_end() {
        if (linear_restore)
            context->PSSetSamplers(0, MAX_SAMPLERS, render_pssss);
        if (stream_out) {
            context->GSSetShader(NULL, NULL, 0);
            context->SOSetTargets(0, NULL, NULL);
            context->CopyResource(so_bs, so_bt);
            D3D11_MAPPED_SUBRESOURCE mapped;
            context->Map(so_bs, 0, D3D11_MAP_READ, 0, &mapped);
            void* data = mapped.pData;
            context->End(so_q);
            D3D11_QUERY_DATA_SO_STATISTICS stat;
            while (S_OK != context->GetData(so_q, &stat, sizeof(stat), 0));
            SOBuffer_Logger stream_out{
                .vs = cached_vs,
                .ps = cached_ps,
                .data = (const char*)data,
                .srvs = cached_pssrvs,
                .vp = cached_vp,
                .n = stat.NumPrimitivesWritten * 3
            };
            ConstantBuffer_Logger ps_constant_buffer{
                .uniform_list = cached_ps->get_uniform_list(),
                .pscbs = cached_pscbs,
            };
            LOG_FUN(_,
                LOG_ARG(stream_out),
                LOG_ARG(ps_constant_buffer)
            );
            context->Unmap(so_bs, 0);
        }
    }

    void set_render_vp() {
        if (!LOG_STARTED) return;

        size_t srvs_n = 0;
        auto srvs = cached_pssrvs;
        while (srvs_n < MAX_SHADER_RESOURCES && *srvs) {
            ++srvs, ++srvs_n;
        }
        LOG_MFUN(_,
            LOG_ARG(cached_rtv),
            LOG_ARG(cached_dsv),
            LOG_ARG_TYPE(cached_pssrvs, ArrayLoggerDeref, srvs_n),
            LogIf<5>{is_render_vp},
            LOG_ARG(render_vp),
            LOG_ARG(render_width),
            LOG_ARG(render_height),
            LOG_ARG(render_orig_width),
            LOG_ARG(render_orig_height)
        );
    }

    void reset_render_vp() {
        if constexpr (ENABLE_CUSTOM_RESOLUTION) {
            if (need_render_vp && is_render_vp) {
                if (cached_vp.Width && cached_vp.Height) {
                    render_vp = cached_vp;
                    context->RSSetViewports(1, &render_vp);
                }
                is_render_vp = false;
            }
        }
    }

    void DrawIndexed(
        UINT IndexCount,
        UINT StartIndexLocation,
        INT BaseVertexLocation
    ) {
        set_render_vp();
        bool filter_next = false;
        bool filter_ss = false;
        bool filter_ss_gba = false;
        bool filter_ss_ds = false;

        filter_ss_gba = device->get_config()->shader_toggle &&
            device->get_d3d11_slot(device->get_config()->slang_active.load()) &&
            device->get_d3d11_slot(device->get_config()->slang_active.load())->shader_preset;

        auto set_filter_state_ps = [this] {
            context->PSSetShader(filter_state.ps->get_inner(), NULL, 0);
            };
        auto restore_ps = [this] {
            auto ps = cached_ps ? cached_ps->get_inner() : NULL;
            context->PSSetShader(ps, NULL, 0);
            };
        auto restore_vps = [this] {
            context->RSSetViewports(1, &render_vp);
            };
        auto restore_pscbs = [this] {
            context->PSSetConstantBuffers(0, MAX_CONSTANT_BUFFERS, render_pscbs);
            };
        auto restore_rtvs = [this] {
            ID3D11RenderTargetView* rtv = cached_rtv ? cached_rtv->get_inner() : NULL;
            ID3D11DepthStencilView* dsv = cached_dsv ? cached_dsv->get_inner() : NULL;
            context->OMSetRenderTargets(1, &rtv, dsv);
            };
        auto restore_pssrvs = [this] {
            context->PSSetShaderResources(0, MAX_SHADER_RESOURCES, render_pssrvs);
            };
        auto restore_pssss = [this] {
            context->PSSetSamplers(0, MAX_SAMPLERS, render_pssss);
            };
        auto set_viewport = [this](UINT width, UINT height) {
            D3D11_VIEWPORT viewport = {
                .TopLeftX = 0,
                .TopLeftY = 0,
                .Width = (FLOAT)width,
                .Height = (FLOAT)height,
                .MinDepth = 0,
                .MaxDepth = 1,
            };
            context->RSSetViewports(1, &viewport);
            };
        auto set_srv = [this](ID3D11ShaderResourceView* srv) {
            context->PSSetShaderResources(0, 1, &srv);
            };
        auto set_rtv = [this](
            ID3D11RenderTargetView* rtv,
            ID3D11DepthStencilView* dsv = NULL
            ) {
                context->OMSetRenderTargets(1, &rtv, dsv);
            };
        auto set_psss = [this](ID3D11SamplerState* psss) {
            context->PSSetSamplers(0, 1, &psss);
            };

        auto srv = *cached_pssrvs;
        {
            if (
                !render_interp &&
                !render_linear &&
                !filter_ss &&
                !filter_ss_gba &&
                !filter_ss_ds
                ) goto end;

            // IndexCount == 6 instead of VertexCount == 4
            if (IndexCount != 6) goto end;

            auto psss = *cached_pssss;

            // Never snub these hashes on psss grounds — let them reach the draw path
            if (cached_ps && (
                cached_ps->get_bytecode_hash() == PS_HASH_T2 ||
                cached_ps->get_bytecode_hash() == PS_HASH_T1
                ))
                filter_next = filter;
            else {
                if (!psss) goto end;
                filter_next = filter && psss == filter_state.psss;
            }

            if (!srv) goto end;

            D3D11_SHADER_RESOURCE_VIEW_DESC& srv_desc = srv->get_desc();
            if (srv_desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
                goto end;

            auto srv_tex = as_wrapper<MyID3D11Texture2D, ID3D11Resource>(srv->get_resource());
            if (!srv_tex) goto end;
            filter_next &= srv_tex == filter_state.rtv_tex;

            bool ds = false;
            D3D11_TEXTURE2D_DESC& srv_tex_desc = srv_tex->get_desc();

            if (filter_next) {
            }
            else if (
                srv_tex_desc.Width == GBA_WIDTH &&
                srv_tex_desc.Height == GBA_HEIGHT
                ) {
                filter_ss |= filter_ss_gba;
                gba_seen_this_scan = true;
            }
            //DS path retained for future-proofing, costs nothing
            else if (
                srv_tex_desc.Width == DS_WIDTH &&
                srv_tex_desc.Height == DS_HEIGHT
                ) {
                ds = true;
                filter_ss |= filter_ss_ds;
            }
            else {
                goto end;
            }

            MyID3D11Texture2D* rtv_tex = nullptr;
            if (filter_next) {
                // Already armed — use stored rtv_tex, don't require current RTV to be wrapped
                rtv_tex = filter_state.rtv_tex;
                if (!rtv_tex) goto end;
            }
            else {
                auto rtv = cached_rtv;
                if (!rtv) goto end;

                D3D11_RENDER_TARGET_VIEW_DESC& rtv_desc = rtv->get_desc();
                if (rtv_desc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D)
                    goto end;

                rtv_tex = as_wrapper<MyID3D11Texture2D, ID3D11Resource>(rtv->get_resource());
                if (!rtv_tex) goto end;
            }

            // draw_enhanced and draw_nn use DrawIndexed internally
            auto draw_enhanced = [&](
                std::vector<TextureViewsAndBuffer*>& v_v
                ) {
                    auto v_it = v_v.begin();
                    if (v_it == v_v.end()) {
                        set_psss(device->get_filter_temp().sampler_linear);
                        context->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
                        restore_pssss();
                        return;
                    }
                    set_filter_state_ps();
                    auto set_it_viewport = [&] {
                        set_viewport((*v_it)->width, (*v_it)->height);
                        };
                    set_it_viewport();
                    auto set_it_rtv = [&] {
                        set_rtv((*v_it)->rtv);
                        };
                    set_it_rtv();
                    auto set_it_pscbs = [&] {
                        context->PSSetConstantBuffers(0, 1, &(*v_it)->ps_cb);
                        };
                    set_it_pscbs();
                    // uses start_index_location
                    context->DrawIndexed(IndexCount, filter_state.start_index_location, BaseVertexLocation);
                    auto v_it_prev = v_it;
                    auto set_it_prev_srv = [&] {
                        set_srv((*v_it_prev)->srv);
                        };
                    while (++v_it != v_v.end()) {
                        set_it_rtv();
                        set_it_prev_srv();
                        set_it_viewport();
                        set_it_pscbs();
                        context->DrawIndexed(IndexCount, filter_state.start_index_location, BaseVertexLocation);
                        v_it_prev = v_it;
                    }
                    restore_ps();
                    restore_vps();
                    restore_rtvs();
                    restore_pscbs();
                    set_it_prev_srv();
                    set_psss(device->get_filter_temp().sampler_linear);
                    context->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
                    restore_pssrvs();
                    restore_pssss();
                };

            auto draw_nn = [&](TextureAndViews* v) {
                set_viewport(v->width, v->height);
                set_rtv(v->rtv);
                set_srv(filter_state.srv->get_inner());
                if (render_linear) set_psss(device->get_filter_temp().sampler_nn);
                context->DrawIndexed(IndexCount, filter_state.start_index_location, BaseVertexLocation);
                restore_vps();
                restore_rtvs();
                set_srv(v->srv);
                set_psss(device->get_filter_temp().sampler_linear);
                context->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
                restore_pssrvs();
                restore_pssss();
                };

            // draw_ss cached_tex final blit uses DrawIndexed
            auto draw_ss = [&](
                d3d11_video_t* d3d11,
                MyID3D11ShaderResourceView* srv,
                D3D11_VIEWPORT* render_vp,
                MyID3D11RenderTargetView* fire_rtv,
                TextureAndViews* cached_tex = NULL
                ) {
                    if (cached_tex) {
                        video_viewport_t vp = {
                            .x = 0,
                            .y = 0,
                            .width = cached_tex->width,
                            .height = cached_tex->height,
                            .full_width = cached_tex->width,
                            .full_height = cached_tex->height
                        };
                        my_d3d11_update_viewport(d3d11, cached_tex->rtv, &vp);
                    }
                    else {
                        video_viewport_t vp;
                        if (device->get_config()->widescreen && ui_scale == 1.0f) {
                            vp = {
                                .x = (int)render_vp->TopLeftX,
                                .y = (int)render_vp->TopLeftY,
                                .width = (unsigned int)render_vp->Width,
                                .height = (unsigned int)render_vp->Height,
                                .full_width = render_size.sc_width,
                                .full_height = render_size.sc_height
                            };
                        }
                        else {
                            unsigned int out_h = (unsigned int)(render_size.sc_height * ui_scale);
                            unsigned int out_w = out_h * GBA_WIDTH / GBA_HEIGHT;
                            int out_x = (int)(render_size.sc_width - out_w) / 2;
                            int out_y = (int)(render_size.sc_height - out_h) / 2;
                            vp = {
                                .x = out_x,
                                .y = out_y,
                                .width = out_w,
                                .height = out_h,
                                .full_width = render_size.sc_width,
                                .full_height = render_size.sc_height
                            };
                            DIAG_LOG("[DRAW_SS_VP_COMPUTED] out_x=%d out_y=%d out_w=%u out_h=%u ui_scale=%f\n",
                                out_x, out_y, out_w, out_h, ui_scale);
                        }
#if DRAWINDEXED_DIAG
                        auto* fire_res = as_wrapper<MyID3D11Texture2D, ID3D11Resource>(fire_rtv->get_resource());
#endif
                        DIAG_LOG("[DRAW_SS_RTV] fire_rtv=%p inner=%p orig_w=%u orig_h=%u\n",
                            (void*)fire_rtv,
                            (void*)fire_rtv->get_inner(),
                            fire_res ? fire_res->get_orig_width() : 0,
                            fire_res ? fire_res->get_orig_height() : 0
                        );

                        my_d3d11_update_viewport(d3d11, fire_rtv->get_inner(), &vp);
                    }

                    auto my_texture = as_wrapper<MyID3D11Texture2D>(srv->get_resource());
                    d3d11_texture_t texture = {};
                    if (my_texture) {
                        texture.handle = my_texture->get_inner();
                        texture.desc = my_texture->get_desc();
                    }

                    // Save game's scissor rects before
                    UINT num_scissors = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                    D3D11_RECT saved_scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
                    context->RSGetScissorRects(&num_scissors, saved_scissors);

                    // ZERO RECT BEFORE DRAW CALL - prevent slang multipass from offsetting to right side 
                    D3D11_RECT scissor = { 0, 0, (LONG)render_size.sc_width, (LONG)render_size.sc_height };
                    context->RSSetScissorRects(1, &scissor);

                    my_d3d11_gfx_frame(d3d11, &texture, device->get_frame_count());

                    // Restore game's scissor rects
                    context->RSSetScissorRects(num_scissors, saved_scissors);

                    context->IASetPrimitiveTopology(cached_pt);
                    context->IASetVertexBuffers(
                        0,
                        D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,
                        cached_vbs.ppVertexBuffers,
                        cached_vbs.pStrides,
                        cached_vbs.pOffsets
                    );
                    // restore index buffer after slang trashes IA state
                    context->IASetIndexBuffer(
                        cached_ib ? cached_ib->get_inner() : NULL,
                        cached_ib_format,
                        cached_ib_offset
                    );
                    context->OMSetBlendState(
                        cached_bs.pBlendState,
                        cached_bs.BlendFactor,
                        cached_bs.SampleMask
                    );
                    restore_pscbs();
                    context->VSSetConstantBuffers(0, MAX_CONSTANT_BUFFERS, cached_vscbs);
                    if (cached_il) context->IASetInputLayout(cached_il->get_inner());
                    restore_ps();
                    if (cached_vs) context->VSSetShader(cached_vs->get_inner(), NULL, 0);
                    context->GSSetShader(cached_gs, NULL, 0);
                    restore_vps();
                    restore_rtvs();

                    if (cached_tex) {
                        set_psss(device->get_filter_temp().sampler_linear);
                        set_srv(cached_tex->srv);
                        context->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
                    }

                    restore_pssss();
                    restore_pssrvs();
                };
                DIAG_LOG("[ENTRY] cached_rtv=%p cached_ps_hash=%08lx IndexCount=%u StartIndex=%u filter=%d filter_next=%d\n",
                    (void*)cached_rtv,
                    cached_ps ? cached_ps->get_bytecode_hash() : 0,
                    IndexCount,
                    StartIndexLocation,
                    (int)filter,
                    (int)filter_next
                );
            {
                DIAG_LOG("[PRE_FIRE] filter_next=%d filter=%d srv_tex=%p filter_state_rtv_tex=%p StartIndex=%u\n",
                    (int)filter_next,
                    (int)filter,
                    (void*)srv_tex,
                    (void*)filter_state.rtv_tex,
                    StartIndexLocation
                );
            }
            if (filter_next) {

                DIAG_LOG("[FIRE_GATE] rtv_orig_w=%u rtv_orig_h=%u cached_sc_w=%u cached_sc_h=%u filter_next=%d srv_tex=%p filter_state_rtv_tex=%p\n",
                    rtv_tex->get_orig_width(),
                    rtv_tex->get_orig_height(),
                    cached_size.sc_width,
                    cached_size.sc_height,
                    (int)filter_next,
                    (void*)srv_tex,
                    (void*)filter_state.rtv_tex
                );
                DIAG_LOG("[GATE2] StartIndexLocation=%u filter_state.start_index=%u match=%d\n",
                    StartIndexLocation,
                    filter_state.start_index_location,
                    (int)(StartIndexLocation == filter_state.start_index_location)
                );
                // orig_width/height check same as Draw — guards against
                // firing on intermediate RT passes that aren't the final output
                if (!filter_next &&
                    (rtv_tex->get_orig_width() != cached_size.sc_width ||
                        rtv_tex->get_orig_height() != cached_size.sc_height)
                    ) goto end;


                else if (filter_state.t1) {
                    auto d3d11 = device->get_d3d11_slot(device->get_config()->slang_active.load());
                    if (filter_ss_gba && d3d11) {
                        filter_ss = true;
                    }

                    if (filter_ss) {
                        {
#if DRAWINDEXED_DIAG
                            auto* fire_tex = as_wrapper<MyID3D11Texture2D>(filter_state.srv->get_resource());
#endif
                            DIAG_LOG("[FIRE] srv=%p tex=%p w=%u h=%u orig_w=%u orig_h=%u rtv_orig_w=%u rtv_orig_h=%u sc_w=%u sc_h=%u vp=(%f,%f,%f,%f) render_interp=%d\n",
                                (void*)filter_state.srv,
                                (void*)fire_tex,
                                fire_tex ? fire_tex->get_desc().Width : 0,
                                fire_tex ? fire_tex->get_desc().Height : 0,
                                fire_tex ? fire_tex->get_orig_width() : 0,
                                fire_tex ? fire_tex->get_orig_height() : 0,
                                rtv_tex->get_orig_width(),
                                rtv_tex->get_orig_height(),
                                render_size.sc_width,
                                render_size.sc_height,
                                render_vp.TopLeftX, render_vp.TopLeftY,
                                render_vp.Width, render_vp.Height,
                                (int)render_interp
                            );
                        }

                       draw_ss(
                           d3d11,
                           filter_state.srv,
                           &render_vp,
                           cached_rtv,        // captured at call time, is the swapchain RTV
                           render_interp ?
                           filter_state.ds ?
                           device->get_filter_temp().tex_nn_ds :
                           device->get_filter_temp().tex_nn_gba :
                           NULL
                       );
                       {
                           DIAG_LOG("[POST_DRAW_SS] done, filter_next going true, original draw at %u suppressed\n",
                               StartIndexLocation
                           );
                       }
                       fired_this_frame = true;
                       filter = false;
                       filter_next = false; 
                    }
                    else if (render_enhanced) {
                        draw_enhanced(
                            filter_state.ds ?
                            device->get_filter_temp().tex_1_ds :
                            device->get_filter_temp().tex_1_gba
                        );
                    }
                    else {
                        set_psss(device->get_filter_temp().sampler_linear);
                        context->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
                        restore_pssss();
                    }
                }
                else {
                    draw_nn(
                        filter_state.ds ?
                        device->get_filter_temp().tex_nn_ds :
                        device->get_filter_temp().tex_nn_gba
                    );
                }
            }
            else {
                // No rtv/srv 2x size relationship to enforce.
                // GBA_WIDTH x GBA_HEIGHT on SRV is already the gate above.
                // T2 hash is the arm condition for this game's filter.
                if (cached_ps->get_bytecode_hash() != PS_HASH_T2)
                    goto end;

                clear_filter();

                // T2 arms, t1=true always for GBA single path
                filter_state.ps = cached_ps; cached_ps->AddRef();
                filter_state.t1 = true;
                filter_state.vs = cached_vs;
                if (cached_vs) cached_vs->AddRef();
                filter_state.psss = psss;
                if (psss) psss->AddRef();
                filter_state.ds = ds;
                // store index location instead of vertex location
                filter_state.start_index_location = StartIndexLocation;
                filter_state.vertex_buffer = *cached_vbs.ppVertexBuffers;
                if (filter_state.vertex_buffer)
                    filter_state.vertex_buffer->AddRef();
                filter_state.vertex_stride = *cached_vbs.pStrides;
                filter_state.vertex_offset = *cached_vbs.pOffsets;
                filter_state.srv = srv; srv->AddRef();
                filter_state.rtv_tex = rtv_tex; rtv_tex->AddRef();

                {
                    DIAG_LOG("[ARM] hash=%08lx srv=%p tex=%p w=%u h=%u orig_w=%u orig_h=%u rtv_tex=%p rtv_w=%u rtv_h=%u rtv_orig_w=%u rtv_orig_h=%u filter_ss_gba=%d filter_next_would_be=%d\n",
                        cached_ps->get_bytecode_hash(),
                        (void*)srv,
                        (void*)srv_tex,
                        srv_tex ? srv_tex->get_desc().Width : 0,
                        srv_tex ? srv_tex->get_desc().Height : 0,
                        srv_tex ? srv_tex->get_orig_width() : 0,
                        srv_tex ? srv_tex->get_orig_height() : 0,
                        (void*)rtv_tex,
                        rtv_tex->get_desc().Width,
                        rtv_tex->get_desc().Height,
                        rtv_tex->get_orig_width(),
                        rtv_tex->get_orig_height(),
                        (int)filter_ss_gba,
                        (int)(filter_ss_gba ? 1 : (render_interp ? 0 : -1))
                    );
                }

                // t1=true: arm immediately if ss enabled, else arm for interp
                if (filter_ss_gba) filter = filter_next = true;
                else if (render_interp) filter = true;
            }
        }
    end:
        if (filter_next) goto done;
        {
            // T1 and T3 pass through — T1 is the fire pass caught
            // above via filter_next. T3 is the screen mask, never intercept.
            // T2 is the arm pass, handled in the else branch above.
            // All other hashes fall to draw_default.
            linear_conditions_begin();
            MyIndexedVertexBuffer_Logger vertices = {
                .input_layout = cached_il,
                .vertex_buffer = cached_vbs.vertex_buffers[0],
                .stride = cached_vbs.pStrides[0],
                .offset = cached_vbs.pOffsets[0],
                .index_buffer = cached_ib,
                .index_format = cached_ib_format,
                .index_offset = cached_ib_offset,
                .IndexCount = IndexCount,
                .StartIndexLocation = StartIndexLocation,
                .BaseVertexLocation = BaseVertexLocation,
            };
            LOG_MFUN(_,
                LogIf<1>{linear_restore},
                LOG_ARG(vertices),
                LOG_ARG(IndexCount),
                LOG_ARG(StartIndexLocation),
                LOG_ARG(BaseVertexLocation),
                LOG_ARG(linear_conditions)
            );
            DIAG_LOG("[PASSTHROUGH] hash=%08lx StartIndex=%u\n",
                cached_ps ? cached_ps->get_bytecode_hash() : 0,
                StartIndexLocation
            );
#if DRAWINDEXED_DIAG

            if (cached_ps && cached_ps->get_bytecode_hash() == PS_HASH_T1) {
                auto* rtv_res = cached_rtv ? (MyID3D11Texture2D*)cached_rtv->get_resource() : NULL;
                DIAG_LOG("[DFEEED] fired=%d StartIndex=%u rtv_orig_w=%u rtv_orig_h=%u\n",
                    (int)fired_this_frame,
                    StartIndexLocation,
                    rtv_res ? rtv_res->get_orig_width() : 0,
                    rtv_res ? rtv_res->get_orig_height() : 0
                );
            }

            if (fired_this_frame && cached_ps && cached_ps->get_bytecode_hash() == PS_HASH_T1) {
                // Log all SRV slots to find what the text PS is sampling from
                for (int i = 0; i < MAX_SHADER_RESOURCES; i++) {
                    auto* slot_srv = render_pssrvs[i];
                    if (!slot_srv) continue;
                    auto* slot_srv_wrapped = as_wrapper<MyID3D11ShaderResourceView>(slot_srv);
                    if (!slot_srv_wrapped) {
                        DIAG_LOG("[TEXT_SRV] slot=%d raw=%p (unwrapped)\n", i, (void*)slot_srv);
                        continue;
                    }
                    auto* slot_tex = as_wrapper<MyID3D11Texture2D, ID3D11Resource>(slot_srv_wrapped->get_resource());
                    DIAG_LOG("[TEXT_SRV] slot=%d srv=%p tex=%p w=%u h=%u orig_w=%u orig_h=%u\n",
                        i,
                        (void*)slot_srv_wrapped,
                        (void*)slot_tex,
                        slot_tex ? slot_tex->get_desc().Width : 0,
                        slot_tex ? slot_tex->get_desc().Height : 0,
                        slot_tex ? slot_tex->get_orig_width() : 0,
                        slot_tex ? slot_tex->get_orig_height() : 0
                    );
                }
                // Also log the RTV it's drawing into
                auto* rtv_res = cached_rtv ? (MyID3D11Texture2D*)cached_rtv->get_resource() : NULL;
                DIAG_LOG("[TEXT_RTV] cached_rtv=%p orig_w=%u orig_h=%u\n",
                    (void*)cached_rtv,
                    rtv_res ? rtv_res->get_orig_width() : 0,
                    rtv_res ? rtv_res->get_orig_height() : 0
                );
                // Also log blend state
                DIAG_LOG("[TEXT_BS] pBlendState=%p SampleMask=%08x BF=(%f,%f,%f,%f)\n",
                    (void*)cached_bs.pBlendState,
                    cached_bs.SampleMask,
                    cached_bs.BlendFactor[0],
                    cached_bs.BlendFactor[1],
                    cached_bs.BlendFactor[2],
                    cached_bs.BlendFactor[3]
                );
                if (cached_bs.pBlendState) {
                    D3D11_BLEND_DESC bd = {};
                    cached_bs.pBlendState->GetDesc(&bd);
                    DIAG_LOG("[TEXT_BD] BlendEnable=%d SrcBlend=%u DestBlend=%u BlendOp=%u SrcBlendAlpha=%u DestBlendAlpha=%u BlendOpAlpha=%u WriteMask=%02x\n",
                        bd.RenderTarget[0].BlendEnable,
                        bd.RenderTarget[0].SrcBlend,
                        bd.RenderTarget[0].DestBlend,
                        bd.RenderTarget[0].BlendOp,
                        bd.RenderTarget[0].SrcBlendAlpha,
                        bd.RenderTarget[0].DestBlendAlpha,
                        bd.RenderTarget[0].BlendOpAlpha,
                        bd.RenderTarget[0].RenderTargetWriteMask
                    );
                }
                for (int i = 0; i < 2; i++) {
                    ID3D11ShaderResourceView* raw_srv = render_pssrvs[i];
                    if (!raw_srv) continue;
                    ID3D11Resource* res = nullptr;
                    raw_srv->GetResource(&res);
                    if (!res) continue;
                    ID3D11Texture2D* tex = nullptr;
                    res->QueryInterface(IID_ID3D11Texture2D, (void**)&tex);
                    if (tex) {
                        D3D11_TEXTURE2D_DESC desc = {};
                        tex->GetDesc(&desc);
                        DIAG_LOG("[TEXT_RAW_SRV] slot=%d w=%u h=%u fmt=%u mips=%u bind=%u\n",
                            i, desc.Width, desc.Height, desc.Format, desc.MipLevels, desc.BindFlags);
                        tex->Release();
                    }
                    res->Release();
                }
            }

#endif

            //Use Key_PS to handle partial alpha keying around text edges + handle UI layer Widescreen mode scaling
            if (fired_this_frame &&
                cached_ps && cached_ps->get_bytecode_hash() == PS_HASH_T1) {            

                auto* rtv_res = cached_rtv ?
                    as_wrapper<MyID3D11Texture2D, ID3D11Resource>(cached_rtv->get_resource()) : NULL;

                bool rtv_matches = rtv_res ?
                    (rtv_res->get_orig_width() == cached_size.sc_width &&
                        rtv_res->get_orig_height() == cached_size.sc_height) :
                    (render_size.sc_width == cached_size.sc_width &&
                        render_size.sc_height == cached_size.sc_height);

                if (rtv_matches) {
                    auto& ft = device->get_filter_temp();
                    if (ft.key_ps && ft.key_bs && ft.key_cb && ft.key_vs) {
                        D3D11_MAPPED_SUBRESOURCE mapped = {};
                        if (SUCCEEDED(context->Map(ft.key_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                            float data[4] = { device->get_config()->key_threshold.load(),
                                              device->get_config()->key_sharpness.load(),
                                              0, 0 };
                            memcpy(mapped.pData, data, sizeof(data));
                            context->Unmap(ft.key_cb, 0);
                        }

                        if (device->get_config()->widescreen && ui_scale == 1.0f && ft.key_vs && ft.key_vs_cb) {
                            D3D11_MAPPED_SUBRESOURCE m = {};
                            if (SUCCEEDED(context->Map(ft.key_vs_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                                float data[4] = { device->get_config()->ws_scale.load(), 0, 0, 0 };
                                memcpy(m.pData, data, sizeof(data));
                                context->Unmap(ft.key_vs_cb, 0);
                            }
                            context->VSSetShader(ft.key_vs, NULL, 0);
                            context->VSSetConstantBuffers(1, 1, &ft.key_vs_cb);
                        }

                        context->PSSetShader(ft.key_ps, NULL, 0);
                        context->PSSetConstantBuffers(0, 1, &ft.key_cb);
                        context->OMSetBlendState(ft.key_bs, NULL, 0xFFFFFFFF);

                        // expand scissor before draw
                        UINT num_sc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                        D3D11_RECT saved_sc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
                        context->RSGetScissorRects(&num_sc, saved_sc);
                        D3D11_RECT full_scissor = { 0, 0, (LONG)render_size.sc_width, (LONG)render_size.sc_height };
                        context->RSSetScissorRects(1, &full_scissor);

                        if (cached_vscbs[0]) {
                            ID3D11Device* dev = nullptr;
                            context->GetDevice(&dev);
                            if (dev) {
                                D3D11_BUFFER_DESC bd = {};
                                cached_vscbs[0]->GetDesc(&bd);
                                D3D11_BUFFER_DESC sd = {};
                                sd.ByteWidth = std::min(bd.ByteWidth, 64u);
                                sd.Usage = D3D11_USAGE_STAGING;
                                sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                                ID3D11Buffer* staging = nullptr;
                                if (SUCCEEDED(dev->CreateBuffer(&sd, NULL, &staging))) {
                                    context->CopySubresourceRegion(staging, 0, 0, 0, 0, cached_vscbs[0], 0, nullptr);
                                    D3D11_MAPPED_SUBRESOURCE m = {};
                                    if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
                                        if (sd.ByteWidth >= 56)
                                            ui_scale = ((float*)m.pData)[13];
                                        DIAG_LOG("[T1_UI_SCALE] ui_scale=%f\n", ui_scale);
                                        context->Unmap(staging, 0);
                                    }
                                    staging->Release();
                                }
                                dev->Release();
                            }
                        }

                        context->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
                        context->RSSetScissorRects(num_sc, saved_sc);  // restore 
                        auto ps = cached_ps ? cached_ps->get_inner() : NULL;
                        context->PSSetShader(ps, NULL, 0);
                        context->PSSetConstantBuffers(0, MAX_CONSTANT_BUFFERS, render_pscbs);
                        context->OMSetBlendState(cached_bs.pBlendState, cached_bs.BlendFactor, cached_bs.SampleMask);
                        linear_conditions_end();
                        return;
                    }
                }
            }
            context->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
            linear_conditions_end();
            return;
        }
    done:
        LOG_MFUN(_,
            LOG_ARG(IndexCount),
            LOG_ARG(StartIndexLocation),
            LOG_ARG(BaseVertexLocation)
        );
    }

    Impl(
        ID3D11DeviceContext* inner_context,
        MyID3D11Device* device
    ) : IUNKNOWN_INIT(inner_context),
        context(inner_context),
        device(device)
    {
        if constexpr (ENABLE_LOGGER) {
            ID3D11Device* dev = NULL;
            inner_context->GetDevice(&dev);
            if (dev) {
                D3D11_BUFFER_DESC bt_desc = {
                    .ByteWidth = SO_B_LEN,
                    .Usage = D3D11_USAGE_DEFAULT,
                    .BindFlags = D3D11_BIND_STREAM_OUTPUT,
                };
                dev->CreateBuffer(&bt_desc, NULL, &so_bt);

                D3D11_BUFFER_DESC bs_desc = {
                    .ByteWidth = SO_B_LEN,
                    .Usage = D3D11_USAGE_STAGING,
                    .CPUAccessFlags = D3D11_CPU_ACCESS_READ
                };
                dev->CreateBuffer(&bs_desc, NULL, &so_bs);

                D3D11_QUERY_DESC q_desc = {
                    .Query = D3D11_QUERY_SO_STATISTICS
                };
                dev->CreateQuery(&q_desc, &so_q);

                dev->Release();
            }
        }
    }

    ~Impl() {
        magic = IMPL_MAGIC_DEAD;
        clear_filter();
        if (so_bt) so_bt->Release();
        if (so_bs) so_bs->Release();
        if (so_q) so_q->Release();
        if (context) context->Release();
    }
};

template<>
struct LogItem<LinearFilterConditions> {
    const LinearFilterConditions* linear_conditions;
    void log_item(Logger* l) const {
        l->log_struct_begin();
#define STRUCT linear_conditions
        l->log_struct_members_named(
            "alpha_discard",
            (int)STRUCT->alpha_discard
        );
        auto
            sd_it = STRUCT->samplers_descs.begin(),
            sd_it_end = STRUCT->samplers_descs.end();
        auto
            td_it = STRUCT->texs_descs.begin(),
            td_it_end = STRUCT->texs_descs.end();
        for (
            size_t i = 0;
            td_it != td_it_end && sd_it != sd_it_end;
            ++td_it, ++sd_it, ++i
            ) {
            auto sd = *sd_it;
            auto td = *td_it;
            if (td) {
#define LOG_DESC_MEMBER(m) do { \
    l->log_struct_sep(); \
    l->log_item(i); \
    l->log_struct_member_access(); \
    l->log_item(#m); \
    l->log_assign(); \
    l->log_item(DESC->m); \
} while (0)
#define DESC td
                LOG_DESC_MEMBER(Width);
                LOG_DESC_MEMBER(Height);
#undef DESC
            }
            if (sd) {
#define DESC sd
                LOG_DESC_MEMBER(Filter);
#undef DESC
            }
#undef LOG_DESC_MEMBER
        }
#undef STRUCT
        l->log_struct_end();
    }
};

MyID3D11DeviceContext::MyID3D11DeviceContext(
    ID3D11DeviceContext* inner,
    MyID3D11Device* device
) : impl(new Impl(inner, device)) {
    register_wrapper(this);
}

MyID3D11DeviceContext::~MyID3D11DeviceContext() {
    magic = CTX_MAGIC_DEAD;
    unregister_wrapper(this);
    delete impl;
}

HRESULT STDMETHODCALLTYPE MyID3D11DeviceContext::QueryInterface(
    REFIID riid,
    void** ppvObject
) {
    if (!ppvObject)
        return E_POINTER;

    *ppvObject = NULL;

    if (
        riid == __uuidof(IUnknown) ||
        riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11DeviceContext)
        ) {
        *ppvObject = static_cast<ID3D11DeviceContext*>(this);
        AddRef();
        return S_OK;
    }

    return impl->context->QueryInterface(riid, ppvObject);
}

ULONG STDMETHODCALLTYPE MyID3D11DeviceContext::AddRef() {
    return impl->context->AddRef();
}

ULONG STDMETHODCALLTYPE MyID3D11DeviceContext::Release() {

    ULONG ref = impl->context->Release();

    if (!ref) {
        delete this;
    }

    return ref;
}

void MyID3D11DeviceContext::sync_cached_size(UINT sc_width, UINT sc_height) {
    impl->cached_size.sc_width = sc_width;
    impl->cached_size.sc_height = sc_height;
}

void MyID3D11DeviceContext::sync_render_size(UINT sc_width, UINT sc_height) {
    impl->render_size.resize(sc_width, sc_height);
}

void MyID3D11DeviceContext::clear_filter() {
    impl->clear_filter();
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::GetDevice(
    ID3D11Device** ppDevice
) {
    LOG_MFUN();
    if (!ppDevice)
        return;
    *ppDevice = impl->device;
    if (*ppDevice)
        (*ppDevice)->AddRef();
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::Draw(
    UINT VertexCount,
    UINT StartVertexLocation
) {
    impl->context->Draw(VertexCount, StartVertexLocation);
}

template<typename InnerT, typename WrapperT>
static inline WrapperT* LookupLiveWrapperAndAddRef(
    std::unordered_map<InnerT*, WrapperT*>& cache,
    std::mutex& cache_mutex,
    InnerT* inner
) {
    if (!inner) return nullptr;

    WrapperT* wrapped = nullptr;
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = cache.find(inner);
        if (it != cache.end() && it->second && is_wrapper(it->second)) {
            wrapped = it->second;
            wrapped->AddRef(); 
        }
    }
    return wrapped;
}

template<typename InnerT, typename WrapperT>
static inline void RemapArrayFromCache(
    InnerT** ppItems,
    UINT count,
    std::unordered_map<InnerT*, WrapperT*>& cache,
    std::mutex& cache_mutex
) {
    if (!ppItems || count == 0) return;

    for (UINT i = 0; i < count; ++i) {
        InnerT* inner = ppItems[i];
        if (!inner) continue;

        WrapperT* wrapped = LookupLiveWrapperAndAddRef(cache, cache_mutex, inner);
        if (wrapped) {
            inner->Release();
            ppItems[i] = wrapped;
        }
    }
}

template<typename InnerT, typename WrapperT>
static inline void RemapSingleFromCache(
    InnerT** ppItem,
    std::unordered_map<InnerT*, WrapperT*>& cache,
    std::mutex& cache_mutex
) {
    if (!ppItem || !*ppItem) return;

    InnerT* inner = *ppItem;
    WrapperT* wrapped = LookupLiveWrapperAndAddRef(cache, cache_mutex, inner);
    if (wrapped) {
        inner->Release();
        *ppItem = wrapped;
    }
}

static inline void RemapSrvArrayToWrappers(
    ID3D11ShaderResourceView** ppViews,
    UINT numViews
) {
    if (!ppViews || numViews == 0) return;

    numViews = std::min(numViews, (UINT)D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);

    for (UINT i = 0; i < numViews; ++i) {
        ID3D11ShaderResourceView* inner = ppViews[i];
        if (!inner) continue;

        MyID3D11ShaderResourceView* wrapped = nullptr;
        {
            std::lock_guard<std::mutex> lock(cached_srvs_map_mutex);
            auto it = cached_srvs_map.find(inner);
            if (it != cached_srvs_map.end() && it->second && is_wrapper(it->second)) {
                wrapped = it->second;
                wrapped->AddRef(); 
            }
        }

        if (wrapped) {
            inner->Release();
            ppViews[i] = wrapped;
        }
    }
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::VSSetConstantBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer* const* ppConstantBuffers
) {
    if (NumBuffers == 0) {
        LOG_MFUN(_, LOG_ARG(StartSlot), LOG_ARG(NumBuffers));
        impl->context->VSSetConstantBuffers(StartSlot, 0, ppConstantBuffers);
        return;
    }

    MyID3D11Buffer* my_cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ID3D11Buffer* buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};

    NumBuffers = std::min(NumBuffers, (UINT)D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);

    if (ppConstantBuffers)
        for (UINT i = 0; i < NumBuffers; ++i)
            my_cbs[i] = as_wrapper<MyID3D11Buffer>(ppConstantBuffers[i]);
    auto constant_buffers = ppConstantBuffers ? my_cbs : nullptr;

    LOG_MFUN(_,
        LOG_ARG(StartSlot),
        LOG_ARG(NumBuffers),
        LOG_ARG_TYPE(constant_buffers, ArrayLoggerDeref, NumBuffers)
    );

    for (UINT i = 0; i < NumBuffers; ++i) {
        buffers[i] = (constant_buffers && constant_buffers[i]) ?
            constant_buffers[i]->get_inner() : nullptr;
    }

    memcpy(impl->cached_vscbs + StartSlot, buffers, sizeof(void*) * NumBuffers);
    impl->context->VSSetConstantBuffers(StartSlot, NumBuffers, buffers);
}


void STDMETHODCALLTYPE MyID3D11DeviceContext::PSSetShaderResources(
    UINT StartSlot,
    UINT NumViews,
    ID3D11ShaderResourceView* const* ppShaderResourceViews
) {
    if (NumViews == 0) {
        LOG_MFUN(_, LOG_ARG(StartSlot), LOG_ARG(NumViews));
        memset(impl->render_pssrvs, 0, sizeof(void*) * MAX_SHADER_RESOURCES);
        memset(impl->cached_pssrvs, 0, sizeof(void*) * MAX_SHADER_RESOURCES);
        impl->context->PSSetShaderResources(0, 0, ppShaderResourceViews);
        return;
    }
    NumViews = std::min(NumViews, (UINT)D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);

    MyID3D11ShaderResourceView* my_srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11ShaderResourceView* srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    int srv_types[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};

    if (ppShaderResourceViews)
        for (UINT i = 0; i < NumViews; ++i)
            my_srvs[i] = as_wrapper<MyID3D11ShaderResourceView>(ppShaderResourceViews[i]);
    auto shader_resource_views = ppShaderResourceViews ? my_srvs : nullptr;

    for (UINT i = 0; i < NumViews; ++i) {
        auto my_srv = shader_resource_views ? shader_resource_views[i] : nullptr;
        srvs[i] = my_srv ? my_srv->get_inner() :
            (ppShaderResourceViews ? ppShaderResourceViews[i] : nullptr);
        if (LOG_STARTED && my_srv &&
            my_srv->get_desc().ViewDimension == D3D_SRV_DIMENSION_TEXTURE2D) {
            auto tex = as_wrapper<MyID3D11Texture2D, ID3D11Resource>(my_srv->get_resource());
            if (tex &&
                tex->get_orig_width() == impl->cached_size.sc_width &&
                tex->get_orig_height() == impl->cached_size.sc_height)
                srv_types[i] = 1;
        }
    }
    LOG_MFUN(_,
        LOG_ARG(StartSlot),
        LOG_ARG(NumViews),
        LOG_ARG_TYPE(shader_resource_views, ArrayLoggerDeref, NumViews),
        LOG_ARG_TYPE(srv_types, ArrayLoggerDeref, NumViews)
    );
    memcpy(impl->render_pssrvs + StartSlot, srvs, sizeof(void*) * NumViews);
    memcpy(impl->cached_pssrvs + StartSlot, my_srvs, sizeof(void*) * NumViews);
    impl->context->PSSetShaderResources(StartSlot, NumViews, srvs);
}

bool MyID3D11DeviceContext::get_ingame_filter_seen() const {
    return impl->ingame_filter_seen;
}

bool MyID3D11DeviceContext::get_gba_seen() const {
    return impl->gba_seen_this_scan;
}

float MyID3D11DeviceContext::get_ui_scale() const {
    return impl->ui_scale;
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::PSSetShader(
    ID3D11PixelShader* pPixelShader,
    ID3D11ClassInstance* const* ppClassInstances,
    UINT NumClassInstances
) {

    auto wrapped = as_wrapper<MyID3D11PixelShader>(pPixelShader);
    auto unwrapped = unwrap<MyID3D11PixelShader>(pPixelShader);

    LOG_MFUN(_,
        LOG_ARG(pPixelShader),
        LOG_ARG_TYPE(wrapped ? wrapped->get_bytecode_hash() : 0, NumHexLogger)
    );

    if (wrapped) {
        auto hash = wrapped->get_bytecode_hash();

        if (hash == 0xc9ca17c9) {
            // scan start - reset the per-scan T2 tracker
            impl->t2_scan_seen = false;
            impl->gba_seen_this_scan = false;
        }
        else if (hash == PS_HASH_T2) {
            // T2 seen this scan
            impl->t2_scan_seen = true;
            impl->ingame_filter_seen = true;
        }
        else if (hash == 0x1034fa8f) {
            // scan end - if T2 was absent this scan, filter is off
            if (!impl->t2_scan_seen) {
                impl->ingame_filter_seen = false;
            }
        }
    }

    impl->cached_ps = wrapped;

    auto safeClassInstances = ppClassInstances;
    auto safeNumClassInstances = NumClassInstances;

    if (pPixelShader == NULL) {
        safeClassInstances = NULL;
        safeNumClassInstances = 0;
    }

    impl->context->PSSetShader(unwrapped, safeClassInstances, safeNumClassInstances);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::PSSetSamplers(
    UINT StartSlot,
    UINT NumSamplers,
    ID3D11SamplerState* const* ppSamplers
) {
    // 1. If ppSamplers is NULL, don't use ArrayLoggerDeref.
    if (ppSamplers) {
        LOG_MFUN(_,
            LOG_ARG(StartSlot),
            LOG_ARG(NumSamplers),
            LOG_ARG_TYPE(ppSamplers, ArrayLoggerDeref, NumSamplers)
        );
    }
    else {
        LOG_MFUN(_,
            LOG_ARG(StartSlot),
            LOG_ARG(NumSamplers),
            LOG_ARG(ppSamplers) // log the NULL pointer
        );
    }

    // Use a fixed size safe array instead of a VLA
    ID3D11SamplerState* sss[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = { NULL };

    // 2. Only unwrap if ppSamplers actually exists
    if (ppSamplers && NumSamplers > 0) {
        for (UINT i = 0; i < NumSamplers; ++i) {
            auto sampler = as_wrapper<MyID3D11SamplerState>(ppSamplers[i]);
            sss[i] = sampler ?
                (impl->render_linear && sampler->get_linear() ?
                    sampler->get_linear() : sampler->get_inner()) :
                ppSamplers[i];  // pass through unwrapped Noesis sampler directly
        }
    }

    if (NumSamplers > 0) {
        memcpy(
            impl->render_pssss + StartSlot,
            sss, // sss is safely populated with either the unwrapped samplers or NULLs
            sizeof(void*) * NumSamplers
        );

        // 3. Protect memcpy from copying a NULL pointer!
        if (ppSamplers) {
            for (UINT i = 0; i < NumSamplers; ++i)
                impl->cached_pssss[StartSlot + i] =
                as_wrapper<MyID3D11SamplerState>(ppSamplers[i]);
        }
        else {
            memset(impl->cached_pssss + StartSlot, 0, sizeof(void*) * NumSamplers);
        }
    }
    else {
        memset(impl->render_pssss, 0, sizeof(void*) * MAX_SAMPLERS);
        memset(impl->cached_pssss, 0, sizeof(void*) * MAX_SAMPLERS);
    }

    impl->context->PSSetSamplers(
        StartSlot,
        NumSamplers,
        NumSamplers ? sss : NULL // Always pass safe array or NULL
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::VSSetShader(
    ID3D11VertexShader* pVertexShader,
    ID3D11ClassInstance* const* ppClassInstances,
    UINT NumClassInstances
) {
    auto wrapped = as_wrapper<MyID3D11VertexShader>(pVertexShader);
    auto unwrapped = unwrap<MyID3D11VertexShader>(pVertexShader);

    impl->cached_vs = wrapped;

    auto safeClassInstances = ppClassInstances;
    auto safeNumClassInstances = NumClassInstances;

    if (pVertexShader == NULL) {
        safeClassInstances = NULL;
        safeNumClassInstances = 0;
    }

    impl->context->VSSetShader(unwrapped, safeClassInstances, safeNumClassInstances);

}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DrawIndexed(
    UINT IndexCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation
) {
    impl->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::PSSetConstantBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer* const* ppConstantBuffers
) {
    if (NumBuffers == 0) {
        LOG_MFUN(_, LOG_ARG(StartSlot), LOG_ARG(NumBuffers));
        impl->context->PSSetConstantBuffers(StartSlot, 0, ppConstantBuffers);
        return;
    }

    MyID3D11Buffer* my_cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ID3D11Buffer* buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};

    NumBuffers = std::min(NumBuffers, (UINT)D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);

    if (ppConstantBuffers)
        for (UINT i = 0; i < NumBuffers; ++i)
            my_cbs[i] = as_wrapper<MyID3D11Buffer>(ppConstantBuffers[i]);
    auto constant_buffers = ppConstantBuffers ? my_cbs : nullptr;

    LOG_MFUN(_,
        LOG_ARG(StartSlot),
        LOG_ARG(NumBuffers),
        LOG_ARG_TYPE(constant_buffers, ArrayLoggerDeref, NumBuffers)
    );

    for (UINT i = 0; i < NumBuffers; ++i) {
        buffers[i] = (constant_buffers && constant_buffers[i]) ?
            constant_buffers[i]->get_inner() : nullptr;
    }

    memcpy(impl->render_pscbs + StartSlot, buffers, sizeof(void*) * NumBuffers);
    memcpy(impl->cached_pscbs + StartSlot, constant_buffers, sizeof(void*) * NumBuffers);
    impl->context->PSSetConstantBuffers(StartSlot, NumBuffers, buffers);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::IASetInputLayout(
    ID3D11InputLayout* pInputLayout
) {
    auto input_layout = as_wrapper<MyID3D11InputLayout>(pInputLayout);
    LOG_MFUN(_,
        LOG_ARG(input_layout)
    );
    impl->cached_il = input_layout;
    impl->context->IASetInputLayout(
        unwrap<MyID3D11InputLayout>(pInputLayout)
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::IASetVertexBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer* const* ppVertexBuffers,
    const UINT* pStrides,
    const UINT* pOffsets
) {
    MyID3D11Buffer* my_vbs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11Buffer* buffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};

    if (StartSlot >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) return;
    NumBuffers = std::min(
        NumBuffers,
        (UINT)D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - StartSlot
    );

    if (ppVertexBuffers) {
        for (UINT i = 0; i < NumBuffers; ++i) {
            ID3D11Buffer* in = ppVertexBuffers[i];
            MyID3D11Buffer* w = as_wrapper<MyID3D11Buffer>(in);
            my_vbs[i] = w;
            buffers[i] = w ? w->get_inner() : in; // keep raw buffers alive
        }
    }

    auto vertex_buffers = ppVertexBuffers ? my_vbs : nullptr;

    LOG_MFUN(_,
        LOG_ARG(StartSlot),
        LOG_ARG(NumBuffers),
        LOG_ARG_TYPE(vertex_buffers, ArrayLoggerDeref, NumBuffers),
        LOG_ARG_TYPE(pStrides, ArrayLoggerDeref, NumBuffers),
        LOG_ARG_TYPE(pOffsets, ArrayLoggerDeref, NumBuffers)
    );

    if (vertex_buffers) {
        for (UINT i = 0; i < NumBuffers; ++i) {
            buffers[i] = vertex_buffers[i] ? vertex_buffers[i]->get_inner() : nullptr;
        }
    }

    if (NumBuffers) {
        if (vertex_buffers) {
            memcpy(
                impl->cached_vbs.vertex_buffers + StartSlot,
                vertex_buffers,
                sizeof(void*) * NumBuffers
            );
        }
        else {
            memset(
                impl->cached_vbs.vertex_buffers + StartSlot,
                0,
                sizeof(void*) * NumBuffers
            );
        }

        memcpy(
            impl->cached_vbs.ppVertexBuffers + StartSlot,
            buffers,
            sizeof(void*) * NumBuffers
        );

        if (pStrides) {
            memcpy(
                impl->cached_vbs.pStrides + StartSlot,
                pStrides,
                sizeof(UINT) * NumBuffers
            );
        }
        else {
            memset(
                impl->cached_vbs.pStrides + StartSlot,
                0,
                sizeof(UINT) * NumBuffers
            );
        }

        if (pOffsets) {
            memcpy(
                impl->cached_vbs.pOffsets + StartSlot,
                pOffsets,
                sizeof(UINT) * NumBuffers
            );
        }
        else {
            memset(
                impl->cached_vbs.pOffsets + StartSlot,
                0,
                sizeof(UINT) * NumBuffers
            );
        }
    }

    impl->context->IASetVertexBuffers(
        StartSlot,
        NumBuffers,
        NumBuffers ? buffers : ppVertexBuffers,
        pStrides,
        pOffsets
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::IASetIndexBuffer(
    ID3D11Buffer* pIndexBuffer,
    DXGI_FORMAT Format,
    UINT Offset
) {
    MyID3D11Buffer* index_buffer = as_wrapper<MyID3D11Buffer>(pIndexBuffer);
    impl->cached_ib = index_buffer;
    impl->cached_ib_format = Format;
    impl->cached_ib_offset = Offset;

    LOG_MFUN(_,
        LOG_ARG(index_buffer),
        LOG_ARG(Format),
        LOG_ARG(Offset)
    );

    ID3D11Buffer* inner = index_buffer ? index_buffer->get_inner() : pIndexBuffer;
    impl->context->IASetIndexBuffer(inner, Format, Offset);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DrawIndexedInstanced(
    UINT IndexCountPerInstance,
    UINT InstanceCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation,
    UINT StartInstanceLocation
) {
    impl->set_render_vp();
    impl->linear_conditions_begin();
    auto& linear_conditions = impl->linear_conditions;
    LOG_MFUN(_,
        LOG_ARG(IndexCountPerInstance),
        LOG_ARG(InstanceCount),
        LOG_ARG(StartIndexLocation),
        LOG_ARG(BaseVertexLocation),
        LOG_ARG(StartInstanceLocation),
        LOG_ARG(linear_conditions)
    );
    impl->context->DrawIndexedInstanced(
        IndexCountPerInstance,
        InstanceCount,
        StartIndexLocation,
        BaseVertexLocation,
        StartInstanceLocation
    );
    impl->linear_conditions_end();
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DrawInstanced(
    UINT VertexCountPerInstance,
    UINT InstanceCount,
    UINT StartVertexLocation,
    UINT StartInstanceLocation
) {
    impl->set_render_vp();
    impl->linear_conditions_begin();
    auto& linear_conditions = impl->linear_conditions;
    LOG_MFUN(_,
        LOG_ARG(VertexCountPerInstance),
        LOG_ARG(InstanceCount),
        LOG_ARG(StartVertexLocation),
        LOG_ARG(StartInstanceLocation),
        LOG_ARG(linear_conditions)
    );
    impl->context->DrawInstanced(
        VertexCountPerInstance,
        InstanceCount,
        StartVertexLocation,
        StartInstanceLocation
    );
    impl->linear_conditions_end();
}


void STDMETHODCALLTYPE MyID3D11DeviceContext::GSSetConstantBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer* const* ppConstantBuffers
) {
    MyID3D11Buffer* my_cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    if (ppConstantBuffers)
        for (UINT i = 0; i < NumBuffers; ++i)
            my_cbs[i] = as_wrapper<MyID3D11Buffer>(ppConstantBuffers[i]);
    auto constant_buffers = ppConstantBuffers ? my_cbs : nullptr;

    ID3D11Buffer* buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};

    NumBuffers = std::min(NumBuffers, (UINT)D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
    for (UINT i = 0; i < NumBuffers; ++i) {
        buffers[i] = constant_buffers[i] ? constant_buffers[i]->get_inner() : nullptr;
    }

    LOG_MFUN(_,
        LOG_ARG(StartSlot),
        LOG_ARG(NumBuffers),
        LOG_ARG_TYPE(constant_buffers, ArrayLoggerDeref, NumBuffers)
    );

    impl->context->GSSetConstantBuffers(
        StartSlot,
        NumBuffers,
        NumBuffers ? buffers : ppConstantBuffers
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::GSSetShader(
    ID3D11GeometryShader* pShader,
    ID3D11ClassInstance* const* ppClassInstances,
    UINT NumClassInstances
) {
    LOG_MFUN(_,
        LOG_ARG(pShader)
    );
    impl->cached_gs = pShader;

    impl->context->GSSetShader(
        pShader,
        ppClassInstances,
        NumClassInstances
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::IASetPrimitiveTopology(
    D3D11_PRIMITIVE_TOPOLOGY Topology
) {
    LOG_MFUN(_,
        LOG_ARG(Topology)
    );
    impl->context->IASetPrimitiveTopology(Topology);
    if constexpr (ENABLE_SLANG_SHADER) {
        impl->cached_pt = Topology;
    }
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::VSSetShaderResources(
    UINT StartSlot,
    UINT NumViews,
    ID3D11ShaderResourceView* const* ppShaderResourceViews
) {
    auto shader_resource_views = (MyID3D11ShaderResourceView* const*)ppShaderResourceViews;

    ID3D11ShaderResourceView* srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};

    NumViews = std::min(NumViews, (UINT)D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
    for (UINT i = 0; i < NumViews; ++i) {
        srvs[i] = shader_resource_views[i] ? shader_resource_views[i]->get_inner() : nullptr;
    }

    LOG_MFUN(_,
        LOG_ARG(StartSlot),
        LOG_ARG(NumViews),
        LOG_ARG_TYPE(shader_resource_views, ArrayLoggerDeref, NumViews)
    );

    impl->context->VSSetShaderResources(
        StartSlot,
        NumViews,
        NumViews ? srvs : ppShaderResourceViews
    );
}


void STDMETHODCALLTYPE MyID3D11DeviceContext::VSSetSamplers(
    UINT StartSlot,
    UINT NumSamplers,
    ID3D11SamplerState* const* ppSamplers
) {
    LOG_MFUN();
    ID3D11SamplerState* sss[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};

    if (ppSamplers) {
        NumSamplers = std::min(NumSamplers, (UINT)D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT);
        for (UINT i = 0; i < NumSamplers; ++i) {
            sss[i] = ppSamplers[i] ? unwrap<MyID3D11SamplerState>(ppSamplers[i]) : nullptr;
        }
    }

    impl->context->VSSetSamplers(
        StartSlot,
        NumSamplers,
        ppSamplers ? sss : nullptr
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::SetPredication(
    ID3D11Predicate* pPredicate,
    WINBOOL PredicateValue
) {
    LOG_MFUN();
    impl->context->SetPredication(
        pPredicate,
        PredicateValue
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::GSSetShaderResources(
    UINT StartSlot,
    UINT NumViews,
    ID3D11ShaderResourceView* const* ppShaderResourceViews
) {
    auto shader_resource_views = (MyID3D11ShaderResourceView* const*)ppShaderResourceViews;

    ID3D11ShaderResourceView* srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    NumViews = std::min(NumViews, (UINT)D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);

    for (UINT i = 0; i < NumViews; ++i) {
        auto in = ppShaderResourceViews ? ppShaderResourceViews[i] : nullptr;
        auto w = as_wrapper<MyID3D11ShaderResourceView>(in);
        srvs[i] = w ? w->get_inner() : in;
    }

    impl->context->GSSetShaderResources(
        StartSlot,
        NumViews,
        ppShaderResourceViews ? srvs : nullptr
    );

    LOG_MFUN(_,
        LOG_ARG(StartSlot),
        LOG_ARG(NumViews),
        LOG_ARG_TYPE(shader_resource_views, ArrayLoggerDeref, NumViews)
    );

}

void STDMETHODCALLTYPE MyID3D11DeviceContext::GSSetSamplers(
    UINT StartSlot,
    UINT NumSamplers,
    ID3D11SamplerState* const* ppSamplers
) {
    LOG_MFUN();
    ID3D11SamplerState* sss[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};

    NumSamplers = std::min(NumSamplers, (UINT)D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT);
    for (UINT i = 0; i < NumSamplers; ++i) {
        sss[i] = ppSamplers[i] ? unwrap<MyID3D11SamplerState>(ppSamplers[i]) : nullptr;
    }

    impl->context->GSSetSamplers(
        StartSlot,
        NumSamplers,
        NumSamplers ? sss : ppSamplers
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::OMSetRenderTargets(
    UINT NumViews,
    ID3D11RenderTargetView* const* ppRenderTargetViews,
    ID3D11DepthStencilView* pDepthStencilView
) {
    auto* depth_stencil_view = as_wrapper<MyID3D11DepthStencilView>(pDepthStencilView);
    ID3D11DepthStencilView* real_dsv = depth_stencil_view ? depth_stencil_view->get_inner() : pDepthStencilView;

    NumViews = std::min(NumViews, (UINT)D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);
    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    MyID3D11RenderTargetView* my_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};

    for (UINT i = 0; i < NumViews; ++i) {
        ID3D11RenderTargetView* v = ppRenderTargetViews ? ppRenderTargetViews[i] : nullptr;
        if (!v) continue;

        my_rtvs[i] = as_wrapper<MyID3D11RenderTargetView>(v);
        rtvs[i] = my_rtvs[i] ? my_rtvs[i]->get_inner() : v;

        if (my_rtvs[i] &&
            my_rtvs[i]->get_desc().ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D) {
            impl->device->set_render_tex_views_and_update(my_rtvs[i]->get_resource(), true);
        }
    }

    LOG_MFUN(_,
        LOG_ARG(NumViews),
        LOG_ARG_TYPE(my_rtvs, ArrayLoggerDeref, NumViews),
        LOG_ARG(depth_stencil_view)
    );

    impl->reset_render_vp();
    impl->render_width = 0;
    impl->render_height = 0;
    impl->render_orig_width = 0;
    impl->render_orig_height = 0;
    impl->need_render_vp = false;

    impl->cached_rtv = NumViews ? my_rtvs[0] : NULL;
    auto dsv = impl->cached_dsv = depth_stencil_view;

    if (dsv && dsv->get_desc().ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2D) {
        impl->device->set_render_tex_views_and_update(dsv->get_resource());
    }

    impl->context->OMSetRenderTargets(
        NumViews,
        NumViews ? rtvs : ppRenderTargetViews,
        real_dsv
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::OMSetBlendState(
    ID3D11BlendState* pBlendState,
    const FLOAT BlendFactor[4],
    UINT SampleMask
) {
    LOG_MFUN(_,
        LOG_ARG(pBlendState),
        LOG_ARG_TYPE(BlendFactor, ArrayLoggerDeref, 4),
        LOG_ARG_TYPE(SampleMask, NumHexLogger)
    );
    impl->context->OMSetBlendState(
        pBlendState,
        BlendFactor,
        SampleMask
    );
    if constexpr (ENABLE_SLANG_SHADER) {
        impl->cached_bs.pBlendState = pBlendState;
        if (BlendFactor) {
            impl->cached_bs.BlendFactor[0] = BlendFactor[0];
            impl->cached_bs.BlendFactor[1] = BlendFactor[1];
            impl->cached_bs.BlendFactor[2] = BlendFactor[2];
            impl->cached_bs.BlendFactor[3] = BlendFactor[3];
        }
        else {
            // D3D11 spec: NULL BlendFactor is treated as {1,1,1,1}
            impl->cached_bs.BlendFactor[0] = 1.0f;
            impl->cached_bs.BlendFactor[1] = 1.0f;
            impl->cached_bs.BlendFactor[2] = 1.0f;
            impl->cached_bs.BlendFactor[3] = 1.0f;
        }
        impl->cached_bs.SampleMask = SampleMask;
    }
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::OMSetDepthStencilState(
    ID3D11DepthStencilState* pDepthStencilState,
    UINT StencilRef
) {
    auto depth_stencil_state =
        as_wrapper<MyID3D11DepthStencilState>(pDepthStencilState);
    impl->cached_dss = depth_stencil_state;
    LOG_MFUN(_,
        LOG_ARG(depth_stencil_state),
        LOG_ARG_TYPE(StencilRef, NumHexLogger)
    );
    impl->context->OMSetDepthStencilState(
        unwrap<MyID3D11DepthStencilState>(pDepthStencilState),
        StencilRef
    );
}


void STDMETHODCALLTYPE MyID3D11DeviceContext::SOSetTargets(
    UINT NumBuffers,
    ID3D11Buffer* const* ppSOTargets,
    const UINT* pOffsets
) {
    LOG_MFUN();
    ID3D11Buffer* buffers[D3D11_SO_BUFFER_SLOT_COUNT] = {};

    NumBuffers = std::min(NumBuffers, (UINT)D3D11_SO_BUFFER_SLOT_COUNT);
    for (UINT i = 0; i < NumBuffers; ++i) {
        buffers[i] = ppSOTargets[i] ? unwrap<MyID3D11Buffer>(ppSOTargets[i]) : nullptr;
    }

    impl->context->SOSetTargets(
        NumBuffers,
        NumBuffers ? buffers : ppSOTargets,
        pOffsets
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DrawAuto(
) {
    impl->set_render_vp();
    impl->linear_conditions_begin();
    auto& linear_conditions = impl->linear_conditions;
    LOG_MFUN(_,
        LOG_ARG(linear_conditions)
    );
    impl->context->DrawAuto();
    impl->linear_conditions_end();
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::RSSetState(
    ID3D11RasterizerState* pRasterizerState
) {
    LOG_MFUN();
    impl->context->RSSetState(
        pRasterizerState
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::RSSetViewports(
    UINT NumViewports,
    const D3D11_VIEWPORT* pViewports
) {
    LOG_MFUN(_,
        LOG_ARG(NumViewports),
        LOG_ARG_TYPE(pViewports, ArrayLoggerRef, NumViewports)
    );
    if (NumViewports) {
        impl->cached_vp = *pViewports;
    }
    else {
        impl->cached_vp = {};
    }
    impl->render_vp = impl->cached_vp;
    impl->is_render_vp = false;
    impl->context->RSSetViewports(
        NumViewports,
        pViewports
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::RSSetScissorRects(
    UINT NumRects,
    const D3D11_RECT* pRects
) {
    LOG_MFUN();
    impl->context->RSSetScissorRects(
        NumRects,
        pRects
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::CopySubresourceRegion(
    ID3D11Resource* pDstResource,
    UINT DstSubresource,
    UINT DstX,
    UINT DstY,
    UINT DstZ,
    ID3D11Resource* pSrcResource,
    UINT SrcSubresource,
    const D3D11_BOX* pSrcBox
) {
    LOG_MFUN(_,
        LOG_ARG_TYPE(pDstResource, MyID3D11Resource_Logger),
        LOG_ARG(DstSubresource),
        LOG_ARG(DstX),
        LOG_ARG(DstY),
        LOG_ARG(DstZ),
        LOG_ARG_TYPE(pSrcResource, MyID3D11Resource_Logger),
        LOG_ARG(SrcSubresource),
        LOG_ARG(pSrcBox)
    );
    D3D11_RESOURCE_DIMENSION dstType;
    pDstResource->GetType(&dstType);
    D3D11_RESOURCE_DIMENSION srcType;
    pSrcResource->GetType(&srcType);
    if (dstType != srcType) return;
    auto box = pSrcBox ? *pSrcBox : D3D11_BOX{};
    switch (dstType) {
    case D3D11_RESOURCE_DIMENSION_BUFFER: {
        auto buffer = as_wrapper<MyID3D11Buffer>(pDstResource);
        auto buffer_src = as_wrapper<MyID3D11Buffer>(pSrcResource);
        if (buffer && buffer_src) {
            if (buffer->get_cached()) {
                if (buffer_src->get_cached() && buffer_src->get_cached_state()) {
                    if (pSrcBox) {
                        if (
                            buffer->get_cached_state() &&
                            pSrcBox->left < pSrcBox->right &&
                            pSrcBox->top < pSrcBox->bottom &&
                            pSrcBox->front < pSrcBox->back
                            ) {
                            memcpy(
                                buffer->get_cached() + DstX,
                                buffer_src->get_cached() + pSrcBox->left,
                                std::min(
                                    pSrcBox->right,
                                    buffer_src->get_desc().ByteWidth
                                ) - pSrcBox->left
                            );
                        }
                    }
                    else {
                        memcpy(
                            buffer->get_cached(),
                            buffer_src->get_cached(),
                            std::min(
                                buffer->get_desc().ByteWidth,
                                buffer_src->get_desc().ByteWidth
                            )
                        );
                        buffer->get_cached_state() = true;
                    }
                }
                else {
                    buffer->get_cached_state() = false;
                }
            }
        }
        pDstResource = unwrap<MyID3D11Buffer>(pDstResource);
        pSrcResource = unwrap<MyID3D11Buffer>(pSrcResource);
        break;
    }

    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        pDstResource = unwrap<MyID3D11Texture1D>(pDstResource);
        pSrcResource = unwrap<MyID3D11Texture1D>(pSrcResource);
        break;

    case D3D11_RESOURCE_DIMENSION_TEXTURE2D: {
        auto tex_dst = as_wrapper<MyID3D11Texture2D>(pDstResource);
        auto tex_src = as_wrapper<MyID3D11Texture2D>(pSrcResource);
        if (tex_dst && impl->device->set_render_tex_views_and_update(tex_dst)) {
            DstX =
                DstX *
                tex_dst->get_desc().Width /
                tex_dst->get_orig_width();
            DstY =
                DstY *
                tex_dst->get_desc().Height /
                tex_dst->get_orig_height();
        }
        if (
            tex_src &&
            impl->device->set_render_tex_views_and_update(tex_src) &&
            pSrcBox
            ) {
            box.left =
                pSrcBox->left *
                tex_src->get_desc().Width /
                tex_src->get_orig_width();
            box.top =
                pSrcBox->top *
                tex_src->get_desc().Height /
                tex_src->get_orig_height();
            box.right =
                pSrcBox->right *
                tex_src->get_desc().Width /
                tex_src->get_orig_width();
            box.bottom =
                pSrcBox->bottom *
                tex_src->get_desc().Height /
                tex_src->get_orig_height();
        }
        pDstResource = unwrap<MyID3D11Texture2D>(pDstResource);
        pSrcResource = unwrap<MyID3D11Texture2D>(pSrcResource);
        break;
    }

    default:
        break;
    }
    impl->context->CopySubresourceRegion(
        pDstResource,
        DstSubresource,
        DstX,
        DstY,
        DstZ,
        pSrcResource,
        SrcSubresource,
        pSrcBox ? &box : NULL
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::CopyResource(
    ID3D11Resource* pDstResource,
    ID3D11Resource* pSrcResource
) {
    LOG_MFUN(_,
        LOG_ARG_TYPE(pDstResource, MyID3D11Resource_Logger),
        LOG_ARG_TYPE(pSrcResource, MyID3D11Resource_Logger)
    );
    impl->device->set_render_tex_views_and_update(pDstResource);
    impl->device->set_render_tex_views_and_update(pSrcResource);
    D3D11_RESOURCE_DIMENSION dstType;
    pDstResource->GetType(&dstType);
    D3D11_RESOURCE_DIMENSION srcType;
    pSrcResource->GetType(&srcType);
    if (dstType != srcType) return;
    switch (dstType) {
    case D3D11_RESOURCE_DIMENSION_BUFFER: {
        auto buffer = as_wrapper<MyID3D11Buffer>(pDstResource);
        auto buffer_src = as_wrapper<MyID3D11Buffer>(pSrcResource);
        if (buffer && buffer_src) {
            if (
                buffer_src->get_cached() && buffer_src->get_cached_state() &&
                buffer->get_desc().ByteWidth == buffer_src->get_desc().ByteWidth
                ) {
                memcpy(
                    buffer->get_cached(),
                    buffer_src->get_cached(),
                    buffer->get_desc().ByteWidth
                );
                buffer->get_cached_state() = true;
            }
            else {
                buffer->get_cached_state() = false;
            }
        }
        pDstResource = buffer ? buffer->get_inner() : pDstResource;
        pSrcResource = buffer_src ? buffer_src->get_inner() : pSrcResource;
        break;
    }

    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        pDstResource =
            unwrap<MyID3D11Texture1D>(pDstResource);
        pSrcResource =
            unwrap<MyID3D11Texture1D>(pSrcResource);
        break;

    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
        pDstResource =
            unwrap<MyID3D11Texture2D>(pDstResource);
        pSrcResource =
            unwrap<MyID3D11Texture2D>(pSrcResource);
        break;

    default:
        break;
    }
    impl->context->CopyResource(
        pDstResource,
        pSrcResource
    );
}
void STDMETHODCALLTYPE MyID3D11DeviceContext::UpdateSubresource(
    ID3D11Resource* pDstResource,
    UINT DstSubresource,
    const D3D11_BOX* pDstBox,
    const void* pSrcData,
    UINT SrcRowPitch,
    UINT SrcDepthPitch
) {
    D3D11_RESOURCE_DIMENSION dstType;
    pDstResource->GetType(&dstType);
    ID3D11Resource* resource_inner;
    int tex_type = 0;
    switch (dstType) {
    case D3D11_RESOURCE_DIMENSION_BUFFER: {
        auto buffer = as_wrapper<MyID3D11Buffer>(pDstResource);
        if (buffer && buffer->get_cached()) {
            if (pDstBox) {
                if (
                    buffer->get_cached_state() && pSrcData &&
                    pDstBox->left < pDstBox->right &&
                    pDstBox->top < pDstBox->bottom &&
                    pDstBox->front < pDstBox->back
                    ) {
                    memcpy(
                        buffer->get_cached() +
                        pDstBox->left,
                        pSrcData,
                        std::min(
                            pDstBox->right,
                            buffer->get_desc().ByteWidth
                        ) - pDstBox->left
                    );
                }
            }
            else {
                if (pSrcData) {
                    memcpy(
                        buffer->get_cached(),
                        pSrcData,
                        buffer->get_desc().ByteWidth
                    );
                    buffer->get_cached_state() = true;
                }
            }
        }
        resource_inner = buffer ? buffer->get_inner() : pDstResource;
        break;
    }

    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        resource_inner =
            unwrap<MyID3D11Texture1D>(pDstResource);
        break;

    case D3D11_RESOURCE_DIMENSION_TEXTURE2D: {
        auto tex = as_wrapper<MyID3D11Texture2D, ID3D11Resource>(pDstResource);
        resource_inner = tex ? tex->get_inner() : pDstResource;
        if (!LOG_STARTED) break;
        if (tex) {
            auto& cached_size = impl->cached_size;
            if (
                tex->get_orig_width() == cached_size.sc_width &&
                tex->get_orig_height() == cached_size.sc_height
                )
                tex_type = 1;
        }
        break;
    }

    default:
        resource_inner = NULL;
        break;
    }
    LOG_MFUN(_,
        LOG_ARG_TYPE(pDstResource, MyID3D11Resource_Logger),
        LogIf<1>{tex_type},
        LOG_ARG(tex_type),
        LOG_ARG(DstSubresource),
        LOG_ARG(pDstBox),
        LOG_ARG(SrcRowPitch),
        LOG_ARG(SrcDepthPitch)
    );
    impl->context->UpdateSubresource(
        resource_inner,
        DstSubresource,
        pDstBox,
        pSrcData,
        SrcRowPitch,
        SrcDepthPitch
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::ClearRenderTargetView(
    ID3D11RenderTargetView* pRenderTargetView,
    const FLOAT ColorRGBA[4]
) {
    auto* render_target_view = as_wrapper<MyID3D11RenderTargetView>(pRenderTargetView);
    ID3D11RenderTargetView* real_rtv = render_target_view ? render_target_view->get_inner() : pRenderTargetView;

    LOG_MFUN(_,
        LOG_ARG(render_target_view),
        LOG_ARG_TYPE(ColorRGBA, ArrayLoggerRef, 4)
    );

    // Replace opaque clears to the 960x640 compositing canvas with a
    // fully transparent clear so the game frame shows through underneath,
    // while still clearing per-frame so UI elements don't ghost.
    {
        auto* rtv_tex = render_target_view ?
            as_wrapper<MyID3D11Texture2D, ID3D11Resource>(render_target_view->get_resource()) :
            NULL;

        bool slang_active =
            impl->device->get_config()->shader_toggle &&
            impl->device->get_d3d11_slot(impl->device->get_config()->slang_active.load()) &&
            impl->device->get_d3d11_slot(impl->device->get_config()->slang_active.load())->shader_preset;

        if (slang_active &&
            rtv_tex &&
            rtv_tex->get_orig_width() == 960 &&
            rtv_tex->get_orig_height() == 640 &&
            ColorRGBA[3] == 1.0f) {
            DIAG_LOG("[CLEAR_960_REPLACED] rtv=%p rgba=(%f,%f,%f,%f) -> (0,0,0,0)\n",
                (void*)render_target_view,
                ColorRGBA[0], ColorRGBA[1], ColorRGBA[2], ColorRGBA[3]
            );
            static const FLOAT transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            impl->context->ClearRenderTargetView(real_rtv, transparent);
            return;
        }

        if (rtv_tex &&
            rtv_tex->get_orig_width() == 960 &&
            rtv_tex->get_orig_height() == 640) {
            DIAG_LOG("[CLEAR_960_PASS] rtv=%p rgba=(%f,%f,%f,%f) alpha!=1 passing through\n",
                (void*)render_target_view,
                ColorRGBA[0], ColorRGBA[1], ColorRGBA[2], ColorRGBA[3]
            );
        }
    }

    impl->context->ClearRenderTargetView(real_rtv, ColorRGBA);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::ClearDepthStencilView(
    ID3D11DepthStencilView* pDepthStencilView,
    UINT ClearFlags,
    FLOAT Depth,
    UINT8 Stencil
) {
    auto* depth_stencil_view = as_wrapper<MyID3D11DepthStencilView>(pDepthStencilView);
    ID3D11DepthStencilView* real_dsv = depth_stencil_view ? depth_stencil_view->get_inner() : pDepthStencilView;

    LOG_MFUN(_,
        LOG_ARG(depth_stencil_view),
        LOG_ARG_TYPE(ClearFlags, D3D11_CLEAR_Logger),
        LogIf<1>{ClearFlags& D3D11_CLEAR_DEPTH},
        LOG_ARG(Depth),
        LogIf<1>{ClearFlags& D3D11_CLEAR_STENCIL},
        LOG_ARG(Stencil)
    );

    impl->context->ClearDepthStencilView(real_dsv, ClearFlags, Depth, Stencil);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::GenerateMips(
    ID3D11ShaderResourceView* pShaderResourceView
) {
    LOG_MFUN();
    impl->context->GenerateMips(
        ((MyID3D11ShaderResourceView*)pShaderResourceView)->
        get_inner()
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::ResolveSubresource(
    ID3D11Resource* pDstResource,
    UINT DstSubresource,
    ID3D11Resource* pSrcResource,
    UINT SrcSubresource,
    DXGI_FORMAT Format
) {
    LOG_MFUN(_,
        LOG_ARG_TYPE(pDstResource, MyID3D11Resource_Logger),
        LOG_ARG(DstSubresource),
        LOG_ARG_TYPE(pSrcResource, MyID3D11Resource_Logger),
        LOG_ARG(SrcSubresource),
        LOG_ARG(Format)
    );
    D3D11_RESOURCE_DIMENSION dstType;
    pDstResource->GetType(&dstType);
    D3D11_RESOURCE_DIMENSION srcType;
    pSrcResource->GetType(&srcType);
    switch (dstType) {
    case D3D11_RESOURCE_DIMENSION_BUFFER:
        pDstResource =
            unwrap<MyID3D11Buffer>(pDstResource);
        break;

    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        pDstResource =
            unwrap<MyID3D11Texture1D>(pDstResource);
        break;

    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
        pDstResource =
            unwrap<MyID3D11Texture2D>(pDstResource);
        break;

    default:
        break;
    }
    switch (srcType) {
    case D3D11_RESOURCE_DIMENSION_BUFFER:
        pSrcResource =
            unwrap<MyID3D11Buffer>(pSrcResource);
        break;

    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        pSrcResource =
            unwrap<MyID3D11Texture1D>(pSrcResource);
        break;

    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
        pSrcResource =
            unwrap<MyID3D11Texture2D>(pSrcResource);
        break;

    default:
        break;
    }
    impl->context->ResolveSubresource(
        pDstResource,
        DstSubresource,
        pSrcResource,
        SrcSubresource,
        Format
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::VSGetConstantBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer** ppConstantBuffers
) {
    LOG_MFUN();
    impl->context->VSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
    RemapArrayFromCache(ppConstantBuffers, NumBuffers, cached_bs_map, cached_bs_map_mutex);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::PSGetShaderResources(
    UINT StartSlot,
    UINT NumViews,
    ID3D11ShaderResourceView** ppShaderResourceViews
) {
    LOG_MFUN();
    impl->context->PSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
    RemapSrvArrayToWrappers(ppShaderResourceViews, NumViews);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::PSGetShader(
    ID3D11PixelShader** ppPixelShader,
    ID3D11ClassInstance** ppClassInstances,
    UINT* pNumClassInstances
) {
    LOG_MFUN();
    if (pNumClassInstances) *pNumClassInstances = 0;
    impl->context->PSGetShader(ppPixelShader, NULL, NULL);
    RemapSingleFromCache(ppPixelShader, cached_pss_map, cached_pss_map_mutex);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::PSGetSamplers(
    UINT StartSlot,
    UINT NumSamplers,
    ID3D11SamplerState** ppSamplers
) {
    LOG_MFUN();
    impl->context->PSGetSamplers(StartSlot, NumSamplers, ppSamplers);
    RemapArrayFromCache(ppSamplers, NumSamplers, cached_sss_map, cached_sss_map_mutex);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::VSGetShader(
    ID3D11VertexShader** ppVertexShader,
    ID3D11ClassInstance** ppClassInstances,
    UINT* pNumClassInstances
) {
    LOG_MFUN();
    if (pNumClassInstances) *pNumClassInstances = 0;
    impl->context->VSGetShader(ppVertexShader, NULL, NULL);
    RemapSingleFromCache(ppVertexShader, cached_vss_map, cached_vss_map_mutex);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::PSGetConstantBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer** ppConstantBuffers
) {
    LOG_MFUN();
    impl->context->PSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
    RemapArrayFromCache(ppConstantBuffers, NumBuffers, cached_bs_map, cached_bs_map_mutex);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::IAGetInputLayout(
    ID3D11InputLayout** ppInputLayout
) {
    LOG_MFUN();
    impl->context->IAGetInputLayout(ppInputLayout);

    if (!ppInputLayout || !*ppInputLayout) return;

    ID3D11InputLayout* inner = *ppInputLayout;
    MyID3D11InputLayout* wrapped = nullptr;

    {
        std::lock_guard<std::mutex> lock(cached_ils_map_mutex);
        auto it = cached_ils_map.find(inner);
        if (it != cached_ils_map.end() && it->second && is_wrapper(it->second)) {
            wrapped = it->second;
            wrapped->AddRef(); 
        }
    }

    if (wrapped) {
        inner->Release();
        *ppInputLayout = wrapped;
    }
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::IAGetVertexBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer** ppVertexBuffers,
    UINT* pStrides,
    UINT* pOffsets
) {
    LOG_MFUN();
    impl->context->IAGetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);

    if (!ppVertexBuffers || NumBuffers == 0) return;
    if (StartSlot >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) return;

    NumBuffers = std::min(
        NumBuffers,
        (UINT)D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - StartSlot
    );

    for (UINT i = 0; i < NumBuffers; ++i) {
        ID3D11Buffer* inner = ppVertexBuffers[i];
        if (!inner) continue;

        MyID3D11Buffer* wrapped = nullptr;
        {
            std::lock_guard<std::mutex> lock(cached_bs_map_mutex);
            auto it = cached_bs_map.find(inner);
            if (it != cached_bs_map.end() && it->second && is_wrapper(it->second)) {
                wrapped = it->second;
                wrapped->AddRef();
            }
        }

        if (wrapped) {
            inner->Release();
            ppVertexBuffers[i] = wrapped;
        }
    }
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::IAGetIndexBuffer(
    ID3D11Buffer** pIndexBuffer,
    DXGI_FORMAT* Format,
    UINT* Offset
) {
    LOG_MFUN();
    impl->context->IAGetIndexBuffer(pIndexBuffer, Format, Offset);

    if (!pIndexBuffer || !*pIndexBuffer) return;

    ID3D11Buffer* inner = *pIndexBuffer;
    MyID3D11Buffer* wrapped = nullptr;
    {
        std::lock_guard<std::mutex> lock(cached_bs_map_mutex);
        auto it = cached_bs_map.find(inner);
        if (it != cached_bs_map.end() && it->second && is_wrapper(it->second)) {
            wrapped = it->second;
            wrapped->AddRef(); 
        }
    }

    if (wrapped) {
        inner->Release();
        *pIndexBuffer = wrapped;
    }
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::GSGetConstantBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer** ppConstantBuffers
) {
    LOG_MFUN();
    impl->context->GSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
    RemapArrayFromCache(ppConstantBuffers, NumBuffers, cached_bs_map, cached_bs_map_mutex);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::GSGetShader(
    ID3D11GeometryShader** ppGeometryShader,
    ID3D11ClassInstance** ppClassInstances,
    UINT* pNumClassInstances
) {
    LOG_MFUN();
    impl->context->GSGetShader(ppGeometryShader, NULL, NULL);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::IAGetPrimitiveTopology(
    D3D11_PRIMITIVE_TOPOLOGY* pTopology
) {
    LOG_MFUN();
    impl->context->IAGetPrimitiveTopology(pTopology);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::VSGetShaderResources(
    UINT StartSlot,
    UINT NumViews,
    ID3D11ShaderResourceView** ppShaderResourceViews
) {
    LOG_MFUN();
    impl->context->VSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
    RemapSrvArrayToWrappers(ppShaderResourceViews, NumViews);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::VSGetSamplers(
    UINT StartSlot,
    UINT NumSamplers,
    ID3D11SamplerState** ppSamplers
) {
    LOG_MFUN();
    impl->context->VSGetSamplers(StartSlot, NumSamplers, ppSamplers);
    RemapArrayFromCache(ppSamplers, NumSamplers, cached_sss_map, cached_sss_map_mutex);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::GetPredication(
    ID3D11Predicate** ppPredicate,
    WINBOOL* pPredicateValue
) {
    LOG_MFUN();
    impl->context->GetPredication(ppPredicate, pPredicateValue);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::GSGetShaderResources(
    UINT StartSlot,
    UINT NumViews,
    ID3D11ShaderResourceView** ppShaderResourceViews
) {
    LOG_MFUN();
    impl->context->GSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
    RemapSrvArrayToWrappers(ppShaderResourceViews, NumViews);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::GSGetSamplers(
    UINT StartSlot,
    UINT NumSamplers,
    ID3D11SamplerState** ppSamplers
) {
    LOG_MFUN();
    impl->context->GSGetSamplers(StartSlot, NumSamplers, ppSamplers);
    RemapArrayFromCache(ppSamplers, NumSamplers, cached_sss_map, cached_sss_map_mutex);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::OMGetRenderTargets(
    UINT NumViews,
    ID3D11RenderTargetView** ppRenderTargetViews,
    ID3D11DepthStencilView** ppDepthStencilView
) {
    LOG_MFUN();
    impl->context->OMGetRenderTargets(NumViews, ppRenderTargetViews, ppDepthStencilView);
    RemapArrayFromCache(ppRenderTargetViews, NumViews, cached_rtvs_map, cached_rtvs_map_mutex);
    RemapSingleFromCache(ppDepthStencilView, cached_dsvs_map, cached_dsvs_map_mutex);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::OMGetBlendState(
    ID3D11BlendState** ppBlendState,
    FLOAT BlendFactor[4],
    UINT* pSampleMask
) {
    LOG_MFUN();
    impl->context->OMGetBlendState(ppBlendState, BlendFactor, pSampleMask);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::OMGetDepthStencilState(
    ID3D11DepthStencilState** ppDepthStencilState,
    UINT* pStencilRef
) {
    impl->context->OMGetDepthStencilState(ppDepthStencilState, pStencilRef);
    RemapSingleFromCache(ppDepthStencilState, cached_dsss_map, cached_dsss_map_mutex);

    LOG_MFUN(_,
        LOG_ARG(ppDepthStencilState ? *ppDepthStencilState : nullptr),
        LOG_ARG_TYPE(pStencilRef ? *pStencilRef : 0, NumHexLogger)
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::SOGetTargets(
    UINT NumBuffers,
    ID3D11Buffer** ppSOTargets
) {
    LOG_MFUN();
    impl->context->SOGetTargets(NumBuffers, ppSOTargets);
    RemapArrayFromCache(ppSOTargets, NumBuffers, cached_bs_map, cached_bs_map_mutex);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::RSGetState(
    ID3D11RasterizerState** ppRasterizerState
) {
    LOG_MFUN();
    impl->context->RSGetState(
        ppRasterizerState
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::RSGetViewports(
    UINT* NumViewports,
    D3D11_VIEWPORT* pViewports
) {
    LOG_MFUN();
    impl->context->RSGetViewports(
        NumViewports,
        pViewports
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::RSGetScissorRects(
    UINT* NumRects,
    D3D11_RECT* pRects
) {
    LOG_MFUN();
    impl->context->RSGetScissorRects(
        NumRects,
        pRects
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::ClearState(
) {
    LOG_MFUN();
    impl->context->ClearState(
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::Flush(
) {
    LOG_MFUN();
    impl->context->Flush(
    );
}

HRESULT STDMETHODCALLTYPE MyID3D11DeviceContext::GetPrivateData(
    REFGUID guid,
    UINT* pDataSize,
    void* pData
) {
    return impl->context->GetPrivateData(guid, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE MyID3D11DeviceContext::SetPrivateData(
    REFGUID guid,
    UINT DataSize,
    const void* pData
) {
    return impl->context->SetPrivateData(guid, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE MyID3D11DeviceContext::SetPrivateDataInterface(
    REFGUID guid,
    const IUnknown* pData
) {
    return impl->context->SetPrivateDataInterface(guid, pData);
}

HRESULT STDMETHODCALLTYPE MyID3D11DeviceContext::Map(
    ID3D11Resource* pResource, UINT Subresource,
    D3D11_MAP MapType, UINT MapFlags,
    D3D11_MAPPED_SUBRESOURCE* pMappedResource
) {
    D3D11_RESOURCE_DIMENSION type;
    pResource->GetType(&type);
    ID3D11Resource* inner;
    switch (type) {
    case D3D11_RESOURCE_DIMENSION_BUFFER:
        inner = unwrap<MyID3D11Buffer>(pResource); break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        inner = unwrap<MyID3D11Texture1D>(pResource); break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
        inner = unwrap<MyID3D11Texture2D>(pResource); break;
    default: inner = pResource; break;
    }
    return impl->context->Map(inner, Subresource, MapType, MapFlags, pMappedResource);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::Unmap(
    ID3D11Resource* pResource, UINT Subresource
) {
    D3D11_RESOURCE_DIMENSION type;
    pResource->GetType(&type);
    ID3D11Resource* inner;
    switch (type) {
    case D3D11_RESOURCE_DIMENSION_BUFFER:
        inner = unwrap<MyID3D11Buffer>(pResource); break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        inner = unwrap<MyID3D11Texture1D>(pResource); break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
        inner = unwrap<MyID3D11Texture2D>(pResource); break;
    default: inner = pResource; break;
    }
    impl->context->Unmap(inner, Subresource);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::Begin(ID3D11Asynchronous* pAsync) {
    impl->context->Begin(pAsync);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::End(ID3D11Asynchronous* pAsync) {
    impl->context->End(pAsync);
}

HRESULT STDMETHODCALLTYPE MyID3D11DeviceContext::GetData(
    ID3D11Asynchronous* pAsync, void* pData, UINT DataSize, UINT GetDataFlags
) {
    return impl->context->GetData(pAsync, pData, DataSize, GetDataFlags);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews(
    UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews,
    ID3D11DepthStencilView* pDepthStencilView,
    UINT UAVStartSlot, UINT NumUAVs,
    ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,
    const UINT* pUAVInitialCounts
) {
    impl->context->OMSetRenderTargetsAndUnorderedAccessViews(
        NumRTVs, ppRenderTargetViews, pDepthStencilView,
        UAVStartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DrawIndexedInstancedIndirect(
    ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs
) {
    impl->context->DrawIndexedInstancedIndirect(
        unwrap<MyID3D11Buffer>(pBufferForArgs),
        AlignedByteOffsetForArgs);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DrawInstancedIndirect(
    ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs
) {
    impl->context->DrawInstancedIndirect(
        unwrap<MyID3D11Buffer>(pBufferForArgs),
        AlignedByteOffsetForArgs);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::Dispatch(
    UINT X, UINT Y, UINT Z
) {
    impl->context->Dispatch(X, Y, Z);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DispatchIndirect(
    ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs
) {
    impl->context->DispatchIndirect(
        unwrap<MyID3D11Buffer>(pBufferForArgs),
        AlignedByteOffsetForArgs);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::CopyStructureCount(
    ID3D11Buffer* pDstBuffer, UINT DstAlignedByteOffset,
    ID3D11UnorderedAccessView* pSrcView
) {
    impl->context->CopyStructureCount(
        unwrap<MyID3D11Buffer>(pDstBuffer),
        DstAlignedByteOffset, pSrcView);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::ClearUnorderedAccessViewUint(
    ID3D11UnorderedAccessView* pUAV, const UINT Values[4]
) {
    impl->context->ClearUnorderedAccessViewUint(pUAV, Values);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::ClearUnorderedAccessViewFloat(
    ID3D11UnorderedAccessView* pUAV, const FLOAT Values[4]
) {
    impl->context->ClearUnorderedAccessViewFloat(pUAV, Values);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::SetResourceMinLOD(
    ID3D11Resource* pResource, FLOAT MinLOD
) {
    impl->context->SetResourceMinLOD(pResource, MinLOD);
}

FLOAT STDMETHODCALLTYPE MyID3D11DeviceContext::GetResourceMinLOD(
    ID3D11Resource* pResource
) {
    return impl->context->GetResourceMinLOD(pResource);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::ExecuteCommandList(
    ID3D11CommandList* pCommandList, WINBOOL RestoreContextState
) {
    impl->context->ExecuteCommandList(pCommandList, RestoreContextState);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::HSSetShaderResources(
    UINT StartSlot, UINT NumViews,
    ID3D11ShaderResourceView* const* ppSRVs
) {
    impl->context->HSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::HSSetShader(
    ID3D11HullShader* pHS,
    ID3D11ClassInstance* const* ppCI, UINT NumCI
) {
    impl->context->HSSetShader(pHS, ppCI, NumCI);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::HSSetSamplers(
    UINT StartSlot, UINT NumSamplers,
    ID3D11SamplerState* const* ppSamplers
) {
    impl->context->HSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::HSSetConstantBuffers(
    UINT StartSlot, UINT NumBuffers,
    ID3D11Buffer* const* ppCBs
) {
    impl->context->HSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DSSetShaderResources(
    UINT StartSlot, UINT NumViews,
    ID3D11ShaderResourceView* const* ppSRVs
) {
    impl->context->DSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DSSetShader(
    ID3D11DomainShader* pDS,
    ID3D11ClassInstance* const* ppCI, UINT NumCI
) {
    impl->context->DSSetShader(pDS, ppCI, NumCI);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DSSetSamplers(
    UINT StartSlot, UINT NumSamplers,
    ID3D11SamplerState* const* ppSamplers
) {
    impl->context->DSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DSSetConstantBuffers(
    UINT StartSlot, UINT NumBuffers,
    ID3D11Buffer* const* ppCBs
) {
    impl->context->DSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::CSSetShaderResources(
    UINT StartSlot, UINT NumViews,
    ID3D11ShaderResourceView* const* ppSRVs
) {
    ID3D11ShaderResourceView* srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};

    if (ppSRVs) {
        NumViews = std::min(NumViews, (UINT)D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
        for (UINT i = 0; i < NumViews; ++i) {
            srvs[i] = unwrap<MyID3D11ShaderResourceView>(ppSRVs[i]);
        }
    }

    impl->context->CSSetShaderResources(
        StartSlot,
        NumViews,
        ppSRVs ? srvs : nullptr
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::CSSetUnorderedAccessViews(
    UINT StartSlot, UINT NumUAVs,
    ID3D11UnorderedAccessView* const* ppUAVs,
    const UINT* pUAVInitialCounts
) {
    impl->context->CSSetUnorderedAccessViews(StartSlot, NumUAVs, ppUAVs, pUAVInitialCounts);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::CSSetShader(
    ID3D11ComputeShader* pCS,
    ID3D11ClassInstance* const* ppCI, UINT NumCI
) {
    impl->context->CSSetShader(pCS, ppCI, NumCI);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::CSSetSamplers(
    UINT StartSlot, UINT NumSamplers,
    ID3D11SamplerState* const* ppSamplers
) {
    // 1. Must unwrap before passing to the real context!
    ID3D11SamplerState* sss[16]; // Safe fixed size, D3D11 max is 14 for CS

    if (ppSamplers && NumSamplers > 0) {
        for (UINT i = 0; i < NumSamplers; ++i) {
            sss[i] = ppSamplers[i] ?
                unwrap<MyID3D11SamplerState>(ppSamplers[i]) :
                NULL;
        }
    }

    // 2. Pass the unwrapped array (sss)
    impl->context->CSSetSamplers(
        StartSlot,
        NumSamplers,
        (ppSamplers && NumSamplers) ? sss : ppSamplers
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::CSSetConstantBuffers(
    UINT StartSlot, UINT NumBuffers,
    ID3D11Buffer* const* ppCBs
) {
    ID3D11Buffer* cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};

    if (ppCBs) {
        NumBuffers = std::min(NumBuffers, (UINT)D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
        for (UINT i = 0; i < NumBuffers; ++i) {
            cbs[i] = unwrap<MyID3D11Buffer>(ppCBs[i]);
        }
    }

    impl->context->CSSetConstantBuffers(
        StartSlot,
        NumBuffers,
        ppCBs ? cbs : nullptr
    );
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::OMGetRenderTargetsAndUnorderedAccessViews(
    UINT NumRTVs, ID3D11RenderTargetView** ppRTVs,
    ID3D11DepthStencilView** ppDSV,
    UINT UAVStartSlot, UINT NumUAVs,
    ID3D11UnorderedAccessView** ppUAVs
) {
    impl->context->OMGetRenderTargetsAndUnorderedAccessViews(
        NumRTVs, ppRTVs, ppDSV, UAVStartSlot, NumUAVs, ppUAVs);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::HSGetShaderResources(
    UINT StartSlot, UINT NumViews,
    ID3D11ShaderResourceView** ppSRVs
) {
    LOG_MFUN();
    impl->context->HSGetShaderResources(StartSlot, NumViews, ppSRVs);
    RemapSrvArrayToWrappers(ppSRVs, NumViews);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::HSGetShader(
    ID3D11HullShader** ppHS,
    ID3D11ClassInstance** ppCI, UINT* pNumCI
) {
    impl->context->HSGetShader(ppHS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::HSGetSamplers(
    UINT StartSlot, UINT NumSamplers,
    ID3D11SamplerState** ppSamplers
) {
    impl->context->HSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::HSGetConstantBuffers(
    UINT StartSlot, UINT NumBuffers,
    ID3D11Buffer** ppCBs
) {
    impl->context->HSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DSGetShaderResources(
    UINT StartSlot, UINT NumViews,
    ID3D11ShaderResourceView** ppSRVs
) {
    LOG_MFUN();
    impl->context->DSGetShaderResources(StartSlot, NumViews, ppSRVs);
    RemapSrvArrayToWrappers(ppSRVs, NumViews);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DSGetShader(
    ID3D11DomainShader** ppDS,
    ID3D11ClassInstance** ppCI, UINT* pNumCI
) {
    impl->context->DSGetShader(ppDS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DSGetSamplers(
    UINT StartSlot, UINT NumSamplers,
    ID3D11SamplerState** ppSamplers
) {
    impl->context->DSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::DSGetConstantBuffers(
    UINT StartSlot, UINT NumBuffers,
    ID3D11Buffer** ppCBs
) {
    impl->context->DSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::CSGetShaderResources(
    UINT StartSlot, UINT NumViews,
    ID3D11ShaderResourceView** ppSRVs
) {
    LOG_MFUN();
    impl->context->CSGetShaderResources(StartSlot, NumViews, ppSRVs);
    RemapSrvArrayToWrappers(ppSRVs, NumViews);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::CSGetUnorderedAccessViews(
    UINT StartSlot, UINT NumUAVs,
    ID3D11UnorderedAccessView** ppUAVs
) {
    impl->context->CSGetUnorderedAccessViews(StartSlot, NumUAVs, ppUAVs);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::CSGetShader(
    ID3D11ComputeShader** ppCS,
    ID3D11ClassInstance** ppCI, UINT* pNumCI
) {
    impl->context->CSGetShader(ppCS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::CSGetSamplers(
    UINT StartSlot, UINT NumSamplers,
    ID3D11SamplerState** ppSamplers
) {
    impl->context->CSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE MyID3D11DeviceContext::CSGetConstantBuffers(
    UINT StartSlot, UINT NumBuffers,
    ID3D11Buffer** ppCBs
) {
    impl->context->CSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE MyID3D11DeviceContext::GetType() {
    return impl->context->GetType();
}

UINT STDMETHODCALLTYPE MyID3D11DeviceContext::GetContextFlags() {
    return impl->context->GetContextFlags();
}

HRESULT STDMETHODCALLTYPE MyID3D11DeviceContext::FinishCommandList(
    WINBOOL RestoreDeferredContextState,
    ID3D11CommandList** ppCommandList
) {
    return impl->context->FinishCommandList(RestoreDeferredContextState, ppCommandList);
}