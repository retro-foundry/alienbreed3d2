#ifndef AB3D2_DXR_SCENE_UPDATE_H
#define AB3D2_DXR_SCENE_UPDATE_H

#include "scene_frame.h"

#include <cstdint>

namespace ab3d2::dxr {

struct DxrSceneGeometryHashes {
    uint64_t layout = 0;
    uint64_t vertex_data = 0;
};

enum class DxrSceneUpdateKind {
    unchanged,
    geometry,
    rebuild,
};

DxrSceneGeometryHashes dxr_scene_geometry_hashes(const SceneFrame &frame);
uint64_t dxr_scene_instance_vertex_hash(
    const SceneGeometryInstance &instance);
DxrSceneUpdateKind dxr_scene_classify_update(
    bool has_previous, const DxrSceneGeometryHashes &previous,
    const DxrSceneGeometryHashes &current);

}  // namespace ab3d2::dxr

#endif
