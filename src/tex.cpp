#include "tex.h"

TextureAndViews::TextureAndViews() {}

TextureAndViews::~TextureAndViews() {
    if (tex) tex->Release();
    if (srv) srv->Release();
    if (rtv) rtv->Release();
}

TextureAndDepthViews::TextureAndDepthViews() {}

TextureAndDepthViews::~TextureAndDepthViews() {
    if (tex_ds) tex_ds->Release();
    if (dsv) dsv->Release();
}

TextureViewsAndBuffer::TextureViewsAndBuffer() {}

TextureViewsAndBuffer::~TextureViewsAndBuffer() {
    if (ps_cb) ps_cb->Release();
}
