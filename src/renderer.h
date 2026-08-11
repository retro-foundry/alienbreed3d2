#ifndef AB3D2_RENDERER_H
#define AB3D2_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#include "render_view.h"
#include "scene_frame.h"

/*
 * API-neutral presenter boundary.  Gameplay producers and the desktop entry
 * point depend only on this interface; renderer_opengl.c is one backend and a
 * later DirectX backend can implement the same operations without changing
 * SceneFrame or game simulation.
 */
typedef enum {
    RENDERER_BACKEND_OPENGL
} RendererBackend;

typedef struct {
    RendererBackend backend;
    int window_width;
    int window_height;
    const char *window_title;
    /*
     * Match the native Alien Breed 3D I startup display mode: create a
     * desktop-sized window at the active display bounds without asking SDL to
     * change the monitor's display mode.  This remains a presentation choice
     * at the renderer boundary, rather than a gameplay concern.
     */
    int fullscreen_desktop;
    /* Opt-in validation path: an SDL hidden window still exercises real GL. */
    int hidden_window;
} RendererConfig;

typedef struct Renderer Renderer;

Renderer *renderer_create(const RendererConfig *config, char *error, size_t error_size);
void renderer_destroy(Renderer *renderer);
int renderer_is_running(const Renderer *renderer);
void renderer_request_quit(Renderer *renderer);
int renderer_present(Renderer *renderer, const SceneFrame *frame, const RenderView *view,
                     char *error, size_t error_size);
/* Nonzero only for the hidden GPU-smoke frame's rendered Player 1 weapon. */
size_t renderer_last_view_weapon_coverage(const Renderer *renderer);
/* Hidden GPU-smoke checksum of the camera-space weapon's changed RGB pixels. */
uint64_t renderer_last_view_weapon_rgb_checksum(const Renderer *renderer);
/* Hidden GPU-smoke coverage for source projectile and fragment bitmap draws. */
size_t renderer_last_projectile_coverage(const Renderer *renderer);
/* Hidden GPU-smoke checksum of the fully presented framebuffer's RGB output. */
uint64_t renderer_last_frame_rgb_checksum(const Renderer *renderer);

#endif
