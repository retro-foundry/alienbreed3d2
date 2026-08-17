#ifndef AB3D2_SCENE_RTX_VISIBILITY_H
#define AB3D2_SCENE_RTX_VISIBILITY_H

#include <stddef.h>
#include <stdint.h>

#include "asset_io.h"

enum {
    SCENE_RTX_VISIBILITY_FORMAT_VERSION = 1u,
    SCENE_RTX_VISIBILITY_MAX_CLUSTERS = 2047u
};

typedef struct {
    float normal[3];
    float distance;
} SceneRtxVisibilityPlane;

typedef struct {
    uint32_t plane_index;
    int32_t children[2];
} SceneRtxVisibilityNode;

typedef struct {
    int32_t contents;
    int32_t cluster;
} SceneRtxVisibilityLeaf;

typedef struct {
    float mins[3];
    float maxs[3];
} SceneRtxVisibilityClusterBounds;

/*
 * Immutable views into one validated .rtxvis asset.  The owning AssetBlob is
 * retained by GameBootstrap for the complete level lifetime.
 */
typedef struct SceneRtxVisibility {
    uint32_t format_version;
    uint32_t level_index;
    uint32_t plane_count;
    uint32_t node_count;
    uint32_t leaf_count;
    uint32_t cluster_count;
    uint32_t pvs_stride;
    float native_to_q2[12];
    const SceneRtxVisibilityPlane *planes;
    const SceneRtxVisibilityNode *nodes;
    const SceneRtxVisibilityLeaf *leaves;
    const SceneRtxVisibilityClusterBounds *cluster_bounds;
    const uint8_t *pvs;
} SceneRtxVisibility;

/* Validate a loaded sidecar and bind immutable views into sidecar_bytes. */
int scene_rtx_visibility_parse(const uint8_t *sidecar_bytes, size_t sidecar_size,
                               uint32_t level_index,
                               const uint8_t *level_data, size_t level_data_size,
                               const uint8_t *level_graphics,
                               size_t level_graphics_size,
                               SceneRtxVisibility *out_visibility,
                               char *error, size_t error_size);

/* Load data/rtx_visibility/level_a.rtxvis through level_p.rtxvis. */
int scene_rtx_visibility_load(const char *data_root, uint32_t level_index,
                              const uint8_t *level_data, size_t level_data_size,
                              const uint8_t *level_graphics,
                              size_t level_graphics_size,
                              AssetBlob *out_asset,
                              SceneRtxVisibility *out_visibility,
                              char *error, size_t error_size);

void scene_rtx_visibility_transform_point(const SceneRtxVisibility *visibility,
                                          const float native_point[3],
                                          float q2_point[3]);
int32_t scene_rtx_visibility_point_cluster(const SceneRtxVisibility *visibility,
                                           const float q2_point[3]);
int32_t scene_rtx_visibility_triangle_cluster(
    const SceneRtxVisibility *visibility, const float native_positions[9]);

#endif
