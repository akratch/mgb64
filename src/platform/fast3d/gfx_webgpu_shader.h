/*
 * gfx_webgpu_shader.h — WGSL combiner-shader emitter for the WebGPU backend.
 *
 * Emits a single WGSL module (a @vertex vs_main + a @fragment fs_main) from the
 * N64 color-combiner id (shader_id0/shader_id1), mirroring the GLSL emitter in
 * gfx_opengl.c (gfx_opengl_create_and_load_new_shader) and the MSL port in
 * gfx_metal.mm. The vertex-attribute layout is the same deterministic CCFeatures
 * walk both of those use, returned here so gfx_webgpu.c can build the matching
 * WGPUVertexBufferLayout. Compiled only under MGB64_WEBGPU_BACKEND.
 */
#ifndef GFX_WEBGPU_SHADER_H
#define GFX_WEBGPU_SHADER_H

#include <stdbool.h>
#include <stdint.h>

/* One interleaved vertex attribute in the buf_vbo stream: WGSL @location, its
 * component count (1..4 floats), and its float offset within the vertex. */
struct WgpuVtxAttr {
    int location;
    int size;     /* 1, 2, 3, or 4 floats */
    int offset;   /* in floats from the start of the vertex */
};

/* Everything gfx_webgpu.c needs to build the pipeline + bind groups for a
 * combiner, alongside the emitted WGSL. */
struct WgpuShaderInfo {
    struct WgpuVtxAttr attrs[24];
    int  num_attrs;
    int  num_floats;          /* vertex stride, in floats */
    int  num_inputs;          /* CCFeatures.num_inputs (shade/prim/env slots) */
    bool used_textures[2];
    bool opt_alpha;
    bool opt_texture_edge;    /* alpha-cutout: discard below the RDP edge threshold */
};

/* Build the WGSL for a combiner id. Returns a heap-allocated NUL-terminated
 * module (caller frees) and fills *info, or NULL on allocation failure. */
char *gfx_webgpu_build_wgsl(uint64_t shader_id0, uint32_t shader_id1,
                            struct WgpuShaderInfo *info);

#endif /* GFX_WEBGPU_SHADER_H */
