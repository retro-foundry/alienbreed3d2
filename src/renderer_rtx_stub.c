#include "renderer_rtx.h"

#include <stdio.h>

struct RendererRtx {
    unsigned reserved;
};

static void renderer_rtx_not_implemented(char *error, size_t error_size)
{
    if (error && error_size > 0u) {
        (void)snprintf(
            error, error_size,
            "renderer=rtx was compiled out (AB3D2_ENABLE_DXR=OFF); "
            "configure a native Windows build with -DAB3D2_ENABLE_DXR=ON");
    }
}

RendererRtx *renderer_rtx_create(
    int window_width, int window_height, const char *window_title,
    int desktop_window, int hidden_window, uint8_t world_light_tessellation,
    const RendererRayTracingOptions *options,
    char *error, size_t error_size)
{
    (void)window_width;
    (void)window_height;
    (void)window_title;
    (void)desktop_window;
    (void)hidden_window;
    (void)world_light_tessellation;
    (void)options;
    renderer_rtx_not_implemented(error, error_size);
    return NULL;
}

void renderer_rtx_destroy(RendererRtx *renderer)
{
    (void)renderer;
}

int renderer_rtx_prepare_resources(
    RendererRtx *renderer, const RendererResourceCatalog *catalog,
    size_t *out_prepared_vector_material_count, char *error, size_t error_size)
{
    (void)renderer;
    (void)catalog;
    (void)out_prepared_vector_material_count;
    renderer_rtx_not_implemented(error, error_size);
    return 0;
}

int renderer_rtx_get_presentation_size(
    const RendererRtx *renderer, int *out_width, int *out_height)
{
    (void)renderer;
    (void)out_width;
    (void)out_height;
    return 0;
}

int renderer_rtx_present(
    RendererRtx *renderer, const SceneFrame *frame, const RenderView *view,
    char *error, size_t error_size)
{
    (void)renderer;
    (void)frame;
    (void)view;
    renderer_rtx_not_implemented(error, error_size);
    return 0;
}

size_t renderer_rtx_last_ui_coverage(const RendererRtx *renderer)
{
    (void)renderer;
    return 0u;
}

size_t renderer_rtx_last_view_weapon_coverage(const RendererRtx *renderer)
{
    (void)renderer;
    return 0u;
}

uint64_t renderer_rtx_last_view_weapon_rgb_checksum(const RendererRtx *renderer)
{
    (void)renderer;
    return UINT64_C(0);
}

size_t renderer_rtx_last_projectile_coverage(const RendererRtx *renderer)
{
    (void)renderer;
    return 0u;
}

uint64_t renderer_rtx_last_frame_rgb_checksum(const RendererRtx *renderer)
{
    (void)renderer;
    return UINT64_C(0);
}

double renderer_rtx_last_frame_delta(const RendererRtx *renderer)
{
    (void)renderer;
    return -1.0;
}

uint64_t renderer_rtx_last_frame_saturated_pixels(const RendererRtx *renderer)
{
    (void)renderer;
    return UINT64_C(0);
}

uint64_t renderer_rtx_last_scene_emissive_scale_fold(const RendererRtx *renderer)
{
    (void)renderer;
    return UINT64_C(0);
}

size_t renderer_rtx_last_world_bitmap_coverage(const RendererRtx *renderer)
{
    (void)renderer;
    return 0u;
}

size_t renderer_rtx_last_world_vector_coverage(const RendererRtx *renderer)
{
    (void)renderer;
    return 0u;
}

size_t renderer_rtx_last_world_additive_coverage(const RendererRtx *renderer)
{
    (void)renderer;
    return 0u;
}

int renderer_rtx_active_ray_tracing_options(
    const RendererRtx *renderer, RendererRayTracingOptions *out_options)
{
    (void)renderer;
    (void)out_options;
    return 0;
}

uint64_t renderer_rtx_scene_rebuild_count(const RendererRtx *renderer)
{
    (void)renderer;
    return UINT64_C(0);
}
