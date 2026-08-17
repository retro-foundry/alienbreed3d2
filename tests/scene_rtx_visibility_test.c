#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_io.h"
#include "scene_rtx_visibility.h"

static void write_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8u);
    destination[2] = (uint8_t)(value >> 16u);
    destination[3] = (uint8_t)(value >> 24u);
}

static int load_level_asset(const char *data_root, uint32_t level_index,
                            const char *name, AssetBlob *asset,
                            char *error, size_t error_size)
{
    char relative_path[96];
    int written = snprintf(relative_path, sizeof(relative_path),
                           "levels/level_%c/%s", (char)('a' + level_index), name);

    return written >= 0 && (size_t)written < sizeof(relative_path) &&
           asset_io_load(data_root, relative_path, asset, error, error_size);
}

int main(int argc, char **argv)
{
    static const uint32_t expected_clusters[16] = {
        304u, 366u, 233u, 74u, 361u, 43u, 1u, 521u,
        1428u, 12u, 244u, 304u, 1815u, 305u, 1167u, 928u
    };
    char error[256];
    AssetBlob level_data = {0};
    AssetBlob level_graphics = {0};
    AssetBlob sidecar = {0};
    SceneRtxVisibility parsed;
    uint8_t *mutation = NULL;
    int result = 1;

    if (argc != 2) {
        (void)fprintf(stderr, "usage: %s <staged-data-root>\n", argv[0]);
        return 2;
    }
    for (uint32_t level = 0u; level < 16u; ++level) {
        if (!load_level_asset(argv[1], level, "twolev.bin", &level_data,
                              error, sizeof(error)) ||
            !load_level_asset(argv[1], level, "twolev.graph.bin", &level_graphics,
                              error, sizeof(error)) ||
            !scene_rtx_visibility_load(argv[1], level,
                                       level_data.bytes, level_data.size,
                                       level_graphics.bytes, level_graphics.size,
                                       &sidecar, &parsed, error, sizeof(error))) {
            (void)fprintf(stderr, "level %c visibility failed: %s\n",
                          (char)('A' + level), error);
            result = 0;
            goto cleanup;
        }
        if (parsed.cluster_count != expected_clusters[level] ||
            parsed.pvs_stride != (expected_clusters[level] + 7u) / 8u ||
            parsed.level_index != level) {
            (void)fprintf(stderr, "level %c visibility golden mismatch\n",
                          (char)('A' + level));
            result = 0;
            goto cleanup;
        }
        asset_blob_release(&sidecar);
        asset_blob_release(&level_graphics);
        asset_blob_release(&level_data);
    }

    if (!load_level_asset(argv[1], 0u, "twolev.bin", &level_data,
                          error, sizeof(error)) ||
        !load_level_asset(argv[1], 0u, "twolev.graph.bin", &level_graphics,
                          error, sizeof(error)) ||
        !scene_rtx_visibility_load(argv[1], 0u,
                                   level_data.bytes, level_data.size,
                                   level_graphics.bytes, level_graphics.size,
                                   &sidecar, &parsed, error, sizeof(error))) {
        (void)fprintf(stderr, "level A mutation fixture failed: %s\n", error);
        result = 0;
        goto cleanup;
    }
    mutation = malloc(sidecar.size);
    if (!mutation) {
        result = 0;
        goto cleanup;
    }
    memcpy(mutation, sidecar.bytes, sidecar.size);
    if (scene_rtx_visibility_parse(mutation, sidecar.size - 1u, 0u,
                                   level_data.bytes, level_data.size,
                                   level_graphics.bytes, level_graphics.size,
                                   &parsed, error, sizeof(error))) {
        (void)fprintf(stderr, "truncated sidecar was accepted\n");
        result = 0;
        goto cleanup;
    }
    memcpy(mutation, sidecar.bytes, sidecar.size);
    write_u32(mutation + 60u, 2048u);
    if (scene_rtx_visibility_parse(mutation, sidecar.size, 0u,
                                   level_data.bytes, level_data.size,
                                   level_graphics.bytes, level_graphics.size,
                                   &parsed, error, sizeof(error))) {
        (void)fprintf(stderr, "cluster overflow was accepted\n");
        result = 0;
        goto cleanup;
    }
    memcpy(mutation, sidecar.bytes, sidecar.size);
    mutation[68u] ^= 4u;
    if (scene_rtx_visibility_parse(mutation, sidecar.size, 0u,
                                   level_data.bytes, level_data.size,
                                   level_graphics.bytes, level_graphics.size,
                                   &parsed, error, sizeof(error))) {
        (void)fprintf(stderr, "corrupt section offset was accepted\n");
        result = 0;
        goto cleanup;
    }
    level_data.bytes[0] ^= 1u;
    if (scene_rtx_visibility_parse(sidecar.bytes, sidecar.size, 0u,
                                   level_data.bytes, level_data.size,
                                   level_graphics.bytes, level_graphics.size,
                                   &parsed, error, sizeof(error))) {
        (void)fprintf(stderr, "stale source hash was accepted\n");
        result = 0;
        goto cleanup;
    }
    level_data.bytes[0] ^= 1u;

cleanup:
    free(mutation);
    asset_blob_release(&sidecar);
    asset_blob_release(&level_graphics);
    asset_blob_release(&level_data);
    return result ? 0 : 1;
}
