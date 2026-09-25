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

typedef enum {
    RENDERER_RTX_RADIANCE_COMBINED = 0,
    RENDERER_RTX_RADIANCE_EMISSION = 1,
    RENDERER_RTX_RADIANCE_DIRECT_DIFFUSE = 2,
    RENDERER_RTX_RADIANCE_DIRECT_SPECULAR = 3,
    RENDERER_RTX_RADIANCE_INDIRECT = 4,
    RENDERER_RTX_RADIANCE_SMOOTH_SPECULAR = 5,
    RENDERER_RTX_RADIANCE_ROUGH_SPECULAR = 6,
} RendererRtxRadianceChannel;

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
/* Throttle the flip queue before input is sampled for the next frame. */
int renderer_rtx_wait_for_present(
    RendererRtx *renderer, char *error, size_t error_size);
int renderer_rtx_present(
    RendererRtx *renderer, const SceneFrame *frame, const RenderView *view,
    char *error, size_t error_size);
size_t renderer_rtx_last_ui_coverage(const RendererRtx *renderer);
size_t renderer_rtx_last_view_weapon_coverage(const RendererRtx *renderer);
uint64_t renderer_rtx_last_view_weapon_rgb_checksum(const RendererRtx *renderer);
size_t renderer_rtx_last_world_bitmap_coverage(const RendererRtx *renderer);
size_t renderer_rtx_last_world_vector_coverage(const RendererRtx *renderer);
size_t renderer_rtx_last_world_additive_coverage(const RendererRtx *renderer);
/* Readback-only path-tracer validation; these values never affect shading. */
size_t renderer_rtx_last_direct_diffuse_coverage(const RendererRtx *renderer);
size_t renderer_rtx_last_direct_specular_coverage(const RendererRtx *renderer);
size_t renderer_rtx_last_invalid_lighting_or_guide_pixels(
    const RendererRtx *renderer);
size_t renderer_rtx_last_smooth_specular_coverage(const RendererRtx *renderer);
/* Opt-in hidden-validation readback of the RGB half-floats at the final noisy
 * HDR boundary before Ray Reconstruction and post processing. Production
 * rendering incurs no copy unless this is enabled before presentation. */
int renderer_rtx_enable_noisy_radiance_readback(RendererRtx *renderer);
/* Validation-only composition switch. It does not alter scene/sample history;
 * a caller requiring sample zero must advance SceneFrame::history_epoch. */
int renderer_rtx_select_radiance_channel(
    RendererRtx *renderer, RendererRtxRadianceChannel channel);
size_t renderer_rtx_last_noisy_radiance_value_count(
    const RendererRtx *renderer);
int renderer_rtx_copy_last_noisy_radiance(
    const RendererRtx *renderer, uint16_t *out_values, size_t value_count);
/* Reports the ray-tracing settings in force. Zero on failure. */
int renderer_rtx_active_ray_tracing_options(
    const RendererRtx *renderer, RendererRayTracingOptions *out_options);
size_t renderer_rtx_last_projectile_coverage(const RendererRtx *renderer);
uint64_t renderer_rtx_last_frame_rgb_checksum(const RendererRtx *renderer);
double renderer_rtx_last_frame_delta(const RendererRtx *renderer);
double renderer_rtx_last_frame_reprojected_delta(const RendererRtx *renderer);
uint64_t renderer_rtx_last_frame_nonzero_pixels(const RendererRtx *renderer);
double renderer_rtx_last_frame_mean_luminance(const RendererRtx *renderer);
uint64_t renderer_rtx_last_frame_saturated_pixels(const RendererRtx *renderer);
uint64_t renderer_rtx_last_frame_temporal_outlier_pixels(
    const RendererRtx *renderer);
uint64_t renderer_rtx_last_frame_reprojected_temporal_outlier_pixels(
    const RendererRtx *renderer);
uint64_t renderer_rtx_last_frame_reprojected_pixel_count(
    const RendererRtx *renderer);
uint64_t renderer_rtx_last_scene_emissive_scale_fold(const RendererRtx *renderer);
/* Monotonic diagnostic count of complete CPU/GPU scene-layout rebuilds. */
uint64_t renderer_rtx_scene_rebuild_count(const RendererRtx *renderer);

#if defined(__cplusplus)
}
#endif

#endif
