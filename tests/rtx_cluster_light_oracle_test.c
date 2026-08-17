#define SDL_MAIN_HANDLED
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_bootstrap.h"
#include "renderer_vulkan_rtx_lights.h"
#include "renderer_vulkan_rtx_materials.h"
#include "renderer_vulkan_rtx_scene.h"

enum {
    TEST_MATERIAL_PAGE_WIDTH = 2568,
    TEST_MATERIAL_PAGE_HEIGHT = 512,
    TEST_TECHNOLIGHTS_LAYER = 6
};

static int load_emissive_region(const char *material_root, uint32_t layer,
                                uint32_t width, uint32_t height,
                                RendererVulkanRtxEmissiveInfo *out_info,
                                char *error, size_t error_size)
{
    char path[1024];
    FILE *file = NULL;
    uint8_t *pixels = NULL;
    size_t row_size = (size_t)width * 4u;
    long expected_size = (long)((size_t)TEST_MATERIAL_PAGE_WIDTH *
        TEST_MATERIAL_PAGE_HEIGHT * 4u);
    int written = snprintf(path, sizeof(path), "%s/%02u_emissive.rgba",
                           material_root, (unsigned)layer);
    int success = 0;

    if (written < 0 || (size_t)written >= sizeof(path) ||
        width == 0u || height == 0u ||
        width > TEST_MATERIAL_PAGE_WIDTH ||
        height > TEST_MATERIAL_PAGE_HEIGHT ||
        !(pixels = malloc(row_size * height)) ||
        !(file = fopen(path, "rb"))) {
        (void)snprintf(error, error_size,
                       "unable to open RTX emissive oracle %s", path);
        goto done;
    }
    if (fseek(file, 0, SEEK_END) != 0 || ftell(file) != expected_size ||
        fseek(file, 0, SEEK_SET) != 0) {
        (void)snprintf(error, error_size,
                       "RTX emissive oracle has the wrong size: %s", path);
        goto done;
    }
    for (uint32_t row = 0u; row < height; ++row) {
        if (fread(pixels + (size_t)row * row_size, 1u, row_size, file) !=
                row_size ||
            (row + 1u < height &&
             fseek(file,
                   (long)((TEST_MATERIAL_PAGE_WIDTH - width) * 4u),
                   SEEK_CUR) != 0)) {
            (void)snprintf(error, error_size,
                           "RTX emissive oracle is truncated: %s", path);
            goto done;
        }
    }
    renderer_vulkan_rtx_extract_emissive_info(
        pixels, width, height, out_info);
    if (!out_info->valid) {
        (void)snprintf(error, error_size,
                       "RTX emissive oracle is empty: %s", path);
        goto done;
    }
    success = 1;

done:
    if (file) fclose(file);
    free(pixels);
    return success;
}

static uint32_t cluster_light_count(
    const RendererVulkanRtxLightUpload *upload, uint32_t cluster)
{
    size_t list_base = RENDERER_VULKAN_RTX_LIGHT_HEADER_WORDS +
        (size_t)upload->light_count * RENDERER_VULKAN_RTX_LIGHT_WORDS;

    return upload->words[list_base + cluster + 1u] -
        upload->words[list_base + cluster];
}

