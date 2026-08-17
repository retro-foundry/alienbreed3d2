#include "dxr_scene_update.h"

#include <cstddef>

namespace ab3d2::dxr {

namespace {

constexpr uint64_t fnv_offset = UINT64_C(1469598103934665603);
constexpr uint64_t fnv_prime = UINT64_C(1099511628211);

uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= fnv_prime;
    }
    return hash;
}

uint64_t hash_instance_vertices(uint64_t hash,
                                const SceneGeometryInstance &instance)
{
    const SceneMesh &mesh = instance.mesh;
    for (uint32_t surface_index = 0;
         mesh.surfaces && surface_index < mesh.surface_count; ++surface_index) {
        const SceneGeometry &geometry = mesh.surfaces[surface_index].geometry;
        for (uint32_t vertex_index = 0;
             geometry.vertices && vertex_index < geometry.vertex_count;
             ++vertex_index) {
            const SceneVertex &vertex = geometry.vertices[vertex_index];
            hash = hash_bytes(hash, &vertex.position, sizeof(vertex.position));
            hash = hash_bytes(hash, &vertex.texture_u, sizeof(vertex.texture_u));
            hash = hash_bytes(hash, &vertex.texture_v, sizeof(vertex.texture_v));
        }
    }
    return hash;
}

}  // namespace

DxrSceneGeometryHashes dxr_scene_geometry_hashes(const SceneFrame &frame)
{
    DxrSceneGeometryHashes hashes = {fnv_offset, fnv_offset};
    for (size_t command_index = 0; command_index < frame.count;
         ++command_index) {
        const SceneCommand &command = frame.commands[command_index];
        if (command.type != SCENE_COMMAND_GEOMETRY_INSTANCE) {
            continue;
        }
        const SceneGeometryInstance &instance = command.data.geometry_instance;
        hashes.layout = hash_bytes(hashes.layout, &instance.source_instance_id,
                                   sizeof(instance.source_instance_id));
        hashes.layout = hash_bytes(hashes.layout, &instance.mesh.source_mesh_id,
                                   sizeof(instance.mesh.source_mesh_id));
        hashes.layout = hash_bytes(hashes.layout,
                                   &instance.mesh.acceleration_class,
                                   sizeof(instance.mesh.acceleration_class));
        hashes.layout = hash_bytes(hashes.layout, &instance.mesh.surface_count,
                                   sizeof(instance.mesh.surface_count));
        for (uint32_t surface_index = 0;
             instance.mesh.surfaces &&
             surface_index < instance.mesh.surface_count; ++surface_index) {
            const SceneMeshSurface &surface =
                instance.mesh.surfaces[surface_index];
            hashes.layout = hash_bytes(hashes.layout, &surface.material.source,
                                       sizeof(surface.material.source));
            hashes.layout = hash_bytes(
                hashes.layout, &surface.material.source_asset_id,
                sizeof(surface.material.source_asset_id));
            hashes.layout = hash_bytes(
                hashes.layout, &surface.geometry.vertex_count,
                sizeof(surface.geometry.vertex_count));
            hashes.layout = hash_bytes(hashes.layout, &surface.geometry.topology,
                                       sizeof(surface.geometry.topology));
            hashes.layout = hash_bytes(hashes.layout, &surface.geometry.primitive,
                                       sizeof(surface.geometry.primitive));
            hashes.layout = hash_bytes(
                hashes.layout, &surface.geometry.texture_window,
                sizeof(surface.geometry.texture_window));
        }
        hashes.vertex_data =
            hash_instance_vertices(hashes.vertex_data, instance);
    }
    return hashes;
}

uint64_t dxr_scene_instance_vertex_hash(
    const SceneGeometryInstance &instance)
{
    return hash_instance_vertices(fnv_offset, instance);
}

DxrSceneUpdateKind dxr_scene_classify_update(
    bool has_previous, const DxrSceneGeometryHashes &previous,
    const DxrSceneGeometryHashes &current)
{
    if (!has_previous || previous.layout != current.layout) {
        return DxrSceneUpdateKind::rebuild;
    }
    return previous.vertex_data == current.vertex_data ?
        DxrSceneUpdateKind::unchanged : DxrSceneUpdateKind::geometry;
}

}  // namespace ab3d2::dxr
