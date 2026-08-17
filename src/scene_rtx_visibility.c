#include "scene_rtx_visibility.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(SceneRtxVisibilityPlane) == 16u,
               "RTX visibility plane layout must match the sidecar");
_Static_assert(sizeof(SceneRtxVisibilityNode) == 12u,
               "RTX visibility node layout must match the sidecar");
_Static_assert(sizeof(SceneRtxVisibilityLeaf) == 8u,
               "RTX visibility leaf layout must match the sidecar");
_Static_assert(sizeof(SceneRtxVisibilityClusterBounds) == 24u,
               "RTX visibility bounds layout must match the sidecar");

enum {
    SCENE_RTX_VISIBILITY_HEADER_SIZE = 144u,
    SCENE_RTX_VISIBILITY_PLANE_SIZE = 16u,
    SCENE_RTX_VISIBILITY_NODE_SIZE = 12u,
    SCENE_RTX_VISIBILITY_LEAF_SIZE = 8u,
    SCENE_RTX_VISIBILITY_BOUNDS_SIZE = 24u
};

static void scene_rtx_visibility_set_error(char *error, size_t error_size,
                                           const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint32_t scene_rtx_visibility_read_u32(const uint8_t *source)
{
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8u) |
           ((uint32_t)source[2] << 16u) | ((uint32_t)source[3] << 24u);
}

static uint64_t scene_rtx_visibility_read_u64(const uint8_t *source)
{
    return (uint64_t)scene_rtx_visibility_read_u32(source) |
           ((uint64_t)scene_rtx_visibility_read_u32(source + 4u) << 32u);
}

