#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>

#include "renderer_opengl.h"
#include "world_light_tessellation.h"
#if defined(AB3D2_ENABLE_RTX)
#include "renderer_vulkan_rtx.h"
#endif

struct Renderer {
    RendererBackend backend;
    int running;
    RendererOpenGL *opengl;
#if defined(AB3D2_ENABLE_RTX)
    RendererVulkanRtx *vulkan_rtx;
#endif
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
        !world_light_tessellation_factor_valid(config->world_light_tessellation) ||
        config->rtx_target_fps < 30u || config->rtx_target_fps > 240u ||
        config->rtx_debug_view < RENDERER_RTX_DEBUG_FINAL ||
        config->rtx_debug_view > RENDERER_RTX_DEBUG_VARIANCE) {
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
                                                  config->window_title,
                                                  config->desktop_window,
                                                  config->hidden_window,
                                                  config->world_light_tessellation,
                                                  error, error_size);
        backend_created = renderer->opengl != NULL;
        break;
    case RENDERER_BACKEND_VULKAN_RTX:
#if defined(AB3D2_ENABLE_RTX)
        renderer->vulkan_rtx = renderer_vulkan_rtx_create(
            config->window_width, config->window_height,
            config->window_title, config->desktop_window,
            config->hidden_window, config->world_light_tessellation,
            config->rtx_target_fps, config->rtx_debug_view,
            error, error_size);
        backend_created = renderer->vulkan_rtx != NULL;
#else
        renderer_set_error(
            error, error_size,
            "renderer=rtx is unavailable in this build; rebuild with AB3D2_ENABLE_RTX=ON");
#endif
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
    if (!renderer) {
        return;
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        renderer_opengl_destroy(renderer->opengl);
        break;
    case RENDERER_BACKEND_VULKAN_RTX:
#if defined(AB3D2_ENABLE_RTX)
        renderer_vulkan_rtx_destroy(renderer->vulkan_rtx);
#endif
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
    case RENDERER_BACKEND_VULKAN_RTX:
#if defined(AB3D2_ENABLE_RTX)
        return renderer_vulkan_rtx_prepare_resources(
            renderer->vulkan_rtx, catalog,
            out_prepared_vector_material_count, error, error_size);
#else
        renderer_set_error(
            error, error_size,
            "renderer=rtx is unavailable in this build; rebuild with AB3D2_ENABLE_RTX=ON");
        return 0;
#endif
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
    if (renderer) {
        renderer->running = 0;
    }
}

int renderer_get_presentation_size(const Renderer *renderer, int *out_width, int *out_height)
{
    if (!renderer || !out_width || !out_height) {
        return 0;
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_get_presentation_size(renderer->opengl,
                                                      out_width, out_height);
    case RENDERER_BACKEND_VULKAN_RTX:
#if defined(AB3D2_ENABLE_RTX)
        return renderer_vulkan_rtx_get_presentation_size(
            renderer->vulkan_rtx, out_width, out_height);
#else
        return 0;
#endif
    default:
        return 0;
    }
}

int renderer_set_rtx_debug_view(Renderer *renderer,
                                RendererRtxDebugView debug_view)
{
    if (!renderer || renderer->backend != RENDERER_BACKEND_VULKAN_RTX) {
        return 0;
    }
#if defined(AB3D2_ENABLE_RTX)
    return renderer_vulkan_rtx_set_debug_view(
        renderer->vulkan_rtx, debug_view);
#else
    (void)debug_view;
    return 0;
#endif
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
    case RENDERER_BACKEND_VULKAN_RTX:
#if defined(AB3D2_ENABLE_RTX)
        return renderer_vulkan_rtx_present(
            renderer->vulkan_rtx, frame, view, error, error_size);
#else
        renderer_set_error(
            error, error_size,
            "renderer=rtx is unavailable in this build; rebuild with AB3D2_ENABLE_RTX=ON");
        return 0;
#endif
    default:
        renderer_set_error(error, error_size, "requested renderer backend is not available");
        return 0;
    }
}

size_t renderer_last_ui_coverage(const Renderer *renderer)
{
    if (!renderer) {
        return 0u;
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_last_ui_coverage(renderer->opengl);
    case RENDERER_BACKEND_VULKAN_RTX:
#if defined(AB3D2_ENABLE_RTX)
        return renderer_vulkan_rtx_last_ui_coverage(renderer->vulkan_rtx);
#else
        return 0u;
#endif
    default:
        return 0u;
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
    case RENDERER_BACKEND_VULKAN_RTX:
#if defined(AB3D2_ENABLE_RTX)
        return renderer_vulkan_rtx_last_view_weapon_coverage(
            renderer->vulkan_rtx);
#else
        return 0u;
#endif
    default:
        return 0u;
    }
}

uint64_t renderer_last_view_weapon_rgb_checksum(const Renderer *renderer)
{
    if (!renderer) {
        return UINT64_C(0);
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_last_view_weapon_rgb_checksum(renderer->opengl);
    case RENDERER_BACKEND_VULKAN_RTX:
#if defined(AB3D2_ENABLE_RTX)
        return renderer_vulkan_rtx_last_view_weapon_rgb_checksum(
            renderer->vulkan_rtx);
#else
        return UINT64_C(0);
#endif
    default:
        return UINT64_C(0);
    }
}

size_t renderer_last_projectile_coverage(const Renderer *renderer)
{
    if (!renderer) {
        return 0u;
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_OPENGL:
        return renderer_opengl_last_projectile_coverage(renderer->opengl);
    case RENDERER_BACKEND_VULKAN_RTX:
#if defined(AB3D2_ENABLE_RTX)
        return renderer_vulkan_rtx_last_projectile_coverage(
            renderer->vulkan_rtx);
#else
        return 0u;
#endif
    default:
        return 0u;
    }
}

size_t renderer_last_indirect_light_coverage(const Renderer *renderer)
{
    if (!renderer) {
        return 0u;
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_VULKAN_RTX:
#if defined(AB3D2_ENABLE_RTX)
        return renderer_vulkan_rtx_last_indirect_light_coverage(
            renderer->vulkan_rtx);
#else
        return 0u;
#endif
    case RENDERER_BACKEND_OPENGL:
    default:
        return 0u;
    }
}

size_t renderer_last_indirect_light_energy(const Renderer *renderer)
{
    if (!renderer) {
        return 0u;
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_VULKAN_RTX:
#if defined(AB3D2_ENABLE_RTX)
        return renderer_vulkan_rtx_last_indirect_light_energy(
            renderer->vulkan_rtx);
#else
        return 0u;
#endif
    case RENDERER_BACKEND_OPENGL:
    default:
        return 0u;
    }
}

size_t renderer_last_direct_light_energy(const Renderer *renderer)
{
    if (!renderer) {
        return 0u;
    }
    switch (renderer->backend) {
    case RENDERER_BACKEND_VULKAN_RTX:
#if defined(AB3D2_ENABLE_RTX)
        return renderer_vulkan_rtx_last_direct_light_energy(
            renderer->vulkan_rtx);
#else
        return 0u;
#endif
    case RENDERER_BACKEND_OPENGL:
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
    case RENDERER_BACKEND_VULKAN_RTX:
#if defined(AB3D2_ENABLE_RTX)
        return renderer_vulkan_rtx_last_frame_rgb_checksum(
            renderer->vulkan_rtx);
#else
        return UINT64_C(0);
#endif
    default:
        return UINT64_C(0);
    }
}
