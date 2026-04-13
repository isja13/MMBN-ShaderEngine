#include "d3d11buffer.h"
#include "d3d11resource_impl.h"
#include "log.h"

#define LOGGER default_logger
#define LOG_MFUN(_, ...) LOG_MFUN_DEF(MyID3D11Buffer, ## __VA_ARGS__)

class MyID3D11Buffer::Impl {
    friend class MyID3D11Buffer;

    IUNKNOWN_PRIV(ID3D11Buffer)
    ID3D11RESOURCE_PRIV

    D3D11_MAP map_type = (D3D11_MAP)0;
    void *mapped = NULL; // result of map
    char *cached = NULL; // local cache
    bool cached_state = false;
    D3D11_BUFFER_DESC desc = {};

    ID3D11DeviceContext* context = NULL;

    Impl(
        ID3D11Buffer** inner,
        const D3D11_BUFFER_DESC* pDesc,
        UINT64 id,
        const D3D11_SUBRESOURCE_DATA* pInitialData,
        ID3D11DeviceContext* context
    ) :
        IUNKNOWN_INIT(*inner),
        ID3D11RESOURCE_INIT(id),
        desc(*pDesc),
        context(context)
    {
        switch (desc.BindFlags) {
            case D3D11_BIND_CONSTANT_BUFFER:
            case D3D11_BIND_INDEX_BUFFER:
            case D3D11_BIND_VERTEX_BUFFER:
                cached = new char[desc.ByteWidth]{};
                if (pInitialData && pInitialData->pSysMem)
                    memcpy(
                        cached,
                        pInitialData->pSysMem,
                        desc.ByteWidth
                    );
                cached_state = true;
                break;

            default:
                break;
        }
    }

    ~Impl() {
        if (cached) delete[] cached;
    }

    HRESULT Map(
        D3D11_MAP MapType,
        UINT MapFlags,
        void **ppData
    ) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT ret = context->Map(inner, 0, MapType, MapFlags, &mappedResource);
        mapped = mappedResource.pData;
        if (ret == S_OK) {
            map_type = MapType;
            if (!LOG_STARTED || !cached) {
                *ppData = mapped;
                mapped = NULL;
            } else {
                switch (map_type) {
                    case D3D11_MAP_WRITE_DISCARD:
                        memset(cached, 0, desc.ByteWidth);
                        *ppData = cached;
                        break;

                    case D3D11_MAP_READ_WRITE:
                    case D3D11_MAP_READ:
                        memcpy(cached, mapped, desc.ByteWidth);
                        *ppData = cached;
                        break;

                    case D3D11_MAP_WRITE:
                    case D3D11_MAP_WRITE_NO_OVERWRITE:
                    default:
                        *ppData = mapped;
                        cached_state = false;
                        mapped = NULL;
                        break;
                }
            }
        }
        LOG_MFUN(_,
            LOG_ARG(MapType),
            LOG_ARG_TYPE(MapFlags, D3D11_MAP_FLAG),
            ret
        );
        return ret;
    }

    void Unmap(
    ) {
        LOG_MFUN();
        switch (map_type) {
            case D3D11_MAP_WRITE_DISCARD:
            case D3D11_MAP_READ_WRITE:
                if (cached && mapped) {
                    memcpy(mapped, cached, desc.ByteWidth);
                    cached_state = true;
                    break;
                }
                /* fall-through */

            case D3D11_MAP_READ:
            case D3D11_MAP_WRITE:
            case D3D11_MAP_WRITE_NO_OVERWRITE:
            default:
                mapped = NULL;
                break;
        }
        context->Unmap(inner, 0);
    }
};

std::unordered_map<ID3D11Buffer*, MyID3D11Buffer*> cached_bs_map;
std::mutex cached_bs_map_mutex;

MyID3D11Buffer::MyID3D11Buffer(
    ID3D11Buffer** inner,
    const D3D11_BUFFER_DESC* pDesc,
    UINT64 id,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11DeviceContext* context
) :
    impl(new Impl(inner, pDesc, id, pInitialData, context))
{    
    LOG_MFUN(_,
        LOG_ARG(*inner),
        LOG_ARG_TYPE(id, NumHexLogger)
    );
    {
        std::lock_guard<std::mutex> lock(cached_bs_map_mutex);
        cached_bs_map.emplace(*inner, this);
    }
    *inner = this;
    register_wrapper(this);
}

MyID3D11Buffer::~MyID3D11Buffer() {
    unregister_wrapper(this);

    FILE* f = fopen("C:\\buffer_destroy.txt", "a");
    if (f) {
        fprintf(f, "~MyID3D11Buffer: this=%p impl=%p impl->inner=%p\n",
            this, impl, impl ? impl->inner : NULL);
        if (impl && impl->inner) {
            fprintf(f, "  erasing from cached_bs_map: %p\n", impl->inner);
            fprintf(f, "  was in map BEFORE: %zu\n", cached_bs_map.count(impl->inner));
            if (impl && impl->inner) {
                std::lock_guard<std::mutex> lock(cached_bs_map_mutex);
                cached_bs_map.erase(impl->inner);
            }
            fprintf(f, "  was in map AFTER: %zu\n", cached_bs_map.count(impl->inner));
        }
        fflush(f);
        fclose(f);
    }

    LOG_MFUN();
    if (impl) {
        delete impl;
    }
}

ID3D11RESOURCE_IMPL(MyID3D11Buffer, ID3D11Buffer, D3D11_RESOURCE_DIMENSION_BUFFER)

D3D11_BUFFER_DESC &MyID3D11Buffer::get_desc() {
    return impl->desc;
}

const D3D11_BUFFER_DESC &MyID3D11Buffer::get_desc() const {
    return impl->desc;
}

char *&MyID3D11Buffer::get_cached() {
    return impl->cached;
}

char *MyID3D11Buffer::get_cached() const {
    return impl->cached;
}

bool &MyID3D11Buffer::get_cached_state() {
    return impl->cached_state;
}

bool MyID3D11Buffer::get_cached_state() const {
    return impl->cached_state;
}

HRESULT STDMETHODCALLTYPE MyID3D11Buffer::Map(
    D3D11_MAP MapType,
    UINT MapFlags,
    void **ppData
) {
    return impl->Map(MapType, MapFlags, ppData);
}

void STDMETHODCALLTYPE MyID3D11Buffer::Unmap(
) {
    impl->Unmap();
}

void STDMETHODCALLTYPE MyID3D11Buffer::GetDesc(
    D3D11_BUFFER_DESC *pDesc
) {
    LOG_MFUN();
    if (pDesc) *pDesc = impl->desc;
}

//std::unordered_map<ID3D11Buffer *, MyID3D11Buffer *> cached_bs_map;
