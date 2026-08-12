#include <math.h>
#include <stdio.h>
#include <string.h>

#include "world_light_tessellation.h"

static SceneVertex make_vertex(int32_t x, int32_t y, int32_t z,
                               int32_t texture_u, int32_t texture_v,
                               int16_t light)
{
    SceneVertex vertex;

    memset(&vertex, 0, sizeof(vertex));
    vertex.position.x = x;
    vertex.position.y = y;
    vertex.position.z = z;
    vertex.texture_u = texture_u;
    vertex.texture_v = texture_v;
    vertex.source_light_level = light;
    return vertex;
}

static int near_value(float first, float second)
{
    return fabsf(first - second) <= 0.001f;
}

static int mesh_has_vertex(const WorldLightTessellationMesh *mesh,
                           float x, float y, float z, float light)
{
    for (uint32_t index = 0u; index < mesh->vertex_count; ++index) {
        const WorldLightTessellationVertex *vertex = &mesh->vertices[index];

        if (near_value(vertex->position_x, x) && near_value(vertex->position_y, y) &&
            near_value(vertex->position_z, z) && near_value(vertex->source_light_level, light)) {
            return 1;
        }
    }
    return 0;
}

static int mesh_is_bounded_and_seamless(const WorldLightTessellationMesh *mesh,
                                        float minimum_light, float maximum_light)
{
    for (uint32_t first_index = 0u; first_index < mesh->vertex_count; ++first_index) {
        const WorldLightTessellationVertex *first = &mesh->vertices[first_index];

        if (first->source_light_level < minimum_light - 0.001f ||
            first->source_light_level > maximum_light + 0.001f) {
            return 0;
        }
        for (uint32_t second_index = first_index + 1u;
             second_index < mesh->vertex_count; ++second_index) {
            const WorldLightTessellationVertex *second = &mesh->vertices[second_index];

            if (near_value(first->position_x, second->position_x) &&
                near_value(first->position_y, second->position_y) &&
                near_value(first->position_z, second->position_z) &&
                !near_value(first->source_light_level, second->source_light_level)) {
                return 0;
            }
        }
    }
    return 1;
}

static int test_wall(void)
{
    SceneVertex vertices[6];
    SceneGeometry geometry;
    WorldLightTessellationMesh mesh = {0};
    char error[256] = {0};

    vertices[0] = make_vertex(0, 0, 0, 0, 0, 100);
    vertices[1] = make_vertex(8, 0, 0, 8, 0, 200);
    vertices[2] = make_vertex(8, 8, 0, 8, 8, 300);
    vertices[3] = vertices[0];
    vertices[4] = vertices[2];
    vertices[5] = make_vertex(0, 8, 0, 0, 8, 400);
    memset(&geometry, 0, sizeof(geometry));
    geometry.vertices = vertices;
    geometry.vertex_count = 6u;
    geometry.topology = SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST;
    geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_WALL;

    if (!world_light_tessellate(&geometry, 1u, &mesh, error, sizeof(error)) ||
        mesh.vertex_count != 6u ||
        !mesh_has_vertex(&mesh, 0.0f, 0.0f, 0.0f, 100.0f) ||
        !mesh_has_vertex(&mesh, 8.0f, 0.0f, 0.0f, 200.0f) ||
        !mesh_has_vertex(&mesh, 8.0f, 8.0f, 0.0f, 300.0f) ||
        !mesh_has_vertex(&mesh, 0.0f, 8.0f, 0.0f, 400.0f)) {
        fprintf(stderr, "1x wall tessellation did not preserve the source quad: %s\n", error);
        world_light_tessellation_mesh_release(&mesh);
        return 0;
    }
    world_light_tessellation_mesh_release(&mesh);
    if (!world_light_tessellate(&geometry, 4u, &mesh, error, sizeof(error)) ||
        mesh.vertex_count != 96u ||
        !mesh_has_vertex(&mesh, 2.0f, 0.0f, 0.0f, 115.625f) ||
        !mesh_has_vertex(&mesh, 4.0f, 4.0f, 0.0f, 250.0f) ||
        !mesh_is_bounded_and_seamless(&mesh, 100.0f, 400.0f)) {
        fprintf(stderr, "4x wall tessellation is not smooth, bounded, or seamless: %s\n", error);
        world_light_tessellation_mesh_release(&mesh);
        return 0;
    }
    world_light_tessellation_mesh_release(&mesh);
    return 1;
}

