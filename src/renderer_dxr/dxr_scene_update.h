#ifndef AB3D2_DXR_SCENE_UPDATE_H
#define AB3D2_DXR_SCENE_UPDATE_H

#include "scene_frame.h"
#include "source_vector_model_scene.h"

#include <cstdint>

namespace ab3d2::dxr {

struct DxrSceneGeometryHashes {
    uint64_t layout = 0;
    uint64_t vertex_data = 0;
    /*
     * Source Gouraud brightness, kept apart from `vertex_data` because it never
     * moves a triangle. With rtx_emissive_animation on it scales authored
     * emission, so a newanims.s:brightanim step must rewrite the vertex buffer
     * without refitting any BLAS. The scene holds it at zero when off.
     */
    uint64_t vertex_light = 0;
};

enum class DxrSceneUpdateKind {
    unchanged,
    geometry,
    rebuild,
};

DxrSceneGeometryHashes dxr_scene_geometry_hashes(const SceneFrame &frame);
uint64_t dxr_scene_instance_vertex_hash(
    const SceneGeometryInstance &instance);
/*
 * The part of a compiled vector model that its animation frame changes: the
 * authored texture regions its faces name, and which face names which region.
 *
 * This belongs to the vertex hash, never the scene layout hash. A vector
 * model's frame selects different regions, so hashing it as layout made every
 * animation step - firing a weapon, an alien walking - a full scene rebuild.
 * Callers hash the structural identity (asset, record, triangle count)
 * into the layout hash separately; that survives an animation step.
 *
 * include_material_extent covers the view weapon, which also hashes each
 * region's pixel extent; world vector objects do not.
 */
uint64_t dxr_vector_material_hash(uint64_t seed,
                                  const SourceVectorSceneMesh &mesh,
                                  bool include_material_extent);

DxrSceneUpdateKind dxr_scene_classify_update(
    bool has_previous, const DxrSceneGeometryHashes &previous,
    const DxrSceneGeometryHashes &current);

}  // namespace ab3d2::dxr

#endif
