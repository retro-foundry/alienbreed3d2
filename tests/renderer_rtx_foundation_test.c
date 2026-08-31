#include "renderer_rtx.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    FOUNDATION_FRAME_COUNT = 2048,
    STATIONARY_SCENE_FRAME_COUNT = 32,
    DIRECT_REFERENCE_FRAME_COUNT = 8
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
        renderer_rtx_last_frame_rgb_checksum(renderer) != UINT64_C(0) ||
        renderer_rtx_last_invalid_lighting_or_guide_pixels(renderer) != 0u) {
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
    if (renderer_rtx_last_invalid_lighting_or_guide_pixels(renderer) != 0u) {
        fprintf(stderr,
                "DXR dynamic scene produced non-finite lighting or RR guides\n");
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
#if !defined(AB3D2_ENABLE_STREAMLINE)
    /* The signed Streamline runtime deliberately supports one verified
     * initialize/shutdown lifecycle per process, so exercise this early device
     * rejection only in the native-DXGI foundation target. */
    if (!SetEnvironmentVariableA("AB3D2_DXR_OUTPUT", "wide-gamut")) {
        fprintf(stderr, "could not set DXR output override for validation\n");
        SDL_Quit();
        return 1;
    }
    error[0] = '\0';
    invalid = renderer_rtx_create(
        640, 360, window_title, 0, 1, 1u, NULL, error, sizeof(error));
    (void)SetEnvironmentVariableA("AB3D2_DXR_OUTPUT", NULL);
    if (invalid || strstr(error, "AB3D2_DXR_OUTPUT") == NULL) {
        fprintf(stderr, "DXR invalid output override was not explicit: %s\n",
                error);
        renderer_rtx_destroy(invalid);
        SDL_Quit();
        return 1;
    }
#endif

    /*
     * ab3d2.ini's ray-tracing settings, as the desktop entry point hands them
     * over. The hidden GPU smoke deliberately never reads ab3d2.ini, so this is
     * where the path from a settings struct to the values the path tracer runs
     * with is actually exercised.
     */
    RendererRayTracingOptions requested = {0};
    RendererRayTracingOptions applied = {0};
    requested.samples_per_pixel = 3u;
    requested.indirect_samples_per_pixel = 7u;
    requested.indirect_light_samples = 2u;
    requested.indirect_temporal_frames = 1u;
    requested.diffuse_gi_scale = 0.6f;
    requested.diffuse_gi_scale_set = UINT8_MAX;
    requested.maximum_bounces = 2u;
    requested.light_candidates = 8u;
    requested.reservoir_sample_limit = 24u;
    requested.radiance_clamp = 0.0f;
    requested.exposure_bias_stops = -1.5f;
    requested.exposure_bias_set = UINT8_MAX;
    requested.ndf_trim = 0.8f;
    requested.reconstruction = RENDERER_RAY_RECONSTRUCTION_BALANCED;
    requested.output = RENDERER_OUTPUT_HDR;
    requested.hdr_peak_nits = 1000.0f;
    requested.hdr_saturation_percent = 100.0f;
    requested.hdr_saturation_percent_set = UINT8_MAX;

    /* Matching one-run environment controls are accepted before hidden
     * validation applies its final forced-SDR decision. */
    if (!SetEnvironmentVariableA("AB3D2_DXR_OUTPUT", "hdr") ||
        !SetEnvironmentVariableA("AB3D2_DXR_HDR_PEAK_NITS", "1200") ||
        !SetEnvironmentVariableA("AB3D2_DXR_HDR_SATURATION", "105")) {
        fprintf(stderr, "could not set valid DXR output overrides\n");
        SDL_Quit();
        return 1;
    }
    RendererRtx *renderer = renderer_rtx_create(
        640, 360, window_title, 0, 1, 1u, &requested, error, sizeof(error));
    (void)SetEnvironmentVariableA("AB3D2_DXR_OUTPUT", NULL);
    (void)SetEnvironmentVariableA("AB3D2_DXR_HDR_PEAK_NITS", NULL);
    (void)SetEnvironmentVariableA("AB3D2_DXR_HDR_SATURATION", NULL);
    if (!renderer) {
        fprintf(stderr, "DXR foundation creation failed: %s\n", error);
        SDL_Quit();
        return 1;
    }
    if (!renderer_rtx_wait_for_present(renderer, error, sizeof(error))) {
        fprintf(stderr, "DXR foundation frame-latency wait failed: %s\n", error);
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }
    if (!renderer_rtx_active_ray_tracing_options(renderer, &applied) ||
        applied.samples_per_pixel != requested.samples_per_pixel ||
        applied.indirect_samples_per_pixel !=
            requested.indirect_samples_per_pixel ||
        applied.indirect_light_samples != requested.indirect_light_samples ||
        applied.indirect_temporal_frames !=
            requested.indirect_temporal_frames ||
        applied.diffuse_gi_scale != requested.diffuse_gi_scale ||
        applied.diffuse_gi_scale_set == 0u ||
        applied.maximum_bounces != requested.maximum_bounces ||
        applied.light_candidates != requested.light_candidates ||
        applied.reservoir_sample_limit != requested.reservoir_sample_limit ||
        applied.radiance_clamp != requested.radiance_clamp ||
        applied.exposure_bias_stops != requested.exposure_bias_stops ||
        applied.exposure_bias_set == 0u ||
        applied.ndf_trim != requested.ndf_trim ||
        applied.output != RENDERER_OUTPUT_SDR ||
        applied.hdr_peak_nits != 0.0f ||
        applied.hdr_saturation_percent != 0.0f ||
        applied.hdr_saturation_percent_set != 0u) {
        fprintf(stderr,
                "DXR ray-tracing settings did not reach the renderer "
                "(direct spp %u indirect spp %u indirect light samples %u GI frames %u bounces %u candidates %u limit %u)\n",
                (unsigned)applied.samples_per_pixel,
                (unsigned)applied.indirect_samples_per_pixel,
                (unsigned)applied.indirect_light_samples,
                (unsigned)applied.indirect_temporal_frames,
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
        /* Temporal blue-noise dithering may move final SDR components by one
         * UNORM code even when the underlying HDR scene is stationary. Keep
         * the regression on meaningful instability, not byte identity. */
        if (scene_frame != 0 &&
            renderer_rtx_last_frame_delta(renderer) > 0.5) {
            fprintf(stderr,
                    "DXR visible-emitter output was unstable for a stationary "
                    "scene at frame %d (delta %.6f)\n",
                    scene_frame, renderer_rtx_last_frame_delta(renderer));
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

    /* A visible non-emissive dielectric receiver and a deliberately off-screen
     * authored technolights triangle exercise actual polygon-light NEE. The
     * former visible-emitter check could pass even if direct lighting returned
     * zero for every receiver. */
    SceneVertex receiver_vertices[6] = {0};
    receiver_vertices[0].position = (SceneWorldPoint){-160, 7680, 320};
    receiver_vertices[1].position = (SceneWorldPoint){160, 7680, 320};
    receiver_vertices[2].position = (SceneWorldPoint){160, -7680, 320};
    receiver_vertices[3] = receiver_vertices[0];
    receiver_vertices[4] = receiver_vertices[2];
    receiver_vertices[5].position = (SceneWorldPoint){-160, -7680, 320};

    SceneVertex emitter_vertices[3] = {0};
    emitter_vertices[0].position = (SceneWorldPoint){-80, -12800, 160};
    emitter_vertices[1].position = (SceneWorldPoint){80, -12800, 160};
    emitter_vertices[2].position = (SceneWorldPoint){0, -15360, 160};
    for (size_t vertex = 0u; vertex < 3u; ++vertex) {
        /* The cropped authored column contains a fully bright texel at V=13,
         * keeping every point on this reference emitter radiometric. */
        emitter_vertices[vertex].texture_v = 13;
    }

    SceneMeshSurface direct_surfaces[2] = {0};
    direct_surfaces[0].material.source =
        SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE;
    direct_surfaces[0].material.source_asset_id = 0u;
    direct_surfaces[0].geometry.vertices = receiver_vertices;
    direct_surfaces[0].geometry.vertex_count = 6u;
    direct_surfaces[0].geometry.topology =
        SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST;
    direct_surfaces[0].geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_WALL;
    direct_surfaces[0].geometry.texture_window.u_period = 64u;
    direct_surfaces[0].geometry.texture_window.v_period = 128u;

    direct_surfaces[1].material.source =
        SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE;
    direct_surfaces[1].material.source_asset_id = 6u;
    direct_surfaces[1].geometry.vertices = emitter_vertices;
    direct_surfaces[1].geometry.vertex_count = 3u;
    direct_surfaces[1].geometry.topology =
        SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST;
    direct_surfaces[1].geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_WALL;
    direct_surfaces[1].geometry.texture_window.u_offset = 156u;
    direct_surfaces[1].geometry.texture_window.u_period = 1u;
    direct_surfaces[1].geometry.texture_window.v_period = 128u;

    SceneCommand direct_commands[2] = {0};
    direct_commands[0].type = SCENE_COMMAND_CAMERA;
    direct_commands[1].type = SCENE_COMMAND_GEOMETRY_INSTANCE;
    direct_commands[1].data.geometry_instance.source_instance_id = 2u;
    direct_commands[1].data.geometry_instance.mesh.source_mesh_id = 2u;
    direct_commands[1].data.geometry_instance.mesh.acceleration_class =
        SCENE_ACCELERATION_CLASS_STATIC;
    direct_commands[1].data.geometry_instance.mesh.surfaces = direct_surfaces;
    direct_commands[1].data.geometry_instance.mesh.surface_count = 2u;
    SceneFrame direct_frame = {0};
    direct_frame.commands = direct_commands;
    direct_frame.count = 2u;
    for (int direct_frame_index = 0;
         direct_frame_index < DIRECT_REFERENCE_FRAME_COUNT;
         ++direct_frame_index) {
        if (!present_scene_frame(renderer, &direct_frame, &view, error,
                                 sizeof(error))) {
            renderer_rtx_destroy(renderer);
            SDL_Quit();
            return 1;
        }
        const size_t diffuse_coverage =
            renderer_rtx_last_direct_diffuse_coverage(renderer);
        const size_t specular_coverage =
            renderer_rtx_last_direct_specular_coverage(renderer);
        if (diffuse_coverage == 0u || specular_coverage == 0u) {
            fprintf(stderr,
                    "DXR direct-light reference missed a material lobe at "
                    "frame %d (diffuse=%zu specular=%zu)\n",
                    direct_frame_index, diffuse_coverage, specular_coverage);
            renderer_rtx_destroy(renderer);
            SDL_Quit();
            return 1;
        }
    }
    renderer_rtx_destroy(renderer);
    SDL_Quit();
    return 0;
}
