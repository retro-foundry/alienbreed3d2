#ifndef AB3D2_RENDERER_H
#define AB3D2_RENDERER_H

#include <stddef.h>

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
} RendererConfig;

typedef struct Renderer Renderer;

Renderer *renderer_create(const RendererConfig *config, char *error, size_t error_size);
void renderer_destroy(Renderer *renderer);
int renderer_is_running(const Renderer *renderer);
void renderer_request_quit(Renderer *renderer);
int renderer_present(Renderer *renderer, const SceneFrame *frame, const RenderView *view,
                     char *error, size_t error_size);

#endif
