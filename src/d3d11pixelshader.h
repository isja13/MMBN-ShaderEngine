#ifndef D3D11PIXELSHADER_H
#define D3D11PIXELSHADER_H

#include "main.h"
#include "d3d11devicechild.h"
#include <mutex>

enum class PIXEL_SHADER_ALPHA_DISCARD {
    UNKNOWN,
    NONE,
    EQUAL,
    LESS,
    LESS_OR_EQUAL
};

class MyID3D11PixelShader : public ID3D11PixelShader {
    class Impl;
    Impl *impl;

    uint64_t magic = 0x5053414C49564531ull; // alive

public:
    ID3D11DEVICECHILD_DECL(ID3D11PixelShader)

    DWORD get_bytecode_hash() const;
    SIZE_T get_bytecode_length() const;
    const std::string &get_source() const;
    PIXEL_SHADER_ALPHA_DISCARD get_alpha_discard() const;
    const std::vector<UINT> &get_texcoord_sampler_map() const;
    const std::vector<std::tuple<std::string, std::string>> &get_uniform_list() const;

    MyID3D11PixelShader(
        ID3D11PixelShader **inner,
        DWORD bytecode_hash,
        SIZE_T bytecode_length,
        const std::string &source
    );

     ~MyID3D11PixelShader();
};

extern std::unordered_map<ID3D11PixelShader *, MyID3D11PixelShader *> cached_pss_map;
extern std::mutex cached_pss_map_mutex;

#endif
