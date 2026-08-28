#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>

#include "renderer_opengl.h"
#include "renderer_rtx.h"
#include "world_light_tessellation.h"

struct Renderer {
    RendererBackend backend;
    int running;
    RendererOpenGL *opengl;
    RendererRtx *rtx;
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
    int backend_created = 0;

    if (!config || !config->window_title || config->window_width <= 0 ||
        config->window_height <= 0 ||
        !world_light_tessellation_factor_valid(config->world_light_tessellation)) {
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
        renderer->opengl = renderer_opengl_create(
            config->window_width, config->window_height, config->window_title,
            config->desktop_window, config->hidden_window,
            config->world_light_tessellation, error, error_size);
        backend_created = renderer->opengl != NULL;
        break;
    case RENDERER_BACKEND_RTX:
        renderer->rtx = renderer_rtx_create(
            config->window_width, config->window_height, config->window_title,
            config->desktop_window, config->hidden_window,
            config->world_light_tessellation, &config->ray_tracing,
            error, error_size);
        backend_created = renderer->rtx != NULL;
        break;
    default:
        renderer_set_error(error, error_size, "requested renderer backend is not available");
        break;
    }
    if (!backend_created) {
        free(renderer);
        return NULL;
    }
    renderer->running = 1;
    return renderer;
}

void renderer_destroy(Renderer *renderer)
{
    if (!renderer) return;
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        renderer_opengl_destroy(renderer->opengl);
        break;
    case RENDERER_BACKEND_RTX:
        renderer_rtx_destroy(renderer->rtx);
        break;
    default:
        break;
    }
    free(renderer);
}

int renderer_prepare_resources(Renderer *renderer, const RendererResourceCatalog *catalog,
                               size_t *out_prepared_vector_material_count,
                               char *error, size_t error_size)
{
    if (!renderer || !catalog || !out_prepared_vector_material_count) {
        renderer_set_error(error, error_size,
                           "renderer resource preparation received invalid state");
        return 0;
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_prepare_resources(
            renderer->opengl, catalog, out_prepared_vector_material_count,
            error, error_size);
    case RENDERER_BACKEND_RTX:
        return renderer_rtx_prepare_resources(
            renderer->rtx, catalog, out_prepared_vector_material_count,
            error, error_size);
    default:
        renderer_set_error(error, error_size, "requested renderer backend is not available");
        return 0;
    }
}

int renderer_is_running(const Renderer *renderer)
{
    return renderer && renderer->running;
}

void renderer_request_quit(Renderer *renderer)
{
    if (renderer) renderer->running = 0;
}

int renderer_get_presentation_size(const Renderer *renderer, int *out_width, int *out_height)
{
    if (!renderer || !out_width || !out_height) return 0;
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_get_presentation_size(
            renderer->opengl, out_width, out_height);
    case RENDERER_BACKEND_RTX:
        return renderer_rtx_get_presentation_size(
            renderer->rtx, out_width, out_height);
    default:
        return 0;
    }
}

int renderer_wait_for_present(Renderer *renderer, char *error, size_t error_size)
{
    if (!renderer) {
        renderer_set_error(error, error_size,
                           "renderer frame-latency wait received invalid state");
        return 0;
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return 1;
    case RENDERER_BACKEND_RTX:
        return renderer_rtx_wait_for_present(renderer->rtx, error, error_size);
    default:
        renderer_set_error(error, error_size,
                           "requested renderer backend is not available");
        return 0;
    }
}

int renderer_present(Renderer *renderer, const SceneFrame *frame, const RenderView *view,
                     char *error, size_t error_size)
{
    if (!renderer || !frame || !view) {
        renderer_set_error(error, error_size,
                           "renderer presentation received invalid state");
        return 0;
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_present(
            renderer->opengl, frame, view, error, error_size);
    case RENDERER_BACKEND_RTX:
        return renderer_rtx_present(
            renderer->rtx, frame, view, error, error_size);
    default:
        renderer_set_error(error, error_size, "requested renderer backend is not available");
        return 0;
    }
}

