#include "d3d11_base.c"
#include "d3d11.h"

#define SLANG_PASS_DIAG 0

#if SLANG_PASS_DIAG
#define SDIAG_LOG(fmt, ...) do { \
    FILE* f = fopen("slang_pass_diag.log", "a"); \
    if (f) { fprintf(f, fmt, ##__VA_ARGS__); fclose(f); } \
} while(0)
#else
#define SDIAG_LOG(fmt, ...) do {} while(0)
#endif

d3d11_video_t* my_d3d11_gfx_init(ID3D11Device* device, DXGI_FORMAT format) {
    d3d11_video_t* d3d11 = (d3d11_video_t*)calloc(1, sizeof(d3d11_video_t));
    if (!d3d11) return NULL;

    d3d11->device = device;

    // Need context for DX11 — get it from device
    D3D11GetImmediateContext(device, &d3d11->context);

    d3d11->frame.texture[0].desc.Format = format;
    d3d11->frame.texture[0].desc.Usage = D3D11_USAGE_DEFAULT;
    d3d11->frame.texture[0].desc.Width = 4;
    d3d11->frame.texture[0].desc.Height = 4;
    d3d11_init_texture(d3d11->device, &d3d11->frame.texture[0]);

    matrix_4x4_ortho(d3d11->ubo_values.mvp, 0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);

    {
        D3D11_SUBRESOURCE_DATA ubo_data;
        D3D11_BUFFER_DESC desc;

        desc.ByteWidth = sizeof(d3d11->ubo_values);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        desc.StructureByteStride = 0;

        ubo_data.pSysMem = &d3d11->ubo_values;
        ubo_data.SysMemPitch = 0;
        ubo_data.SysMemSlicePitch = 0;

        D3D11CreateBuffer(d3d11->device, &desc, &ubo_data, &d3d11->ubo);
        D3D11CreateBuffer(d3d11->device, &desc, NULL, &d3d11->frame.ubo);
    }

    d3d11_gfx_set_rotation(d3d11, 0);

    {
        D3D11_SAMPLER_DESC desc = { D3D11_FILTER_MIN_MAG_MIP_POINT };
        desc.MaxAnisotropy = 1;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MinLOD = -D3D11_FLOAT32_MAX;
        desc.MaxLOD = D3D11_FLOAT32_MAX;

        for (unsigned i = 0; i < RARCH_WRAP_MAX; ++i) {
            switch (i) {
            case RARCH_WRAP_BORDER:
                desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
                break;
            case RARCH_WRAP_EDGE:
                desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
                break;
            case RARCH_WRAP_REPEAT:
                desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
                break;
            case RARCH_WRAP_MIRRORED_REPEAT:
                desc.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR;
                break;
            }
            desc.AddressV = desc.AddressU;
            desc.AddressW = desc.AddressU;

            desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            D3D11CreateSamplerState(d3d11->device, &desc, &d3d11->samplers[RARCH_FILTER_LINEAR][i]);

            desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            D3D11CreateSamplerState(d3d11->device, &desc, &d3d11->samplers[RARCH_FILTER_NEAREST][i]);
        }
    }

    d3d11_set_filtering(d3d11, 0, true);

    {
        d3d11_vertex_t vertices[] = {
            { { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
            { { 0.0f, 1.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
            { { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
            { { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        };
        D3D11_SUBRESOURCE_DATA vertexData = { vertices };

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(vertices);
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11CreateBuffer(d3d11->device, &desc, &vertexData, &d3d11->frame.vbo);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(d3d11_vertex_t, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(d3d11_vertex_t, texcoord), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(d3d11_vertex_t, color), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        static const char shader[] =
#include "d3d_shaders/opaque_sm5.hlsl.h"
            ;

        if (!d3d11_init_shader(d3d11->device, shader, sizeof(shader), NULL, "VSMain", "PSMain", NULL, desc, countof(desc), &d3d11->shaders[VIDEO_SHADER_STOCK_BLEND])) {
            goto error;
        }
    }

    {
        D3D11_BLEND_DESC blend_desc = { 0 };

        blend_desc.AlphaToCoverageEnable = FALSE;
        blend_desc.IndependentBlendEnable = FALSE;
        blend_desc.RenderTarget[0].BlendEnable = TRUE;
        blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
        blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11CreateBlendState(d3d11->device, &blend_desc, &d3d11->blend_enable);

        blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        D3D11CreateBlendState(d3d11->device, &blend_desc, &d3d11->blend_pipeline);

        blend_desc.RenderTarget[0].BlendEnable = FALSE;
        D3D11CreateBlendState(d3d11->device, &blend_desc, &d3d11->blend_disable);
    }

    {
        D3D11_RASTERIZER_DESC desc = { (D3D11_FILL_MODE)0 };

        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;

        D3D11CreateRasterizerState(d3d11->device, &desc, &d3d11->state);
    }

    return d3d11;

error:
    d3d11_gfx_free(d3d11);
    return NULL;
}

void my_d3d11_gfx_free(d3d11_video_t* d3d11) {
    if (!d3d11) return;

    d3d11_free_shader_preset(d3d11);

    d3d11_release_texture(&d3d11->frame.texture[0]);
    Release(d3d11->frame.ubo);
    Release(d3d11->frame.vbo);

    for (unsigned i = 0; i < GFX_MAX_SHADERS; i++) {
        d3d11_release_shader(&d3d11->shaders[i]);
    }

    Release(d3d11->blend_pipeline);
    Release(d3d11->ubo);
    Release(d3d11->blend_enable);
    Release(d3d11->blend_disable);

    for (unsigned i = 0; i < RARCH_WRAP_MAX; i++) {
        Release(d3d11->samplers[RARCH_FILTER_LINEAR][i]);
        Release(d3d11->samplers[RARCH_FILTER_NEAREST][i]);
    }

    Release(d3d11->state);
    Release(d3d11->context);

    free(d3d11);
}

bool my_d3d11_gfx_set_shader(d3d11_video_t* d3d11, const char* path) {
    return d3d11_gfx_set_shader(d3d11, RARCH_SHADER_SLANG, path);
}

bool my_d3d11_gfx_frame(d3d11_video_t* d3d11, d3d11_texture_t* texture, UINT64 frame_count) {
    D3D11DeviceContext context = d3d11->context;
    unsigned width = texture->desc.Width;
    unsigned height = texture->desc.Height;

    D3D11SetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    if (d3d11->shader_preset) {
        if (d3d11->frame.texture[0].desc.Width != width || d3d11->frame.texture[0].desc.Height != height) {
            d3d11->resize_render_targets = true;
        }

        if (d3d11->resize_render_targets) {
            for (unsigned i = 0; i < d3d11->shader_preset->passes; ++i) {
                d3d11_release_texture(&d3d11->pass[i].rt);
                d3d11_release_texture(&d3d11->pass[i].feedback);
                memset(&d3d11->pass[i].rt, 0, sizeof(d3d11->pass[i].rt));
                memset(&d3d11->pass[i].feedback, 0, sizeof(d3d11->pass[i].feedback));
            }
        }

        if (d3d11->shader_preset->history_size) {
            if (d3d11->init_history) {
                d3d11_init_history(d3d11, width, height);
            }
            else {
                d3d11_texture_t tmp = d3d11->frame.texture[d3d11->shader_preset->history_size];
                for (int k = d3d11->shader_preset->history_size; k > 0; --k) {
                    d3d11->frame.texture[k] = d3d11->frame.texture[k - 1];
                }
                d3d11->frame.texture[0] = tmp;
            }
        }
    }

    if (d3d11->frame.texture[0].desc.Width != width || d3d11->frame.texture[0].desc.Height != height) {
        d3d11->frame.texture[0].desc.Width = width;
        d3d11->frame.texture[0].desc.Height = height;
        d3d11_init_texture(d3d11->device, &d3d11->frame.texture[0]);
    }

    if (d3d11->shader_preset) {
        if (d3d11->resize_render_targets) {
            d3d11_init_render_targets(d3d11, width, height);
            {
                SDIAG_LOG("[INIT_RT] width=%u height=%u output_size=(%f,%f) frame.output_size=(%f,%f,%f,%f)\n",
                    width, height,
                    d3d11->ubo_values.OutputSize.width,
                    d3d11->ubo_values.OutputSize.height,
                    d3d11->frame.output_size.x,
                    d3d11->frame.output_size.y,
                    d3d11->frame.output_size.z,
                    d3d11->frame.output_size.w
                );
                if (d3d11->shader_preset) {
                    for (unsigned _i = 0; _i < d3d11->shader_preset->passes; ++_i) {
                        SDIAG_LOG("  [INIT_RT PASS %u] rt.desc=(%u x %u) rt.handle=%p rt.rt_view=%p viewport=(%f,%f,%f,%f) scale_type_x=%d scale_type_y=%d scale_x=%f scale_y=%f\n",
                            _i,
                            d3d11->pass[_i].rt.desc.Width,
                            d3d11->pass[_i].rt.desc.Height,
                            (void*)d3d11->pass[_i].rt.handle,
                            (void*)d3d11->pass[_i].rt.rt_view,
                            d3d11->pass[_i].viewport.TopLeftX,
                            d3d11->pass[_i].viewport.TopLeftY,
                            d3d11->pass[_i].viewport.Width,
                            d3d11->pass[_i].viewport.Height,
                            d3d11->shader_preset->pass[_i].fbo.scale_x,
                            d3d11->shader_preset->pass[_i].fbo.scale_y
                        );
                    }
                }
            }
        }
    }

    D3D11SetVertexBuffer(context, 0, d3d11->frame.vbo, sizeof(d3d11_vertex_t), 0);
    D3D11SetBlendState(context, d3d11->blend_disable, NULL, D3D11_DEFAULT_SAMPLE_MASK);

    D3D11CopyResource(context, (D3D11Resource)d3d11->frame.texture[0].handle, (D3D11Resource)texture->handle);

    {
        SDIAG_LOG("[COPY] src=%p dst=%p src_w=%u src_h=%u\n",
            (void*)texture->handle,
            (void*)d3d11->frame.texture[0].handle,
            texture->desc.Width,
            texture->desc.Height
        );
    }

    texture = d3d11->frame.texture;

    if (d3d11->shader_preset) {
        for (unsigned i = 0; i < d3d11->shader_preset->passes; ++i) {
            if (d3d11->shader_preset->pass[i].feedback) {
                d3d11_texture_t tmp = d3d11->pass[i].feedback;
                d3d11->pass[i].feedback = d3d11->pass[i].rt;
                d3d11->pass[i].rt = tmp;
            }
        }

        for (unsigned i = 0; i < d3d11->shader_preset->passes; ++i) {
            d3d11_set_shader(context, &d3d11->pass[i].shader);

            if (d3d11->shader_preset->pass[i].frame_count_mod) {
                d3d11->pass[i].frame_count = frame_count % d3d11->shader_preset->pass[i].frame_count_mod;
            }
            else {
                d3d11->pass[i].frame_count = frame_count;
            }
            d3d11->pass[i].frame_direction = 1;

            for (unsigned j = 0; j < SLANG_CBUFFER_MAX; ++j) {
                D3D11Buffer    buffer = d3d11->pass[i].buffers[j];
                cbuffer_sem_t* buffer_sem = &d3d11->pass[i].semantics.cbuffers[j];

                if (buffer_sem->stage_mask && buffer_sem->uniforms) {
                    D3D11_MAPPED_SUBRESOURCE mapped;
                    uniform_sem_t* uniform = buffer_sem->uniforms;

                    D3D11MapBuffer(context, buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                    while (uniform->size) {
                        if (uniform->data) {
                            memcpy((char*)mapped.pData + uniform->offset, uniform->data, uniform->size);
                        }
                        uniform++;
                    }
                    D3D11UnmapBuffer(context, buffer, 0);

                    if (buffer_sem->stage_mask & SLANG_STAGE_VERTEX_MASK) {
                        D3D11SetVShaderConstantBuffers(context, buffer_sem->binding, 1, &buffer);
                    }

                    if (buffer_sem->stage_mask & SLANG_STAGE_FRAGMENT_MASK) {
                        D3D11SetPShaderConstantBuffers(context, buffer_sem->binding, 1, &buffer);
                    }
                }
            }

            {
                D3D11RenderTargetView null_rt = NULL;
                D3D11SetRenderTargets(context, 1, &null_rt, NULL);
            }

            {
                D3D11ShaderResourceView textures[SLANG_NUM_BINDINGS] = { NULL };
                D3D11SamplerState       samplers[SLANG_NUM_BINDINGS] = { NULL };

                texture_sem_t* texture_sem = d3d11->pass[i].semantics.textures;
                while (texture_sem->stage_mask) {
                    int binding = texture_sem->binding;
                    textures[binding] = *(D3D11ShaderResourceView*)texture_sem->texture_data;
                    samplers[binding] = d3d11->samplers[texture_sem->filter][texture_sem->wrap];
                    texture_sem++;
                }

                D3D11SetPShaderResources(context, 0, SLANG_NUM_BINDINGS, textures);
                D3D11SetPShaderSamplers(context, 0, SLANG_NUM_BINDINGS, samplers);
            }
            {
                SDIAG_LOG("[PASS %u] rt.handle=%p rt.rt_view=%p rt.view=%p rt.desc=(%u x %u) pass.viewport=(%f,%f,%f,%f) frame.viewport=(%f,%f,%f,%f) ubo.OutputSize=(%f,%f) frame.output_size=(%f,%f)\n",
                    i,
                    (void*)d3d11->pass[i].rt.handle,
                    (void*)d3d11->pass[i].rt.rt_view,
                    (void*)d3d11->pass[i].rt.view,
                    d3d11->pass[i].rt.desc.Width,
                    d3d11->pass[i].rt.desc.Height,
                    d3d11->pass[i].viewport.TopLeftX,
                    d3d11->pass[i].viewport.TopLeftY,
                    d3d11->pass[i].viewport.Width,
                    d3d11->pass[i].viewport.Height,
                    d3d11->frame.viewport.TopLeftX,
                    d3d11->frame.viewport.TopLeftY,
                    d3d11->frame.viewport.Width,
                    d3d11->frame.viewport.Height,
                    d3d11->ubo_values.OutputSize.width,
                    d3d11->ubo_values.OutputSize.height,
                    d3d11->frame.output_size.x,
                    d3d11->frame.output_size.y
                );
                texture_sem_t* sem = d3d11->pass[i].semantics.textures;
                while (sem->stage_mask) {
                    SDIAG_LOG("  [PASS %u] texsem binding=%d view=%p sampler filter=%d wrap=%d\n",
                        i, sem->binding,
                        (void*)*(D3D11ShaderResourceView*)sem->texture_data,
                        sem->filter, sem->wrap
                    );
                    sem++;
                }
                for (unsigned j = 0; j < SLANG_CBUFFER_MAX; ++j) {
                    cbuffer_sem_t* buffer_sem = &d3d11->pass[i].semantics.cbuffers[j];
                    if (buffer_sem->stage_mask && buffer_sem->uniforms) {
                        uniform_sem_t* uniform = buffer_sem->uniforms;
                        while (uniform->size) {
                            if (uniform->data && uniform->size <= 16) {
                                float* fdata = (float*)uniform->data;
                                SDIAG_LOG("  [PASS %u] cbuf[%u] uniform offset=%u size=%u data=(%f,%f,%f,%f)\n",
                                    i, j,
                                    uniform->offset,
                                    uniform->size,
                                    uniform->size >= 4 ? fdata[0] : 0.0f,
                                    uniform->size >= 8 ? fdata[1] : 0.0f,
                                    uniform->size >= 12 ? fdata[2] : 0.0f,
                                    uniform->size >= 16 ? fdata[3] : 0.0f
                                );
                            }
                            uniform++;
                        }
                    }
                }
            }
            if (d3d11->pass[i].rt.handle) {
                D3D11SetRenderTargets(context, 1, &d3d11->pass[i].rt.rt_view, NULL);
                D3D11SetViewports(context, 1, &d3d11->pass[i].viewport);
                {
                    SDIAG_LOG("[DRAW PASS %u] SET rtv=%p viewport=(%f,%f,%f,%f) rt.desc=(%u x %u)\n",
                        i,
                        (void*)d3d11->pass[i].rt.rt_view,
                        d3d11->pass[i].viewport.TopLeftX,
                        d3d11->pass[i].viewport.TopLeftY,
                        d3d11->pass[i].viewport.Width,
                        d3d11->pass[i].viewport.Height,
                        d3d11->pass[i].rt.desc.Width,
                        d3d11->pass[i].rt.desc.Height
                    );
                }
                D3D11Draw(context, 4, 0);
                texture = &d3d11->pass[i].rt;
            }
            else {
                {
                    SDIAG_LOG("[DRAW PASS %u DIRECT] SET rtv=%p viewport=(%f,%f,%f,%f) renderTargetView=%p\n",
                        i,
                        (void*)d3d11->renderTargetView,
                        d3d11->frame.viewport.TopLeftX,
                        d3d11->frame.viewport.TopLeftY,
                        d3d11->frame.viewport.Width,
                        d3d11->frame.viewport.Height,
                        (void*)d3d11->renderTargetView
                    );
                }
                D3D11SetRenderTargets(context, 1, &d3d11->renderTargetView, NULL);
                D3D11SetViewports(context, 1, &d3d11->frame.viewport);
                D3D11Draw(context, 4, 0);
                texture = NULL;
                break;
            }
        }
    }

    D3D11SetRenderTargets(context, 1, &d3d11->renderTargetView, NULL);

    if (texture) {
        d3d11_set_shader(context, &d3d11->shaders[VIDEO_SHADER_STOCK_BLEND]);
        D3D11SetPShaderResources(context, 0, 1, &texture->view);
        D3D11SetPShaderSamplers(context, 0, 1, &d3d11->samplers[RARCH_FILTER_UNSPEC][RARCH_WRAP_DEFAULT]);
        D3D11SetVShaderConstantBuffers(context, 0, 1, &d3d11->frame.ubo);
        D3D11SetViewports(context, 1, &d3d11->frame.viewport);
        {
            SDIAG_LOG("[STOCK_BLEND_FALLBACK] rtv=%p viewport=(%f,%f,%f,%f) texture.view=%p texture.desc=(%u x %u)\n",
                (void*)d3d11->renderTargetView,
                d3d11->frame.viewport.TopLeftX,
                d3d11->frame.viewport.TopLeftY,
                d3d11->frame.viewport.Width,
                d3d11->frame.viewport.Height,
                (void*)texture->view,
                texture->desc.Width,
                texture->desc.Height
            );
        }
        D3D11Draw(context, 4, 0);
    }

    D3D11SetViewports(context, 1, &d3d11->frame.viewport);

    return true;
}

void my_d3d11_update_viewport(d3d11_video_t* d3d11, D3D11RenderTargetView renderTargetView, video_viewport_t* viewport) {

    {
        SDIAG_LOG("[UPDATE_VP] rtv=%p x=%d y=%d w=%u h=%u full_w=%u full_h=%u prev_output=(%f,%f) will_resize=%d\n",
            (void*)renderTargetView,
            viewport->x, viewport->y,
            viewport->width, viewport->height,
            viewport->full_width, viewport->full_height,
            d3d11->frame.output_size.x,
            d3d11->frame.output_size.y,
            (int)(d3d11->shader_preset && (
                d3d11->frame.output_size.x != (float)viewport->width ||
                d3d11->frame.output_size.y != (float)viewport->height
                ))
        );
    }

    d3d11->renderTargetView = renderTargetView;
    d3d11->vp = *viewport;

    d3d11->frame.viewport.TopLeftX = d3d11->vp.x;
    d3d11->frame.viewport.TopLeftY = d3d11->vp.y;
    d3d11->frame.viewport.Width = d3d11->vp.width;
    d3d11->frame.viewport.Height = d3d11->vp.height;
    d3d11->frame.viewport.MinDepth = 0.0f;
    d3d11->frame.viewport.MaxDepth = 1.0f;

    d3d11->viewport = d3d11->frame.viewport;

    d3d11->ubo_values.OutputSize.width = d3d11->viewport.Width;
    d3d11->ubo_values.OutputSize.height = d3d11->viewport.Height;

    if (d3d11->shader_preset && (d3d11->frame.output_size.x != d3d11->vp.width || d3d11->frame.output_size.y != d3d11->vp.height)) {
        d3d11->resize_render_targets = true;
    }

    d3d11->frame.output_size.x = d3d11->vp.width;
    d3d11->frame.output_size.y = d3d11->vp.height;
    d3d11->frame.output_size.z = 1.0f / d3d11->vp.width;
    d3d11->frame.output_size.w = 1.0f / d3d11->vp.height;
}