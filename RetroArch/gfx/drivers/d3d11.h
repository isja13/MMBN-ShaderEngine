#ifndef RETROARCH_D3D11_H
#define RETROARCH_D3D11_H

#include "../common/d3d11_common.h"

#ifdef __cplusplus
extern "C" {
#endif

	d3d11_video_t* my_d3d11_gfx_init(ID3D11Device* device, DXGI_FORMAT format);
	void my_d3d11_gfx_free(d3d11_video_t* d3d11);
	bool my_d3d11_gfx_set_shader(d3d11_video_t* d3d11, const char* path);
	bool my_d3d11_gfx_frame(d3d11_video_t* d3d11, d3d11_texture_t* texture, UINT64 frame_count);
	void my_d3d11_update_viewport(d3d11_video_t* d3d11, D3D11RenderTargetView renderTargetView, video_viewport_t* viewport);

#ifdef __cplusplus
}
#endif

#endif