static int test_polygon(void)
{
    SceneVertex vertices[4];
    SceneGeometry geometry;
    WorldLightTessellationMesh mesh = {0};
    char error[256] = {0};

    vertices[0] = make_vertex(0, 16, 0, 0, 0, 100);
    vertices[1] = make_vertex(8, 16, 0, 8, 0, 200);
    vertices[2] = make_vertex(8, 16, 8, 8, 8, 300);
    vertices[3] = make_vertex(0, 16, 8, 0, 8, 400);
    memset(&geometry, 0, sizeof(geometry));
    geometry.vertices = vertices;
    geometry.vertex_count = 4u;
    geometry.topology = SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY;
    geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_FLOOR;

    if (!world_light_tessellate(&geometry, 1u, &mesh, error, sizeof(error)) ||
        mesh.vertex_count != 6u ||
        !mesh_has_vertex(&mesh, 0.0f, 16.0f, 0.0f, 100.0f) ||
        !mesh_has_vertex(&mesh, 8.0f, 16.0f, 0.0f, 200.0f) ||
        !mesh_has_vertex(&mesh, 8.0f, 16.0f, 8.0f, 300.0f) ||
        !mesh_has_vertex(&mesh, 0.0f, 16.0f, 8.0f, 400.0f)) {
        fprintf(stderr, "1x polygon tessellation lost source boundary values: %s\n", error);
        world_light_tessellation_mesh_release(&mesh);
        return 0;
    }
    world_light_tessellation_mesh_release(&mesh);
    if (!world_light_tessellate(&geometry, 4u, &mesh, error, sizeof(error)) ||
        mesh.vertex_count != 96u ||
        !mesh_is_bounded_and_seamless(&mesh, 100.0f, 400.0f)) {
        fprintf(stderr, "4x polygon field is not bounded or diagonal-seam free: %s\n", error);
        world_light_tessellation_mesh_release(&mesh);
        return 0;
    }
    world_light_tessellation_mesh_release(&mesh);
    return 1;
}

static int test_concave_and_water(void)
{
    SceneVertex concave_vertices[5];
    SceneVertex water_vertices[4];
    SceneGeometry geometry;
    WorldLightTessellationMesh mesh = {0};
    char error[256] = {0};

    concave_vertices[0] = make_vertex(0, 0, 0, 0, 0, 100);
    concave_vertices[1] = make_vertex(8, 0, 0, 8, 0, 180);
    concave_vertices[2] = make_vertex(4, 0, 4, 4, 4, 220);
    concave_vertices[3] = make_vertex(8, 0, 8, 8, 8, 300);
    concave_vertices[4] = make_vertex(0, 0, 8, 0, 8, 400);
    memset(&geometry, 0, sizeof(geometry));
    geometry.vertices = concave_vertices;
    geometry.vertex_count = 5u;
    geometry.topology = SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY;
    geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_CEILING;
    if (!world_light_tessellate(&geometry, 2u, &mesh, error, sizeof(error)) ||
        mesh.vertex_count != 36u ||
        !mesh_is_bounded_and_seamless(&mesh, 100.0f, 400.0f)) {
        fprintf(stderr, "concave polygon tessellation failed: %s\n", error);
        world_light_tessellation_mesh_release(&mesh);
        return 0;
    }
    world_light_tessellation_mesh_release(&mesh);

    water_vertices[0] = make_vertex(0, 0, 0, 0, 0, 275);
    water_vertices[1] = make_vertex(8, 0, 0, 8, 0, 275);
    water_vertices[2] = make_vertex(8, 0, 8, 8, 8, 275);
    water_vertices[3] = make_vertex(0, 0, 8, 0, 8, 275);
    geometry.vertices = water_vertices;
    geometry.vertex_count = 4u;
    geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_WATER;
    if (!world_light_tessellate(&geometry, 8u, &mesh, error, sizeof(error)) ||
        mesh.vertex_count != 384u ||
        !mesh_is_bounded_and_seamless(&mesh, 275.0f, 275.0f)) {
        fprintf(stderr, "water tessellation changed uniform source lighting: %s\n", error);
        world_light_tessellation_mesh_release(&mesh);
        return 0;
    }
    world_light_tessellation_mesh_release(&mesh);
    return 1;
}

int main(void)
{
    if (!world_light_tessellation_factor_valid(1u) ||
        !world_light_tessellation_factor_valid(2u) ||
        !world_light_tessellation_factor_valid(4u) ||
        !world_light_tessellation_factor_valid(8u) ||
        world_light_tessellation_factor_valid(3u) ||
        !test_wall() || !test_polygon() || !test_concave_and_water()) {
        return 1;
    }
    return 0;
}