static uint64_t scene_rtx_visibility_fnv1a64(const uint8_t *bytes, size_t size)
{
    uint64_t hash = UINT64_C(14695981039346656037);

    for (size_t index = 0u; index < size; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int scene_rtx_visibility_section(uint32_t offset, uint32_t count,
                                        uint32_t item_size, size_t file_size,
                                        size_t *out_end)
{
    size_t byte_count;

    if (count != 0u && item_size > SIZE_MAX / count) {
        return 0;
    }
    byte_count = (size_t)count * item_size;
    if ((size_t)offset > file_size || byte_count > file_size - offset) {
        return 0;
    }
    *out_end = (size_t)offset + byte_count;
    return 1;
}

static int scene_rtx_visibility_valid_bounds(
    const SceneRtxVisibilityClusterBounds *bounds)
{
    int empty = isinf(bounds->mins[0]) && bounds->mins[0] > 0.0f &&
                isinf(bounds->mins[1]) && bounds->mins[1] > 0.0f &&
                isinf(bounds->mins[2]) && bounds->mins[2] > 0.0f &&
                isinf(bounds->maxs[0]) && bounds->maxs[0] < 0.0f &&
                isinf(bounds->maxs[1]) && bounds->maxs[1] < 0.0f &&
                isinf(bounds->maxs[2]) && bounds->maxs[2] < 0.0f;

    if (empty) {
        return 1;
    }
    for (uint32_t axis = 0u; axis < 3u; ++axis) {
        if (!isfinite(bounds->mins[axis]) || !isfinite(bounds->maxs[axis]) ||
            bounds->mins[axis] > bounds->maxs[axis]) {
            return 0;
        }
    }
    return 1;
}

int scene_rtx_visibility_parse(const uint8_t *sidecar_bytes, size_t sidecar_size,
                               uint32_t level_index,
                               const uint8_t *level_data, size_t level_data_size,
                               const uint8_t *level_graphics,
                               size_t level_graphics_size,
                               SceneRtxVisibility *out_visibility,
                               char *error, size_t error_size)
{
    uint32_t format_version;
    uint32_t header_size;
    uint32_t stored_level_index;
    uint64_t stored_data_size;
    uint64_t stored_graphics_size;
    uint64_t stored_data_hash;
    uint64_t stored_graphics_hash;
    uint32_t plane_count;
    uint32_t node_count;
    uint32_t leaf_count;
    uint32_t cluster_count;
    uint32_t pvs_stride;
    uint32_t plane_offset;
    uint32_t node_offset;
    uint32_t leaf_offset;
    uint32_t bounds_offset;
    uint32_t pvs_offset;
    uint32_t stored_file_size;
    size_t section_end;
    size_t pvs_size;
    SceneRtxVisibility visibility;

    if (!out_visibility) {
        scene_rtx_visibility_set_error(error, error_size,
                                       "RTX visibility output pointer is null");
        return 0;
    }
    memset(out_visibility, 0, sizeof(*out_visibility));
    if (!sidecar_bytes || sidecar_size < SCENE_RTX_VISIBILITY_HEADER_SIZE ||
        !level_data || !level_graphics) {
        scene_rtx_visibility_set_error(error, error_size,
                                       "RTX visibility sidecar or level source is truncated");
        return 0;
    }
    if (memcmp(sidecar_bytes, "RTXV", 4u) != 0) {
        scene_rtx_visibility_set_error(error, error_size,
                                       "RTX visibility sidecar has an invalid magic");
        return 0;
    }
    format_version = scene_rtx_visibility_read_u32(sidecar_bytes + 4u);
    header_size = scene_rtx_visibility_read_u32(sidecar_bytes + 8u);
    stored_level_index = scene_rtx_visibility_read_u32(sidecar_bytes + 12u);
    stored_data_size = scene_rtx_visibility_read_u64(sidecar_bytes + 16u);
    stored_graphics_size = scene_rtx_visibility_read_u64(sidecar_bytes + 24u);
    stored_data_hash = scene_rtx_visibility_read_u64(sidecar_bytes + 32u);
    stored_graphics_hash = scene_rtx_visibility_read_u64(sidecar_bytes + 40u);
    plane_count = scene_rtx_visibility_read_u32(sidecar_bytes + 48u);
    node_count = scene_rtx_visibility_read_u32(sidecar_bytes + 52u);
    leaf_count = scene_rtx_visibility_read_u32(sidecar_bytes + 56u);
    cluster_count = scene_rtx_visibility_read_u32(sidecar_bytes + 60u);
    pvs_stride = scene_rtx_visibility_read_u32(sidecar_bytes + 64u);
    plane_offset = scene_rtx_visibility_read_u32(sidecar_bytes + 68u);
    node_offset = scene_rtx_visibility_read_u32(sidecar_bytes + 72u);
    leaf_offset = scene_rtx_visibility_read_u32(sidecar_bytes + 76u);
    bounds_offset = scene_rtx_visibility_read_u32(sidecar_bytes + 80u);
    pvs_offset = scene_rtx_visibility_read_u32(sidecar_bytes + 84u);
    stored_file_size = scene_rtx_visibility_read_u32(sidecar_bytes + 88u);

    if (format_version != SCENE_RTX_VISIBILITY_FORMAT_VERSION ||
        header_size != SCENE_RTX_VISIBILITY_HEADER_SIZE) {
        scene_rtx_visibility_set_error(error, error_size,
                                       "RTX visibility sidecar uses an unsupported format");
        return 0;
    }
    if (stored_level_index != level_index) {
        scene_rtx_visibility_set_error(error, error_size,
                                       "RTX visibility sidecar belongs to a different level");
        return 0;
    }
    if ((uint64_t)level_data_size != stored_data_size ||
        (uint64_t)level_graphics_size != stored_graphics_size ||
        scene_rtx_visibility_fnv1a64(level_data, level_data_size) != stored_data_hash ||
        scene_rtx_visibility_fnv1a64(level_graphics, level_graphics_size) !=
            stored_graphics_hash) {
        scene_rtx_visibility_set_error(error, error_size,
                                       "RTX visibility sidecar is stale for this level data");
        return 0;
    }
    if (stored_file_size != sidecar_size) {
        scene_rtx_visibility_set_error(error, error_size,
                                       "RTX visibility sidecar file length is inconsistent");
        return 0;
    }
    if (plane_count == 0u || node_count == 0u || leaf_count == 0u ||
        cluster_count == 0u || cluster_count > SCENE_RTX_VISIBILITY_MAX_CLUSTERS ||
        pvs_stride != (cluster_count + 7u) / 8u) {
        scene_rtx_visibility_set_error(error, error_size,
                                       "RTX visibility sidecar declares invalid counts");
        return 0;
    }
    if (plane_offset != header_size ||
        !scene_rtx_visibility_section(plane_offset, plane_count,
                                      SCENE_RTX_VISIBILITY_PLANE_SIZE,
                                      sidecar_size, &section_end) ||
        section_end != node_offset ||
        !scene_rtx_visibility_section(node_offset, node_count,
                                      SCENE_RTX_VISIBILITY_NODE_SIZE,
                                      sidecar_size, &section_end) ||
        section_end != leaf_offset ||
        !scene_rtx_visibility_section(leaf_offset, leaf_count,
                                      SCENE_RTX_VISIBILITY_LEAF_SIZE,
                                      sidecar_size, &section_end) ||
        section_end != bounds_offset ||
        !scene_rtx_visibility_section(bounds_offset, cluster_count,
                                      SCENE_RTX_VISIBILITY_BOUNDS_SIZE,
                                      sidecar_size, &section_end) ||
        section_end != pvs_offset) {
        scene_rtx_visibility_set_error(error, error_size,
                                       "RTX visibility sidecar sections are invalid");
        return 0;
    }
    if (cluster_count > SIZE_MAX / pvs_stride) {
        scene_rtx_visibility_set_error(error, error_size,
                                       "RTX visibility PVS size overflows the host");
        return 0;
    }
    pvs_size = (size_t)cluster_count * pvs_stride;
    if ((size_t)pvs_offset > sidecar_size ||
        pvs_size != sidecar_size - pvs_offset) {
        scene_rtx_visibility_set_error(error, error_size,
                                       "RTX visibility PVS matrix has an invalid length");
        return 0;
    }

    memset(&visibility, 0, sizeof(visibility));
    visibility.format_version = format_version;
    visibility.level_index = stored_level_index;
    visibility.plane_count = plane_count;
    visibility.node_count = node_count;
    visibility.leaf_count = leaf_count;
    visibility.cluster_count = cluster_count;
    visibility.pvs_stride = pvs_stride;
    memcpy(visibility.native_to_q2, sidecar_bytes + 96u,
           sizeof(visibility.native_to_q2));
    visibility.planes = (const SceneRtxVisibilityPlane *)(sidecar_bytes + plane_offset);
    visibility.nodes = (const SceneRtxVisibilityNode *)(sidecar_bytes + node_offset);
    visibility.leaves = (const SceneRtxVisibilityLeaf *)(sidecar_bytes + leaf_offset);
    visibility.cluster_bounds =
        (const SceneRtxVisibilityClusterBounds *)(sidecar_bytes + bounds_offset);
    visibility.pvs = sidecar_bytes + pvs_offset;

    for (uint32_t index = 0u; index < 12u; ++index) {
        if (!isfinite(visibility.native_to_q2[index])) {
            scene_rtx_visibility_set_error(error, error_size,
                                           "RTX visibility transform is not finite");
            return 0;
        }
    }
    for (uint32_t index = 0u; index < plane_count; ++index) {
        const SceneRtxVisibilityPlane *plane = visibility.planes + index;

        if (!isfinite(plane->normal[0]) || !isfinite(plane->normal[1]) ||
            !isfinite(plane->normal[2]) || !isfinite(plane->distance)) {
            scene_rtx_visibility_set_error(error, error_size,
                                           "RTX visibility plane is not finite");
            return 0;
        }
    }
    for (uint32_t index = 0u; index < node_count; ++index) {
        const SceneRtxVisibilityNode *node = visibility.nodes + index;

        if (node->plane_index >= plane_count) {
            scene_rtx_visibility_set_error(error, error_size,
                                           "RTX visibility node has an invalid plane");
            return 0;
        }
        for (uint32_t side = 0u; side < 2u; ++side) {
            int32_t child = node->children[side];

            if (child == INT32_MIN ||
                (child >= 0 && (uint32_t)child >= node_count) ||
                (child < 0 && (uint32_t)(-1 - child) >= leaf_count)) {
                scene_rtx_visibility_set_error(error, error_size,
                                               "RTX visibility node has an invalid child");
                return 0;
            }
        }
    }
    for (uint32_t index = 0u; index < leaf_count; ++index) {
        int32_t cluster = visibility.leaves[index].cluster;

        if (cluster < -1 || (cluster >= 0 && (uint32_t)cluster >= cluster_count)) {
            scene_rtx_visibility_set_error(error, error_size,
                                           "RTX visibility leaf has an invalid cluster");
            return 0;
        }
    }
    for (uint32_t index = 0u; index < cluster_count; ++index) {
        if (!scene_rtx_visibility_valid_bounds(visibility.cluster_bounds + index)) {
            scene_rtx_visibility_set_error(error, error_size,
                                           "RTX visibility cluster has invalid bounds");
            return 0;
        }
    }
    *out_visibility = visibility;
    return 1;
}

int scene_rtx_visibility_load(const char *data_root, uint32_t level_index,
                              const uint8_t *level_data, size_t level_data_size,
                              const uint8_t *level_graphics,
                              size_t level_graphics_size,
                              AssetBlob *out_asset,
                              SceneRtxVisibility *out_visibility,
                              char *error, size_t error_size)
{
    char relative_path[64];
    int written;

    if (!out_asset || !out_visibility || level_index >= 16u) {
        scene_rtx_visibility_set_error(error, error_size,
                                       "RTX visibility load arguments are invalid");
        return 0;
    }
    out_asset->bytes = NULL;
    out_asset->size = 0u;
    memset(out_visibility, 0, sizeof(*out_visibility));
    written = snprintf(relative_path, sizeof(relative_path),
                       "rtx_visibility/level_%c.rtxvis",
                       (char)('a' + level_index));
    if (written < 0 || (size_t)written >= sizeof(relative_path) ||
        !asset_io_load(data_root, relative_path, out_asset, error, error_size)) {
        return 0;
    }
    if (!scene_rtx_visibility_parse(out_asset->bytes, out_asset->size, level_index,
                                    level_data, level_data_size,
                                    level_graphics, level_graphics_size,
                                    out_visibility, error, error_size)) {
        asset_blob_release(out_asset);
        return 0;
    }
    return 1;
}

void scene_rtx_visibility_transform_point(const SceneRtxVisibility *visibility,
                                          const float native_point[3],
                                          float q2_point[3])
{
    for (uint32_t row = 0u; row < 3u; ++row) {
        q2_point[row] = visibility->native_to_q2[row * 4u + 0u] * native_point[0] +
                        visibility->native_to_q2[row * 4u + 1u] * native_point[1] +
                        visibility->native_to_q2[row * 4u + 2u] * native_point[2] +
                        visibility->native_to_q2[row * 4u + 3u];
    }
}

int32_t scene_rtx_visibility_point_cluster(const SceneRtxVisibility *visibility,
                                           const float q2_point[3])
{
    int32_t node_index = 0;
    uint32_t visits = 0u;

    if (!visibility || !q2_point || visibility->node_count == 0u) {
        return -1;
    }
    while (node_index >= 0) {
        const SceneRtxVisibilityNode *node;
        const SceneRtxVisibilityPlane *plane;
        float side;

        if ((uint32_t)node_index >= visibility->node_count ||
            visits++ >= visibility->node_count) {
            return -1;
        }
        node = visibility->nodes + node_index;
        plane = visibility->planes + node->plane_index;
        side = q2_point[0] * plane->normal[0] +
               q2_point[1] * plane->normal[1] +
               q2_point[2] * plane->normal[2] - plane->distance;
        node_index = node->children[side < 0.0f ? 1u : 0u];
    }
    {
        uint32_t leaf_index = (uint32_t)(-1 - node_index);
        int32_t cluster;

        if (leaf_index >= visibility->leaf_count) {
            return -1;
        }
        cluster = visibility->leaves[leaf_index].cluster;
        return cluster >= 0 && (uint32_t)cluster < visibility->cluster_count ?
            cluster : -1;
    }
}

static int scene_rtx_visibility_off_center(const float positions[9], float offset,
                                           float center[3])
{
    float edge_a[3];
    float edge_b[3];
    float normal[3];
    float length;

    for (uint32_t axis = 0u; axis < 3u; ++axis) {
        center[axis] = (positions[axis] + positions[3u + axis] +
                        positions[6u + axis]) / 3.0f;
        edge_a[axis] = positions[3u + axis] - positions[axis];
        edge_b[axis] = positions[6u + axis] - positions[axis];
    }
    normal[0] = edge_a[1] * edge_b[2] - edge_a[2] * edge_b[1];
    normal[1] = edge_a[2] * edge_b[0] - edge_a[0] * edge_b[2];
    normal[2] = edge_a[0] * edge_b[1] - edge_a[1] * edge_b[0];
    length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] +
                   normal[2] * normal[2]);
    if (!(length > 0.0f)) {
        return 0;
    }
    for (uint32_t axis = 0u; axis < 3u; ++axis) {
        center[axis] += normal[axis] * (offset / length);
    }
    return 1;
}

int32_t scene_rtx_visibility_triangle_cluster(
    const SceneRtxVisibility *visibility, const float native_positions[9])
{
    float q2_positions[9];

    if (!visibility || !native_positions) {
        return -1;
    }
    for (uint32_t vertex = 0u; vertex < 3u; ++vertex) {
        scene_rtx_visibility_transform_point(visibility,
                                             native_positions + vertex * 3u,
                                             q2_positions + vertex * 3u);
    }
    /* Q2RTX bsp_mesh.c retries exactly these two offsets. */
    for (uint32_t attempt = 0u; attempt < 2u; ++attempt) {
        float center[3];
        float offset = attempt == 0u ? 0.01f : 1.0f;
        int32_t cluster;

        if (!scene_rtx_visibility_off_center(q2_positions, offset, center)) {
            return -1;
        }
        cluster = scene_rtx_visibility_point_cluster(visibility, center);
        if (cluster >= 0) {
            return cluster;
        }
    }
    return -1;
}
