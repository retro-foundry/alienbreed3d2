#ifndef AB3D2_RENDERER_RTX_H
#define AB3D2_RENDERER_RTX_H

#include <stddef.h>
#include <stdint.h>

#include "render_view.h"
#include "renderer_ray_tracing_options.h"
#include "renderer_resources.h"
#include "scene_frame.h"

/*
 * Clean-room boundary for the native RTX backend.  A disabled or unsupported
 * build supplies the fail-fast stub.  The enabled Windows implementation ray
 * traces SceneFrame world geometry, non-projectile bitmap/glare commands,
 * animated world vector models, and the exact ENT_NEXT_2 companion weapon in
 * one shared PBR scene before Ray Reconstruction.  Transient projectile
 * sprites, HUD, and text remain outside this milestone.
 */
typedef struct RendererRtx RendererRtx;

#if defined(__cplusplus)
extern "C" {
#endif

/* `options` may be NULL, which keeps every renderer default. */
RendererRtx *renderer_rtx_create(
    int window_width, int window_height, const char *window_title,
    int desktop_window, int hidden_window, uint8_t world_light_tessellation,
    const RendererRayTracingOptions *options,
    char *error, size_t error_size);
void renderer_rtx_destroy(RendererRtx *renderer);
int renderer_rtx_prepare_resources(
    RendererRtx *renderer, const RendererResourceCatalog *catalog,
    size_t *out_prepared_vector_material_count, char *error, size_t error_size);
int renderer_rtx_get_presentation_size(
    const RendererRtx *renderer, int *out_width, int *out_height);
int renderer_rtx_present(
    RendererRtx *renderer, const SceneFrame *frame, const RenderView *view,
    char *error, size_t error_size);
size_t renderer_rtx_last_ui_coverage(const RendererRtx *renderer);
size_t renderer_rtx_last_view_weapon_coverage(const RendererRtx *renderer);
uint64_t renderer_rtx_last_view_weapon_rgb_checksum(const RendererRtx *renderer);
size_t renderer_rtx_last_world_bitmap_coverage(const RendererRtx *renderer);
size_t renderer_rtx_last_world_vector_coverage(const RendererRtx *renderer);
size_t renderer_rtx_last_world_additive_coverage(const RendererRtx *renderer);
/* Reports the ray-tracing settings in force. Zero on failure. */
int renderer_rtx_active_ray_tracing_options(
    const RendererRtx *renderer, RendererRayTracingOptions *out_options);
size_t renderer_rtx_last_projectile_coverage(const RendererRtx *renderer);
uint64_t renderer_rtx_last_frame_rgb_checksum(const RendererRtx *renderer);
double renderer_rtx_last_frame_delta(const RendererRtx *renderer);
uint64_t renderer_rtx_last_frame_saturated_pixels(const RendererRtx *renderer);
uint64_t renderer_rtx_last_frame_temporal_outlier_pixels(
    const RendererRtx *renderer);
uint64_t renderer_rtx_last_scene_emissive_scale_fold(const RendererRtx *renderer);
/* Monotonic diagnostic count of complete CPU/GPU scene-layout rebuilds. */
uint64_t renderer_rtx_scene_rebuild_count(const RendererRtx *renderer);

#if defined(__cplusplus)
}
#endif

#endif
