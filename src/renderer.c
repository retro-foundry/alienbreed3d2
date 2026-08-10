#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>

#include "renderer_opengl.h"

struct Renderer {
    RendererBackend backend;
    int running;
    RendererOpenGL *opengl;
};

static void renderer_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

Renderer *renderer_create(const RendererConfig *config, char *error, size_t error_size)
{
    Renderer *renderer;

    if (!config || !config->window_title || config->window_width <= 0 ||
        config->window_height <= 0) {
        renderer_set_error(error, error_size, "renderer configuration is invalid");
        return NULL;
    }
    renderer = calloc(1u, sizeof(*renderer));
    if (!renderer) {
        renderer_set_error(error, error_size, "renderer allocation failed");
        return NULL;
    }
    renderer->backend = config->backend;
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        renderer->opengl = renderer_opengl_create(config->window_width, config->window_height,
                                                  config->window_title, config->hidden_window,
                                                  error, error_size);
        break;
    default:
        renderer_set_error(error, error_size, "requested renderer backend is not available");
        break;
    }
    if (!renderer->opengl) {
        free(renderer);
        return NULL;
    }
    renderer->running = 1;
    return renderer;
}

void renderer_destroy(Renderer *renderer)
{
    if (!renderer) {
        return;
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        renderer_opengl_destroy(renderer->opengl);
        break;
    default:
        break;
    }
    free(renderer);
}

int renderer_is_running(const Renderer *renderer)
{
    return renderer && renderer->running;
}

void renderer_request_quit(Renderer *renderer)
{
    if (renderer) {
        renderer->running = 0;
    }
}

int renderer_present(Renderer *renderer, const SceneFrame *frame, const RenderView *view,
                     char *error, size_t error_size)
{
    if (!renderer || !frame || !view) {
        renderer_set_error(error, error_size, "renderer presentation received invalid state");
        return 0;
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_present(renderer->opengl, frame, view, error, error_size);
    default:
        renderer_set_error(error, error_size, "requested renderer backend is not available");
        return 0;
    }
}

size_t renderer_last_view_weapon_coverage(const Renderer *renderer)
{
    if (!renderer) {
        return 0u;
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_last_view_weapon_coverage(renderer->opengl);
    default:
        return 0u;
    }
}

uint64_t renderer_last_frame_rgb_checksum(const Renderer *renderer)
{
    if (!renderer) {
        return UINT64_C(0);
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_last_frame_rgb_checksum(renderer->opengl);
    default:
        return UINT64_C(0);
    }
}
