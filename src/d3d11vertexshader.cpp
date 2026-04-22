#include "d3d11vertexshader.h"
#include "d3d11devicechild_impl.h"
#include "log.h"

#define LOGGER default_logger
#define LOG_MFUN(_, ...) LOG_MFUN_DEF(MyID3D11VertexShader, ## __VA_ARGS__)

class MyID3D11VertexShader::Impl {
    friend class MyID3D11VertexShader;

    IUNKNOWN_PRIV(ID3D11VertexShader)
    DWORD bytecode_hash = 0;
    SIZE_T bytecode_length = 0;
    std::string source;
    std::vector<D3D11_SO_DECLARATION_ENTRY> decl_entries;
    ID3D11GeometryShader *sogs = NULL;

    Impl(
        ID3D11VertexShader **inner,
        DWORD bytecode_hash,
        SIZE_T bytecode_length,
        const std::string &source,
        std::vector<D3D11_SO_DECLARATION_ENTRY> &&decl_entries,
        ID3D11GeometryShader *pGeometryShader
    ) :
        IUNKNOWN_INIT(*inner),
        bytecode_hash(bytecode_hash),
        bytecode_length(bytecode_length),
        source(source),
        decl_entries(std::move(decl_entries)),
        sogs(pGeometryShader)
    {}

    ~Impl() {
        if (sogs) sogs->Release();
    }
};

ID3D11DEVICECHILD_IMPL(MyID3D11VertexShader, ID3D11VertexShader)

DWORD MyID3D11VertexShader::get_bytecode_hash() const { return impl->bytecode_hash; }
SIZE_T MyID3D11VertexShader::get_bytecode_length() const { return impl->bytecode_length; }
const std::string &MyID3D11VertexShader::get_source() const { return impl->source; }
const std::vector<D3D11_SO_DECLARATION_ENTRY> &
MyID3D11VertexShader::get_decl_entries() const { return impl->decl_entries; }
ID3D11GeometryShader *MyID3D11VertexShader::get_sogs() const { return impl->sogs; }

MyID3D11VertexShader::MyID3D11VertexShader(
    ID3D11VertexShader** inner,
    DWORD bytecode_hash,
    SIZE_T bytecode_length,
    const std::string& source,
    std::vector<D3D11_SO_DECLARATION_ENTRY>&& decl_entries,
    ID3D11GeometryShader* pGeometryShader
) :
    impl(new Impl(
        inner,
        bytecode_hash,
        bytecode_length,
        source,
        std::move(decl_entries),
        pGeometryShader
    ))
{
    ID3D11VertexShader* real_vs = *inner;

    LOG_MFUN(_,
        LOG_ARG(real_vs),
        LOG_ARG_TYPE(bytecode_hash, NumHexLogger),
        LOG_ARG(bytecode_length)
    );

    {
        std::lock_guard<std::mutex> lock(cached_vss_map_mutex);
        cached_vss_map.emplace(real_vs, this);
    }

    *inner = this;
    register_wrapper(this);
}

MyID3D11VertexShader::~MyID3D11VertexShader() {
    unregister_wrapper(this);
    LOG_MFUN();

    {
        std::lock_guard<std::mutex> lock(cached_vss_map_mutex);
        cached_vss_map.erase(impl->inner);
    }

    delete impl;
}

std::unordered_map<ID3D11VertexShader*, MyID3D11VertexShader*> cached_vss_map;
std::mutex cached_vss_map_mutex;
