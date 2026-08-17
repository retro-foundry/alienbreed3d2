#include "renderer_dxr/dxr_scene_update.h"

#include <cstdio>

using ab3d2::dxr::DxrSceneGeometryHashes;
using ab3d2::dxr::DxrSceneUpdateKind;
using ab3d2::dxr::dxr_scene_classify_update;
using ab3d2::dxr::dxr_scene_geometry_hashes;

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

    surface.material.source_asset_id = 6u;
    const DxrSceneGeometryHashes material_changed =
        dxr_scene_geometry_hashes(frame);
    if (!expect(dxr_scene_classify_update(true, moved, material_changed),
                DxrSceneUpdateKind::rebuild, "material replacement")) {
        return 1;
    }

    surface.material.source_asset_id = 5u;
    surface.geometry.vertex_count = 0u;
    const DxrSceneGeometryHashes topology_changed =
        dxr_scene_geometry_hashes(frame);
    return expect(dxr_scene_classify_update(true, moved, topology_changed),
                  DxrSceneUpdateKind::rebuild, "topology replacement") ? 0 : 1;
}
