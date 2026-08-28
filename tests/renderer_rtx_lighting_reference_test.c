#include "renderer_rtx.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <stdint.h>
#include <stdio.h>

enum { REFERENCE_FRAME_COUNT = 4 };

#define REFERENCE_ROUGHNESS_BELOW_016 UINT32_C(0xF0000001)
#define REFERENCE_ROUGHNESS_020 UINT32_C(0xF0000002)
#define REFERENCE_ROUGHNESS_030 UINT32_C(0xF0000003)
#define REFERENCE_ROUGHNESS_100 UINT32_C(0xF0000004)
#define REFERENCE_METAL_ROUGHNESS_020 UINT32_C(0xF0000005)
#define REFERENCE_EMITTER UINT32_C(0xF0000006)
#define REFERENCE_METAL_BELOW_016 UINT32_C(0xF0000007)
#define REFERENCE_ADDITIVE UINT32_C(0xF0000100)

typedef struct {
    uint32_t material_id;
    const char *name;
    int expect_diffuse;
    int expect_specular;
} DirectMaterialCase;

static void initialize_wall_surface(SceneMeshSurface *surface,
                                    SceneVertex *vertices,
                                    uint32_t vertex_count,
                                    uint32_t material_id)
{
    *surface = (SceneMeshSurface){0};
    surface->material.source = SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE;
    surface->material.source_asset_id = material_id;
    surface->geometry.vertices = vertices;
    surface->geometry.vertex_count = vertex_count;
    surface->geometry.topology = SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST;
    surface->geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_WALL;
    surface->geometry.texture_window.u_period = 1u;
    surface->geometry.texture_window.v_period = 1u;
}

static int present_reference(RendererRtx *renderer, SceneFrame *frame,
                             RenderView *view, char *error,
                             size_t error_size)
{
    error[0] = '\0';
    if (!renderer_rtx_present(renderer, frame, view, error, error_size)) {
        fprintf(stderr, "DXR lighting reference present failed: %s\n", error);
        return 0;
    }
    const size_t invalid =
        renderer_rtx_last_invalid_lighting_or_guide_pixels(renderer);
    if (invalid != 0u) {
        fprintf(stderr,
                "DXR lighting reference produced %zu non-finite lighting/guide "
                "pixels\n",
                invalid);
        return 0;
    }
    return 1;
}

static int validate_direct_case(RendererRtx *renderer, SceneFrame *frame,
                                RenderView *view, SceneMeshSurface *receiver,
                                const DirectMaterialCase *test_case,
                                char *error, size_t error_size)
{
    receiver->material.source_asset_id = test_case->material_id;
    for (int frame_index = 0; frame_index < REFERENCE_FRAME_COUNT;
         ++frame_index) {
        if (!present_reference(renderer, frame, view, error, error_size)) {
            return 0;
        }
        const size_t diffuse =
            renderer_rtx_last_direct_diffuse_coverage(renderer);
        const size_t specular =
            renderer_rtx_last_direct_specular_coverage(renderer);
        if ((diffuse != 0u) != test_case->expect_diffuse ||
            (specular != 0u) != test_case->expect_specular ||
            renderer_rtx_last_frame_rgb_checksum(renderer) == UINT64_C(0)) {
            fprintf(stderr,
                    "DXR lighting case %s failed at frame %d "
                    "(diffuse=%zu specular=%zu checksum=%llu)\n",
                    test_case->name, frame_index, diffuse, specular,
                    (unsigned long long)
                        renderer_rtx_last_frame_rgb_checksum(renderer));
            return 0;
        }
    }
    return 1;
}

