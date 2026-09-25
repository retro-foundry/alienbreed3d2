#include "renderer_dxr/dxr_scene_update.h"

#include <cstdio>

using ab3d2::dxr::DxrSceneGeometryHashes;
using ab3d2::dxr::DxrSceneUpdateKind;
using ab3d2::dxr::dxr_scene_classify_update;
using ab3d2::dxr::dxr_scene_geometry_hashes;
using ab3d2::dxr::dxr_vector_material_hash;

namespace {

bool expect(DxrSceneUpdateKind actual, DxrSceneUpdateKind expected,
            const char *case_name)
{
    if (actual == expected) {
        return true;
    }
    std::fprintf(stderr, "DXR scene update classification failed: %s\n",
                 case_name);
    return false;
}

}  // namespace

int main()
{
    SceneVertex vertices[3] = {};
    vertices[0].position = {-64, 0, 128};
    vertices[1].position = {64, 0, 128};
    vertices[2].position = {0, -128, 128};
    SceneMeshSurface surface = {};
    surface.material.source = SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE;
    surface.material.source_asset_id = 5u;
    surface.geometry.vertices = vertices;
    surface.geometry.vertex_count = 3u;
    surface.geometry.topology = SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST;
    surface.geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_WALL;
    surface.geometry.texture_window = {0u, 64u, 64u};
    SceneCommand command = {};
    command.type = SCENE_COMMAND_GEOMETRY_INSTANCE;
    command.data.geometry_instance.source_instance_id = 17u;
    command.data.geometry_instance.mesh.source_mesh_id = 19u;
    command.data.geometry_instance.mesh.acceleration_class =
        SCENE_ACCELERATION_CLASS_DYNAMIC;
    command.data.geometry_instance.mesh.surfaces = &surface;
    command.data.geometry_instance.mesh.surface_count = 1u;
    SceneFrame frame = {};
    frame.commands = &command;
    frame.count = 1u;

    const DxrSceneGeometryHashes initial = dxr_scene_geometry_hashes(frame);
    if (!expect(dxr_scene_classify_update(false, {}, initial),
                DxrSceneUpdateKind::rebuild, "first frame") ||
        !expect(dxr_scene_classify_update(true, initial, initial),
                DxrSceneUpdateKind::unchanged, "identical frame")) {
        return 1;
    }

    vertices[0].position.y += 32;
    const DxrSceneGeometryHashes moved = dxr_scene_geometry_hashes(frame);
    if (!expect(dxr_scene_classify_update(true, initial, moved),
                DxrSceneUpdateKind::geometry, "moving mechanism")) {
        return 1;
    }

    /* Source Gouraud brightness scales authored emission, so a brightness-only
     * frame rewrites vertices without moving anything. The scene zeroes the
     * light hash when rtx_emissive_animation is off, and then it is ignored. */
    vertices[1].source_light_level += 7;
    const DxrSceneGeometryHashes relit = dxr_scene_geometry_hashes(frame);
    DxrSceneGeometryHashes moved_unlit = moved;
    DxrSceneGeometryHashes relit_unlit = relit;
    moved_unlit.vertex_light = 0u;
    relit_unlit.vertex_light = 0u;
    if (relit.vertex_data != moved.vertex_data ||
        !expect(dxr_scene_classify_update(true, moved, relit),
                DxrSceneUpdateKind::geometry, "animated Gouraud brightness") ||
        !expect(dxr_scene_classify_update(true, relit, relit),
                DxrSceneUpdateKind::unchanged, "identical relit frame") ||
        !expect(dxr_scene_classify_update(true, moved_unlit, relit_unlit),
                DxrSceneUpdateKind::unchanged,
                "Gouraud brightness with emissive animation off")) {
        return 1;
    }

    surface.material.source_asset_id = 6u;
    const DxrSceneGeometryHashes material_changed =
        dxr_scene_geometry_hashes(frame);
    if (!expect(dxr_scene_classify_update(true, relit, material_changed),
                DxrSceneUpdateKind::rebuild, "material replacement")) {
        return 1;
    }

    surface.material.source_asset_id = 5u;
    surface.geometry.vertex_count = 0u;
    const DxrSceneGeometryHashes topology_changed =
        dxr_scene_geometry_hashes(frame);
    if (!expect(dxr_scene_classify_update(true, relit, topology_changed),
                DxrSceneUpdateKind::rebuild, "topology replacement")) {
        return 1;
    }

    /*
     * A vector model's animation frame - the player firing the shotgun, an
     * alien walking - selects different authored texture regions for the same
     * faces. That is vertex data, not scene layout: classifying it as layout
     * rebuilt every BLAS, the TLAS, the emitter table and the atlas roughly
     * every 80ms while firing, which is the stutter this guards against.
     */
    SourceVectorSceneMaterial weapon_materials[2] = {};
    weapon_materials[0].width = 32u;
    weapon_materials[0].height = 32u;
    weapon_materials[0].source_map_offset = 0x400u;
    weapon_materials[0].minimum_u = 0u;
    weapon_materials[0].maximum_u = 31u;
    weapon_materials[0].minimum_v = 0u;
    weapon_materials[0].maximum_v = 31u;
    weapon_materials[1] = weapon_materials[0];
    weapon_materials[1].source_map_offset = 0x800u;
    SourceVectorSceneTriangle weapon_triangles[1] = {};
    weapon_triangles[0].material_index = 0u;
    SourceVectorSceneMesh weapon = {};
    weapon.triangles = weapon_triangles;
    weapon.triangle_count = 1u;
    weapon.materials = weapon_materials;
    weapon.material_count = 2u;

    const uint64_t idle = dxr_vector_material_hash(0u, weapon, true);
    if (idle != dxr_vector_material_hash(0u, weapon, true)) {
        std::fprintf(stderr,
                     "DXR vector material hash is not deterministic\n");
        return 1;
    }

    /* The next animation frame points the same face at the other region. */
    weapon_triangles[0].material_index = 1u;
    const uint64_t fired = dxr_vector_material_hash(0u, weapon, true);
    if (idle == fired) {
        std::fprintf(stderr,
                     "DXR vector material hash ignored an animation frame\n");
        return 1;
    }

    /*
     * The scene layout is the model's structural identity, which an animation
     * step does not touch, so the step has to classify as a geometry update.
     */
    const DxrSceneGeometryHashes weapon_idle = {0x5eedu, idle};
    const DxrSceneGeometryHashes weapon_fired = {0x5eedu, fired};
    if (!expect(dxr_scene_classify_update(true, weapon_idle, weapon_fired),
                DxrSceneUpdateKind::geometry, "view weapon animation step") ||
        !expect(dxr_scene_classify_update(true, weapon_idle, weapon_idle),
                DxrSceneUpdateKind::unchanged, "held view weapon frame")) {
        return 1;
    }

    /* World vector objects take the same path with no material extent. */
    const uint64_t alien_idle = dxr_vector_material_hash(0u, weapon, false);
    weapon_triangles[0].material_index = 0u;
    const uint64_t alien_stepped = dxr_vector_material_hash(0u, weapon, false);
    if (alien_idle == alien_stepped) {
        std::fprintf(stderr,
                     "DXR world-vector material hash ignored an animation frame\n");
        return 1;
    }
    return expect(dxr_scene_classify_update(
                      true, {0xa11eu, alien_idle}, {0xa11eu, alien_stepped}),
                  DxrSceneUpdateKind::geometry, "world vector animation step") ?
        0 : 1;
}