size_t renderer_last_ui_coverage(const Renderer *renderer)
{
    if (!renderer) return 0u;
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_last_ui_coverage(renderer->opengl);
    case RENDERER_BACKEND_RTX:
        return renderer_rtx_last_ui_coverage(renderer->rtx);
    default:
        return 0u;
    }
}

size_t renderer_last_view_weapon_coverage(const Renderer *renderer)
{
    if (!renderer) return 0u;
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_last_view_weapon_coverage(renderer->opengl);
    case RENDERER_BACKEND_RTX:
        return renderer_rtx_last_view_weapon_coverage(renderer->rtx);
    default:
        return 0u;
    }
}

uint64_t renderer_last_view_weapon_rgb_checksum(const Renderer *renderer)
{
    if (!renderer) return UINT64_C(0);
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_last_view_weapon_rgb_checksum(renderer->opengl);
    case RENDERER_BACKEND_RTX:
        return renderer_rtx_last_view_weapon_rgb_checksum(renderer->rtx);
    default:
        return UINT64_C(0);
    }
}

size_t renderer_last_world_bitmap_coverage(const Renderer *renderer)
{
    if (!renderer || renderer->backend != RENDERER_BACKEND_RTX) return 0u;
    return renderer_rtx_last_world_bitmap_coverage(renderer->rtx);
}

size_t renderer_last_world_vector_coverage(const Renderer *renderer)
{
    if (!renderer || renderer->backend != RENDERER_BACKEND_RTX) return 0u;
    return renderer_rtx_last_world_vector_coverage(renderer->rtx);
}

size_t renderer_last_world_additive_coverage(const Renderer *renderer)
{
    if (!renderer || renderer->backend != RENDERER_BACKEND_RTX) return 0u;
    return renderer_rtx_last_world_additive_coverage(renderer->rtx);
}

size_t renderer_last_projectile_coverage(const Renderer *renderer)
{
    if (!renderer) return 0u;
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_last_projectile_coverage(renderer->opengl);
    case RENDERER_BACKEND_RTX:
        return renderer_rtx_last_projectile_coverage(renderer->rtx);
    default:
        return 0u;
    }
}

uint64_t renderer_last_frame_rgb_checksum(const Renderer *renderer)
{
    if (!renderer) return UINT64_C(0);
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_last_frame_rgb_checksum(renderer->opengl);
    case RENDERER_BACKEND_RTX:
        return renderer_rtx_last_frame_rgb_checksum(renderer->rtx);
    default:
        return UINT64_C(0);
    }
}

double renderer_last_frame_delta(const Renderer *renderer)
{
    if (!renderer) return -1.0;
    switch (renderer->backend) {
    case RENDERER_BACKEND_RTX:
        return renderer_rtx_last_frame_delta(renderer->rtx);
    default:
        return -1.0;
    }
}

uint64_t renderer_last_frame_saturated_pixels(const Renderer *renderer)
{
    if (!renderer) return UINT64_C(0);
    switch (renderer->backend) {
    case RENDERER_BACKEND_RTX:
        return renderer_rtx_last_frame_saturated_pixels(renderer->rtx);
    default:
        return UINT64_C(0);
    }
}

uint64_t renderer_last_frame_temporal_outlier_pixels(const Renderer *renderer)
{
    if (!renderer) return UINT64_C(0);
    switch (renderer->backend) {
    case RENDERER_BACKEND_RTX:
        return renderer_rtx_last_frame_temporal_outlier_pixels(renderer->rtx);
    default:
        return UINT64_C(0);
    }
}

uint64_t renderer_scene_rebuild_count(const Renderer *renderer)
{
    if (!renderer) return UINT64_C(0);
    switch (renderer->backend) {
    case RENDERER_BACKEND_RTX:
        return renderer_rtx_scene_rebuild_count(renderer->rtx);
    default:
        return UINT64_C(0);
    }
}
