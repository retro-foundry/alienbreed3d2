#ifndef AB3D2_RENDERER_H
#define AB3D2_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#include "render_view.h"
#include "renderer_backend.h"
#include "renderer_resources.h"
#include "scene_frame.h"

/*
 * API-neutral presenter boundary.  Gameplay producers and the desktop entry
 * point depend only on this interface; renderer_opengl.c is one backend and a
 * later DirectX backend can implement the same operations without changing
 * SceneFrame or game simulation.
 */
typedef struct {
    RendererBackend backend;
    int window_width;
    int window_height;
    const char *window_title;
    /*
     * Create the same desktop-sized, positioned normal window as the first
     * port's display_init.  This is a presentation choice at the renderer
     * boundary, rather than gameplay.
     */
    int desktop_window;
    /* Opt-in validation path: an SDL hidden window still exercises real GL. */
    int hidden_window;
    /* Presentation-only source-mesh subdivision: 1, 2, 4, or 8. */
    uint8_t world_light_tessellation;
    uint8_t rtx_dynamic_resolution;
    uint8_t rtx_resolution_scale;
    uint8_t rtx_denoiser_iterations;
    uint8_t rtx_bloom;
    uint16_t rtx_target_fps;
    RendererRtxDebugView rtx_debug_view;
} RendererConfig;

typedef struct Renderer Renderer;

Renderer *renderer_create(const RendererConfig *config, char *error, size_t error_size);
void renderer_destroy(Renderer *renderer);
/* Convert immutable Game_Start resources before any gameplay frame is presented. */
int renderer_prepare_resources(Renderer *renderer, const RendererResourceCatalog *catalog,
                               size_t *out_prepared_vector_material_count,
                               char *error, size_t error_size);
int renderer_is_running(const Renderer *renderer);
void renderer_request_quit(Renderer *renderer);
/* Live drawable extent used by both presentation and relative-mouse scaling. */
int renderer_get_presentation_size(const Renderer *renderer, int *out_width, int *out_height);
/* Hidden validation hook; returns zero for a non-RTX backend. */
int renderer_set_rtx_debug_view(Renderer *renderer,
                                RendererRtxDebugView debug_view);
int renderer_present(Renderer *renderer, const SceneFrame *frame, const RenderView *view,
                     char *error, size_t error_size);
/* Nonzero only when hidden GPU smoke rendered visible UI glyph pixels. */
size_t renderer_last_ui_coverage(const Renderer *renderer);
/* Nonzero only for the hidden GPU-smoke frame's rendered Player 1 weapon. */
size_t renderer_last_view_weapon_coverage(const Renderer *renderer);
/* Hidden GPU-smoke checksum of the camera-space weapon's changed RGB pixels. */
uint64_t renderer_last_view_weapon_rgb_checksum(const Renderer *renderer);
/* Hidden GPU-smoke coverage for source projectile and fragment bitmap draws. */
size_t renderer_last_projectile_coverage(const Renderer *renderer);
/* Hidden RTX smoke coverage for secondary path-tracing intersections. */
size_t renderer_last_indirect_light_coverage(const Renderer *renderer);
/* Hidden RTX smoke attempts and accepted taps for secondary-light history. */
size_t renderer_last_secondary_history_attempts(const Renderer *renderer);
size_t renderer_last_secondary_history_accepted(const Renderer *renderer);
/* Quantized nonzero radiance delivered by the hidden RTX smoke paths. */
size_t renderer_last_indirect_light_energy(const Renderer *renderer);
/* Quantized emissive direct-light radiance delivered by hidden RTX smoke. */
size_t renderer_last_direct_light_energy(const Renderer *renderer);
/* Hidden RTX smoke counters proving Q2RTX light sampling is exercised. */
size_t renderer_last_light_shadow_samples(const Renderer *renderer);
size_t renderer_last_per_light_history_samples(const Renderer *renderer);
/* Hidden GPU-smoke checksum of the fully presented framebuffer's RGB output. */
uint64_t renderer_last_frame_rgb_checksum(const Renderer *renderer);

#endif
