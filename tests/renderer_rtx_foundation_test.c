#include "renderer_rtx.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    FOUNDATION_FRAME_COUNT = 2048,
    STATIONARY_SCENE_FRAME_COUNT = 32
};

static int present_frame(RendererRtx *renderer, SceneFrame *frame,
                         RenderView *view, char *error, size_t error_size)
{
    error[0] = '\0';
    if (!renderer_rtx_present(renderer, frame, view, error, error_size)) {
        fprintf(stderr, "DXR foundation present failed: %s\n", error);
        return 0;
    }
    if (renderer_rtx_last_ui_coverage(renderer) != 0u ||
        renderer_rtx_last_view_weapon_coverage(renderer) != 0u ||
        renderer_rtx_last_view_weapon_rgb_checksum(renderer) != UINT64_C(0) ||
        renderer_rtx_last_projectile_coverage(renderer) != 0u ||
        renderer_rtx_last_frame_rgb_checksum(renderer) != UINT64_C(0)) {
        fprintf(stderr, "DXR diagnostic foundation reported scene/UI coverage\n");
        return 0;
    }
    return 1;
}

static int present_scene_frame(RendererRtx *renderer, SceneFrame *frame,
                               RenderView *view, char *error,
                               size_t error_size)
{
    error[0] = '\0';
    if (!renderer_rtx_present(renderer, frame, view, error, error_size)) {
        fprintf(stderr, "DXR dynamic-scene present failed: %s\n", error);
        return 0;
    }
    if (renderer_rtx_last_frame_rgb_checksum(renderer) == UINT64_C(0)) {
        fprintf(stderr, "DXR dynamic scene produced no RGB coverage\n");
        return 0;
    }
    return 1;
}