int main(int argc, char **argv)
{
    static const struct {
        uint32_t cluster;
        uint32_t expected_count;
    } oracle[] = {
        {255u, 0u}, {257u, 0u}, {258u, 0u}, {259u, 0u}, {267u, 4u}
    };
    GameBootstrap *game = NULL;
    SceneFrame frame = {0};
    RendererVulkanRtxCpuScene scene = {0};
    RendererVulkanRtxMaterialGpu materials[
        RENDERER_VULKAN_RTX_MATERIAL_LAYER_COUNT] = {0};
    RendererVulkanRtxEmissiveInfo emissive_info[
        RENDERER_VULKAN_RTX_MATERIAL_LAYER_COUNT] = {0};
    RendererVulkanRtxLightHistory history = {0};
    RendererVulkanRtxLightUpload upload = {0};
    const SceneLighting *lighting = NULL;
    uint32_t cluster_267_floor_triangle_count = 0u;
    char error[512] = {0};
    int success = 0;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <data-root> <rtx-material-root>\n", argv[0]);
        return 2;
    }
    game = calloc(1u, sizeof(*game));
    if (!game || !scene_frame_init(&frame, 1024u) ||
        !game_bootstrap_init(game, argv[1], error, sizeof(error))) {
        fprintf(stderr, "RTX Level A oracle setup failed: %s\n", error);
        goto done;
    }
    game_bootstrap_set_rtx_visibility_required(game, 1);
    if (!game_session_select_level(&game->session, 0u, error, sizeof(error)) ||
        !game_bootstrap_start_selected_single_player(
            game, argv[1], error, sizeof(error))) {
        fprintf(stderr, "RTX Level A oracle load failed: %s\n", error);
        goto done;
    }
    if (game->rtx_visibility.cluster_count != 304u) {
        fprintf(stderr, "Level A has %u RTX clusters instead of 304\n",
                (unsigned)game->rtx_visibility.cluster_count);
        goto done;
    }
    scene_frame_begin(&frame);
    if (!game_bootstrap_submit_scene_frame(game, &frame) ||
        !renderer_vulkan_rtx_flatten_world(
            &frame, 1u, &scene, error, sizeof(error))) {
        fprintf(stderr, "RTX Level A strict scene failed: %s\n", error);
        goto done;
    }
    for (size_t index = 0u; index < frame.count; ++index) {
        if (frame.commands[index].type == SCENE_COMMAND_LIGHTING) {
            lighting = &frame.commands[index].data.lighting;
            break;
        }
    }
    if (!lighting || lighting->rtx_visibility != &game->rtx_visibility) {
        fprintf(stderr, "Level A scene omitted required RTX visibility\n");
        goto done;
    }
    for (uint32_t base = 0u; base + 2u < scene.vertex_count; base += 3u) {
        if (renderer_vulkan_rtx_visibility_cluster_from_flags(
                scene.vertices[base].flags) == 267 &&
            (scene.vertices[base].flags & UINT32_C(0xff)) ==
                SCENE_GEOMETRY_PRIMITIVE_FLOOR) {
            ++cluster_267_floor_triangle_count;
        }
    }
    if (cluster_267_floor_triangle_count == 0u) {
        fprintf(stderr, "Level A cluster 267 has no native floor triangles\n");
        goto done;
    }
    materials[TEST_TECHNOLIGHTS_LAYER].flags =
        RENDERER_VULKAN_RTX_MATERIAL_FLAG_LIGHT;
    materials[TEST_TECHNOLIGHTS_LAYER].emissive_factor = 200.0f;
    if (!load_emissive_region(
            argv[2], TEST_TECHNOLIGHTS_LAYER, 1032u, 512u,
            &emissive_info[TEST_TECHNOLIGHTS_LAYER], error, sizeof(error)) ||
        !renderer_vulkan_rtx_lights_build(
            &scene, materials, emissive_info, lighting, &history, &upload,
            error, sizeof(error))) {
        fprintf(stderr, "RTX Level A light-list build failed: %s\n", error);
        goto done;
    }
    fprintf(stdout,
            "Level A native light build: emitters=%u pairs=%u culled=%u\n",
            (unsigned)upload.light_count,
            (unsigned)upload.list_index_count,
            (unsigned)upload.culled_cluster_light_pairs);
    fprintf(stdout, "Level A cluster 267 native floor triangles: %u\n",
            (unsigned)cluster_267_floor_triangle_count);
    for (size_t index = 0u; index < sizeof(oracle) / sizeof(oracle[0]); ++index) {
        uint32_t actual = cluster_light_count(&upload, oracle[index].cluster);
        fprintf(stdout, "Level A cluster %u polygon lights: %u\n",
                (unsigned)oracle[index].cluster, (unsigned)actual);
    }
    for (size_t index = 0u; index < sizeof(oracle) / sizeof(oracle[0]); ++index) {
        uint32_t actual = cluster_light_count(&upload, oracle[index].cluster);

        if (actual != oracle[index].expected_count) {
            fprintf(stderr,
                    "Level A cluster %u has %u polygon lights instead of %u\n",
                    (unsigned)oracle[index].cluster, (unsigned)actual,
                    (unsigned)oracle[index].expected_count);
            goto done;
        }
    }
    fprintf(stdout,
            "Level A RTX oracle: clusters=304 emitters=%u pairs=%u "
            "[255=0 257=0 258=0 259=0 267=4]\n",
            (unsigned)upload.light_count,
            (unsigned)upload.list_index_count);
    success = 1;

done:
    renderer_vulkan_rtx_lights_release(&upload);
    renderer_vulkan_rtx_light_history_release(&history);
    renderer_vulkan_rtx_cpu_scene_destroy(&scene);
    scene_frame_destroy(&frame);
    if (game) game_bootstrap_destroy(game);
    free(game);
    return success ? 0 : 1;
}
