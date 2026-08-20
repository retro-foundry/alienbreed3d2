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
 * point depend only on this interface. renderer_opengl.c implements the live
 * backend. The clean-room RTX boundary selects either the fail-fast stub or
 * the opt-in Windows D3D12/DXR diagnostic foundation without changing
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
int renderer_present(Renderer *renderer, const SceneFrame *frame, const RenderView *view,
                     char *error, size_t error_size);
/* Nonzero only when hidden GPU smoke rendered visible UI glyph pixels. */
size_t renderer_last_ui_coverage(const Renderer *renderer);
/* Nonzero only for the hidden GPU-smoke frame's rendered Player 1 weapon. */
size_t renderer_last_view_weapon_coverage(const Renderer *renderer);
/* Hidden GPU-smoke checksum of the camera-space weapon's changed RGB pixels. */
uint64_t renderer_last_view_weapon_rgb_checksum(const Renderer *renderer);
/* Hidden DXR-smoke primary-ray pixels for world billboard/effect and vector
 * entity geometry after alpha testing. */
size_t renderer_last_world_bitmap_coverage(const Renderer *renderer);
size_t renderer_last_world_vector_coverage(const Renderer *renderer);
size_t renderer_last_world_additive_coverage(const Renderer *renderer);
/* Hidden GPU-smoke coverage for source projectile and fragment bitmap draws. */
size_t renderer_last_projectile_coverage(const Renderer *renderer);
/* Hidden GPU-smoke checksum of the fully presented framebuffer's RGB output. */
uint64_t renderer_last_frame_rgb_checksum(const Renderer *renderer);

/* Hidden GPU-smoke temporal-stability metric: mean absolute per-component
 * difference between the last two presented frames on the 0-255 display scale,
 * or a negative value when the backend has not read back two frames. */
double renderer_last_frame_delta(const Renderer *renderer);

/* Hidden GPU-smoke count of presented pixels with any component at or above
 * 250, a proxy for radiance outliers that survive tone mapping. */
uint64_t renderer_last_frame_saturated_pixels(const Renderer *renderer);

/* Hidden GPU-smoke fold of the per-vertex source Gouraud shade response the
 * ray-traced backend uploaded for the last presented scene, which is what
 * scales authored emission there. It is the only way to observe
 * newanims.s:brightanim reaching that backend while its fresh-sample noise
 * still dominates any image comparison. Zero on backends that trace no
 * authored emission. */
uint64_t renderer_last_scene_emissive_scale_fold(const Renderer *renderer);

/* Hidden GPU-smoke count of complete RTX scene-layout rebuilds. */
uint64_t renderer_scene_rebuild_count(const Renderer *renderer);

#endif