int main(void)
{
    static const char window_title[] = "AB3D2 DXR foundation test";
    char error[1024] = {0};
    SceneFrame frame = {0};
    RenderView view = {0};
    RendererResourceCatalog catalog = {0};
    size_t prepared_material_count = SIZE_MAX;
    int width = 0;
    int height = 0;
    int original_width = 0;
    int original_height = 0;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    RendererRtx *invalid = renderer_rtx_create(
        0, 720, window_title, 0, 1, 1u, NULL, error, sizeof(error));
    if (invalid || strstr(error, "configuration is invalid") == NULL) {
        fprintf(stderr, "DXR invalid-configuration failure was not explicit: %s\n", error);
        renderer_rtx_destroy(invalid);
        SDL_Quit();
        return 1;
    }

    /*
     * ab3d2.ini's ray-tracing settings, as the desktop entry point hands them
     * over. The hidden GPU smoke deliberately never reads ab3d2.ini, so this is
     * where the path from a settings struct to the values the path tracer runs
     * with is actually exercised.
     */
    RendererRayTracingOptions requested = {0};
    RendererRayTracingOptions applied = {0};
    requested.samples_per_pixel = 3u;
    requested.maximum_bounces = 2u;
    requested.light_candidates = 8u;
    requested.reservoir_sample_limit = 24u;
    requested.radiance_clamp = 150.0f;
    requested.exposure = 1.5f;
    requested.ndf_trim = 0.8f;
    requested.reconstruction = RENDERER_RAY_RECONSTRUCTION_BALANCED;

    RendererRtx *renderer = renderer_rtx_create(
        640, 360, window_title, 0, 1, 1u, &requested, error, sizeof(error));
    if (!renderer) {
        fprintf(stderr, "DXR foundation creation failed: %s\n", error);
        SDL_Quit();
        return 1;
    }
    if (!renderer_rtx_active_ray_tracing_options(renderer, &applied) ||
        applied.samples_per_pixel != requested.samples_per_pixel ||
        applied.maximum_bounces != requested.maximum_bounces ||
        applied.light_candidates != requested.light_candidates ||
        applied.reservoir_sample_limit != requested.reservoir_sample_limit ||
        applied.radiance_clamp != requested.radiance_clamp ||
        applied.exposure != requested.exposure ||
        applied.ndf_trim != requested.ndf_trim) {
        fprintf(stderr,
                "DXR ray-tracing settings did not reach the renderer "
                "(spp %u bounces %u candidates %u limit %u)\n",
                (unsigned)applied.samples_per_pixel,
                (unsigned)applied.maximum_bounces,
                (unsigned)applied.light_candidates,
                (unsigned)applied.reservoir_sample_limit);
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }
    if (!renderer_rtx_prepare_resources(renderer, &catalog, &prepared_material_count,
                                        error, sizeof(error)) ||
        prepared_material_count != 0u) {
        fprintf(stderr, "DXR foundation resource contract failed: %s\n", error);
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }
    if (!renderer_rtx_get_presentation_size(renderer, &original_width, &original_height)) {
        fprintf(stderr, "DXR foundation did not report its initial client size\n");
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }

    HWND window = FindWindowA(NULL, window_title);
    if (!window) {
        fprintf(stderr, "DXR foundation HWND was not discoverable for resize validation\n");
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }
    for (int frame_index = 0; frame_index < FOUNDATION_FRAME_COUNT; ++frame_index) {
        SDL_PumpEvents();
        if (frame_index == 32) {
            if (!SetWindowPos(window, NULL, 0, 0, 800, 450,
                              SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
                fprintf(stderr, "DXR foundation test could not resize its HWND\n");
                renderer_rtx_destroy(renderer);
                SDL_Quit();
                return 1;
            }
        } else if (frame_index == 64) {
            ShowWindow(window, SW_MINIMIZE);
        } else if (frame_index == 66) {
            ShowWindow(window, SW_RESTORE);
            ShowWindow(window, SW_HIDE);
        }
        if (!present_frame(renderer, &frame, &view, error, sizeof(error))) {
            renderer_rtx_destroy(renderer);
            SDL_Quit();
            return 1;
        }
    }
    if (!renderer_rtx_get_presentation_size(renderer, &width, &height) ||
        width <= 0 || height <= 0 ||
        (width == original_width && height == original_height)) {
        fprintf(stderr,
                "DXR foundation resize was not observed (before %dx%d, after %dx%d)\n",
                original_width, original_height, width, height);
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }

    SceneVertex moving_vertices[3] = {0};
    moving_vertices[0].position = (SceneWorldPoint){-64, 128, 256};
    moving_vertices[1].position = (SceneWorldPoint){64, 128, 256};
    moving_vertices[2].position = (SceneWorldPoint){0, -128, 256};
    moving_vertices[1].texture_u = 64;
    moving_vertices[2].texture_u = 32;
    moving_vertices[2].texture_v = 64;
    SceneMeshSurface moving_surface = {0};
    moving_surface.material.source =
        SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE;
    /* Use the authored technolights wall so the isolated radiance pass has a
     * real visible source without inventing ambient light for this test. */
    moving_surface.material.source_asset_id = 6u;
    moving_surface.geometry.vertices = moving_vertices;
    moving_surface.geometry.vertex_count = 3u;
    moving_surface.geometry.topology = SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST;
    moving_surface.geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_WALL;
    moving_surface.geometry.texture_window.u_period = 64u;
    moving_surface.geometry.texture_window.v_period = 128u;
    SceneCommand moving_commands[2] = {0};
    moving_commands[0].type = SCENE_COMMAND_CAMERA;
    moving_commands[1].type = SCENE_COMMAND_GEOMETRY_INSTANCE;
    moving_commands[1].data.geometry_instance.source_instance_id = 1u;
    moving_commands[1].data.geometry_instance.mesh.source_mesh_id = 1u;
    moving_commands[1].data.geometry_instance.mesh.acceleration_class =
        SCENE_ACCELERATION_CLASS_DYNAMIC;
    moving_commands[1].data.geometry_instance.mesh.surfaces = &moving_surface;
    moving_commands[1].data.geometry_instance.mesh.surface_count = 1u;
    SceneFrame moving_frame = {0};
    moving_frame.commands = moving_commands;
    moving_frame.count = 2u;
    uint64_t previous_scene_checksum = UINT64_C(0);
    for (int scene_frame = 0; scene_frame < STATIONARY_SCENE_FRAME_COUNT;
         ++scene_frame) {
        if (!present_scene_frame(renderer, &moving_frame, &view, error,
                                 sizeof(error))) {
            renderer_rtx_destroy(renderer);
            SDL_Quit();
            return 1;
        }
        const uint64_t scene_checksum =
            renderer_rtx_last_frame_rgb_checksum(renderer);
#if !defined(AB3D2_ENABLE_STREAMLINE)
        if (scene_frame != 0 && scene_checksum != previous_scene_checksum) {
            fprintf(stderr,
                    "DXR visible-emitter output changed for a stationary scene at frame %d\n",
                    scene_frame);
            renderer_rtx_destroy(renderer);
            SDL_Quit();
            return 1;
        }
#endif
        previous_scene_checksum = scene_checksum;
    }
    const uint64_t first_texture_window_checksum = previous_scene_checksum;
    moving_surface.geometry.texture_window.u_offset = 32u;
    if (!present_scene_frame(renderer, &moving_frame, &view, error,
                             sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }
    previous_scene_checksum =
        renderer_rtx_last_frame_rgb_checksum(renderer);
    if (previous_scene_checksum == first_texture_window_checksum) {
        fprintf(stderr,
                "DXR wall sampling ignored the source texture U window\n");
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }
    for (size_t vertex = 0; vertex < 3u; ++vertex) {
        moving_vertices[vertex].position.y += 32;
    }
    if (!present_scene_frame(renderer, &moving_frame, &view, error,
                             sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }
    if (renderer_rtx_last_frame_rgb_checksum(renderer) ==
        previous_scene_checksum) {
        fprintf(stderr,
                "DXR visible-emitter output did not respond to moved geometry\n");
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }
    renderer_rtx_destroy(renderer);
    SDL_Quit();
    return 0;
}