static int validate_transport_case(RendererRtx *renderer, SceneFrame *frame,
                                   RenderView *view, const char *name,
                                   int expect_direct_diffuse,
                                   int expect_direct_specular,
                                   int expect_smooth_specular,
                                   int expect_radiance, char *error,
                                   size_t error_size)
{
    for (int frame_index = 0; frame_index < REFERENCE_FRAME_COUNT;
         ++frame_index) {
        if (!present_reference(renderer, frame, view, error, error_size)) {
            return 0;
        }
        const size_t direct_diffuse =
            renderer_rtx_last_direct_diffuse_coverage(renderer);
        const size_t direct_specular =
            renderer_rtx_last_direct_specular_coverage(renderer);
        const size_t smooth_specular =
            renderer_rtx_last_smooth_specular_coverage(renderer);
        const uint64_t checksum =
            renderer_rtx_last_frame_rgb_checksum(renderer);
        if ((direct_diffuse != 0u) != expect_direct_diffuse ||
            (direct_specular != 0u) != expect_direct_specular ||
            (smooth_specular != 0u) != expect_smooth_specular ||
            (checksum != UINT64_C(0)) != expect_radiance) {
            fprintf(stderr,
                    "DXR transport case %s failed at frame %d "
                    "(direct=%zu/%zu smooth=%zu checksum=%llu)\n",
                    name, frame_index, direct_diffuse, direct_specular,
                    smooth_specular, (unsigned long long)checksum);
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    static const DirectMaterialCase material_cases[] = {
        {REFERENCE_ROUGHNESS_BELOW_016, "roughness-below-0.16", 1, 0},
        {REFERENCE_ROUGHNESS_020, "roughness-0.20", 1, 1},
        {REFERENCE_ROUGHNESS_030, "roughness-0.30", 1, 1},
        {REFERENCE_ROUGHNESS_100, "roughness-1.00", 1, 1},
        {REFERENCE_METAL_ROUGHNESS_020, "pure-metal-0.20", 0, 1},
    };
    static const char window_title[] = "AB3D2 DXR lighting reference test";
    char error[1024] = {0};
    RenderView view = {0};

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    RendererRayTracingOptions options = {0};
    options.samples_per_pixel = 8u;
    options.indirect_samples_per_pixel = 1u;
    options.diffuse_gi_scale = 0.0f;
    options.diffuse_gi_scale_set = UINT8_MAX;
    options.maximum_bounces = 2u;
    options.light_candidates = 16u;
    options.reservoir_sample_limit = 0u;
    options.reservoir_sample_limit_set = UINT8_MAX;
    options.radiance_clamp = 0.0f;
    options.exposure_bias_stops = -1.0f;
    options.exposure_bias_set = UINT8_MAX;
    options.ndf_trim = 0.9f;
    options.reconstruction = RENDERER_RAY_RECONSTRUCTION_OFF;
    options.output = RENDERER_OUTPUT_SDR;
    RendererRtx *renderer = renderer_rtx_create(
        640, 360, window_title, 0, 1, 1u, &options, error, sizeof(error));
    if (!renderer) {
        fprintf(stderr, "DXR lighting reference creation failed: %s\n", error);
        SDL_Quit();
        return 1;
    }

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

    SceneMeshSurface surfaces[3] = {0};
    initialize_wall_surface(&surfaces[0], receiver_vertices, 6u,
                            REFERENCE_ROUGHNESS_020);
    initialize_wall_surface(&surfaces[1], emitter_vertices, 3u,
                            REFERENCE_EMITTER);
    SceneCommand commands[3] = {0};
    commands[0].type = SCENE_COMMAND_CAMERA;
    commands[1].type = SCENE_COMMAND_GEOMETRY_INSTANCE;
    commands[1].data.geometry_instance.source_instance_id = 1u;
    commands[1].data.geometry_instance.mesh.source_mesh_id = 1u;
    commands[1].data.geometry_instance.mesh.acceleration_class =
        SCENE_ACCELERATION_CLASS_STATIC;
    commands[1].data.geometry_instance.mesh.surfaces = surfaces;
    commands[1].data.geometry_instance.mesh.surface_count = 1u;
    SceneFrame frame = {0};
    frame.commands = commands;
    frame.count = 2u;

    /* Geometry with a non-emissive PBR material and no emitter must remain
     * exact black; base colour is never an ambient or emission substitute. */
    if (!present_reference(renderer, &frame, &view, error, sizeof(error)) ||
        renderer_rtx_last_frame_rgb_checksum(renderer) != UINT64_C(0) ||
        renderer_rtx_last_direct_diffuse_coverage(renderer) != 0u ||
        renderer_rtx_last_direct_specular_coverage(renderer) != 0u) {
        fprintf(stderr,
                "DXR unlit material reference did not remain exact black\n");
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }

    commands[1].data.geometry_instance.mesh.surface_count = 2u;
    for (size_t case_index = 0u;
         case_index < sizeof(material_cases) / sizeof(material_cases[0]);
         ++case_index) {
        if (!validate_direct_case(renderer, &frame, &view, &surfaces[0],
                                  &material_cases[case_index], error,
                                  sizeof(error))) {
            renderer_rtx_destroy(renderer);
            SDL_Quit();
            return 1;
        }
    }

    /* With no geometry behind a sub-0.16 pure-metal receiver, its companion
     * GGX ray misses and every named radiance channel remains black. */
    surfaces[0].material.source_asset_id = REFERENCE_METAL_BELOW_016;
    commands[1].data.geometry_instance.mesh.surface_count = 1u;
    if (!validate_transport_case(renderer, &frame, &view, "specular-miss",
                                 0, 0, 0, 0, error, sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }

    /* This source additive bitmap is behind the camera, so primary visibility
     * never crosses it. The reflected segment accumulates its authored
     * radiance, continues through the non-occluding quad, and then misses. */
    commands[2].type = SCENE_COMMAND_SPRITE_INSTANCE;
    commands[2].data.sprite_instance.source_mesh_id = 2u;
    commands[2].data.sprite_instance.acceleration_class =
        SCENE_ACCELERATION_CLASS_DYNAMIC;
    commands[2].data.sprite_instance.sprite.position =
        (SceneWorldPoint){0, 0, -80};
    commands[2].data.sprite_instance.sprite.source =
        SCENE_SPRITE_SOURCE_OBJECT_BITMAP;
    commands[2].data.sprite_instance.sprite.presentation =
        SCENE_SPRITE_PRESENTATION_WORLD_OBJECT;
    commands[2].data.sprite_instance.sprite.surface_attachment =
        SCENE_SPRITE_SURFACE_FREE;
    commands[2].data.sprite_instance.sprite.source_asset_id =
        REFERENCE_ADDITIVE;
    commands[2].data.sprite_instance.sprite.source_record_id = 2u;
    commands[2].data.sprite_instance.sprite.source_width = 200u;
    commands[2].data.sprite_instance.sprite.source_height = 100u;
    commands[2].data.sprite_instance.sprite.flags =
        SCENE_SPRITE_FLAG_ADDITIVE;
    commands[2].data.sprite_instance.sprite.source_clip_top_y = -32768;
    commands[2].data.sprite_instance.sprite.source_clip_bottom_y = 32767;
    frame.count = 3u;
    if (!validate_transport_case(renderer, &frame, &view,
                                 "additive-on-reflected-miss", 0, 0, 1, 1,
                                 error, sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }
    frame.count = 2u;

    /* A large emitter behind and offset from the camera is invisible to the
     * primary ray. The smooth-GGX companion is the only path that can reach
     * it from the camera-facing metal receiver. */
    SceneVertex reflected_emitter_vertices[6] = {0};
    reflected_emitter_vertices[0].position =
        (SceneWorldPoint){-320, 23040, -160};
    reflected_emitter_vertices[1].position =
        (SceneWorldPoint){480, 23040, -160};
    reflected_emitter_vertices[2].position =
        (SceneWorldPoint){480, -23040, -160};
    reflected_emitter_vertices[3] = reflected_emitter_vertices[0];
    reflected_emitter_vertices[4] = reflected_emitter_vertices[2];
    reflected_emitter_vertices[5].position =
        (SceneWorldPoint){-320, -23040, -160};
    initialize_wall_surface(&surfaces[1], reflected_emitter_vertices, 6u,
                            REFERENCE_EMITTER);
    commands[1].data.geometry_instance.mesh.surface_count = 2u;
    if (!validate_transport_case(renderer, &frame, &view,
                                 "reflected-off-camera-emitter", 0, 0, 1, 1,
                                 error, sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }

    /* Replace the reflected source with a non-emissive wall. A separate
     * off-axis emitter sits between it and the camera but outside the mirror
     * segment, so the reached wall's local-light NEE supplies the reflection. */
    SceneVertex reflected_wall_vertices[6] = {0};
    reflected_wall_vertices[0].position =
        (SceneWorldPoint){-480, 23040, -160};
    reflected_wall_vertices[1].position =
        (SceneWorldPoint){480, 23040, -160};
    reflected_wall_vertices[2].position =
        (SceneWorldPoint){480, -23040, -160};
    reflected_wall_vertices[3] = reflected_wall_vertices[0];
    reflected_wall_vertices[4] = reflected_wall_vertices[2];
    reflected_wall_vertices[5].position =
        (SceneWorldPoint){-480, -23040, -160};
    SceneVertex reflected_light_vertices[3] = {0};
    reflected_light_vertices[0].position =
        (SceneWorldPoint){350, 7680, -80};
    reflected_light_vertices[1].position =
        (SceneWorldPoint){450, 7680, -80};
    reflected_light_vertices[2].position =
        (SceneWorldPoint){400, -7680, -80};
    initialize_wall_surface(&surfaces[1], reflected_wall_vertices, 6u,
                            REFERENCE_ROUGHNESS_100);
    initialize_wall_surface(&surfaces[2], reflected_light_vertices, 3u,
                            REFERENCE_EMITTER);
    commands[1].data.geometry_instance.mesh.surface_count = 3u;
    if (!validate_transport_case(renderer, &frame, &view,
                                 "reflected-locally-lit-wall", 0, 0, 1, 1,
                                 error, sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }

    /* Put the emitter far outside the primary frustum. A nearby off-screen
     * blocker covers its complete receiver-to-light cone, turning a proven
     * direct response back into exact black without affecting camera rays. */
    SceneVertex side_light_vertices[3] = {0};
    side_light_vertices[0].position =
        (SceneWorldPoint){480, 5120, 160};
    side_light_vertices[1].position =
        (SceneWorldPoint){640, 5120, 160};
    side_light_vertices[2].position =
        (SceneWorldPoint){560, -5120, 160};
    SceneVertex blocker_vertices[6] = {0};
    blocker_vertices[0].position = (SceneWorldPoint){380, 12800, 180};
    blocker_vertices[1].position = (SceneWorldPoint){620, 12800, 180};
    blocker_vertices[2].position = (SceneWorldPoint){620, -12800, 180};
    blocker_vertices[3] = blocker_vertices[0];
    blocker_vertices[4] = blocker_vertices[2];
    blocker_vertices[5].position = (SceneWorldPoint){380, -12800, 180};
    surfaces[0].material.source_asset_id = REFERENCE_ROUGHNESS_020;
    initialize_wall_surface(&surfaces[1], side_light_vertices, 3u,
                            REFERENCE_EMITTER);
    initialize_wall_surface(&surfaces[2], blocker_vertices, 6u,
                            REFERENCE_ROUGHNESS_100);
    commands[1].data.geometry_instance.mesh.surface_count = 2u;
    if (!validate_transport_case(renderer, &frame, &view,
                                 "unoccluded-off-screen-direct", 1, 1, 0, 1,
                                 error, sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }
    commands[1].data.geometry_instance.mesh.surface_count = 3u;
    if (!validate_transport_case(renderer, &frame, &view,
                                 "occluded-off-screen-direct", 0, 0, 0, 0,
                                 error, sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }

    renderer_rtx_destroy(renderer);
    SDL_Quit();
    return 0;
}
