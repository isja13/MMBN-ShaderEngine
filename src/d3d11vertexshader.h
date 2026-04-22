#ifndef D3D11VERTEXSHADER_H
#define D3D11VERTEXSHADER_H

#include "main.h"
#include "d3d11devicechild.h"
#include <mutex>

class MyID3D11VertexShader : public ID3D11VertexShader {
    class Impl;
    Impl *impl;

public:
    ID3D11DEVICECHILD_DECL(ID3D11VertexShader)

    DWORD get_bytecode_hash() const;
    SIZE_T get_bytecode_length() const;
    const std::string &get_source() const;
    const std::vector<D3D11_SO_DECLARATION_ENTRY> &get_decl_entries() const;
    ID3D11GeometryShader *get_sogs() const;

    MyID3D11VertexShader(
        ID3D11VertexShader **inner,
        DWORD bytecode_hash,
        SIZE_T bytecode_length,
        const std::string &source,
        std::vector<D3D11_SO_DECLARATION_ENTRY> &&decl_entries,
        ID3D11GeometryShader *pGeometryShader
    );

     ~MyID3D11VertexShader();
};

extern std::unordered_map<ID3D11VertexShader *, MyID3D11VertexShader *> cached_vss_map;
extern std::mutex cached_vss_map_mutex;

#endif
