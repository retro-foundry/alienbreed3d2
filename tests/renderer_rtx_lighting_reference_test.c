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
    options.maximum_bounces = 1u;
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

    SceneMeshSurface surfaces[2] = {0};
    initialize_wall_surface(&surfaces[0], receiver_vertices, 6u,
                            REFERENCE_ROUGHNESS_020);
    initialize_wall_surface(&surfaces[1], emitter_vertices, 3u,
                            REFERENCE_EMITTER);
    SceneCommand commands[2] = {0};
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

    renderer_rtx_destroy(renderer);
    SDL_Quit();
    return 0;
}
