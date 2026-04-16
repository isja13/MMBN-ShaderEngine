#ifndef TEX_H
#define TEX_H

#include "main.h"

struct TextureAndViews {
    ID3D11Texture2D *tex = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    UINT width = 0;
    UINT height = 0;
    TextureAndViews();
    ~TextureAndViews();
};

struct TextureAndDepthViews : TextureAndViews {
    ID3D11Texture2D *tex_ds = NULL;
    ID3D11DepthStencilView *dsv = NULL;
    TextureAndDepthViews();
    ~TextureAndDepthViews();
};

struct TextureViewsAndBuffer : TextureAndViews {
    ID3D11Buffer *ps_cb = NULL;
    TextureViewsAndBuffer();
    ~TextureViewsAndBuffer();
};

#endif
