#ifndef AB3D2_RENDERER_RTX_H
#define AB3D2_RENDERER_RTX_H

#include <stddef.h>
#include <stdint.h>

#include "render_view.h"
#include "renderer_resources.h"
#include "scene_frame.h"

/*
 * Clean-room boundary for the native RTX backend.  A disabled or unsupported
 * build supplies the fail-fast stub.  The Phase 2 Windows implementation is
 * deliberately a D3D12/DXR diagnostic presenter and consumes no SceneFrame
 * content until the later scene milestones are implemented.
 */
typedef struct RendererRtx RendererRtx;

#if defined(__cplusplus)
extern "C" {
#endif

RendererRtx *renderer_rtx_create(
    int window_width, int window_height, const char *window_title,
    int desktop_window, int hidden_window, uint8_t world_light_tessellation,
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
size_t renderer_rtx_last_projectile_coverage(const RendererRtx *renderer);
uint64_t renderer_rtx_last_frame_rgb_checksum(const RendererRtx *renderer);

#if defined(__cplusplus)
}
#endif

#endif
