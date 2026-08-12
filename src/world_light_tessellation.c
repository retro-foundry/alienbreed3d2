#include "world_light_tessellation.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WORLD_LIGHT_WALL_VERTEX_COUNT = 6u
};

static const double world_light_epsilon = 1.0e-9;

typedef struct {
    const SceneGeometry *geometry;
    double *distances;
    double *tangent_half_angles;
    double *weights;
} WorldLightPolygonField;

static void world_light_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size != 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

int world_light_tessellation_factor_valid(uint8_t factor)
{
    return factor == 1u || factor == 2u || factor == 4u || factor == 8u;
}

static float world_light_lerp(float first, float second, float fraction)
{
    return first + (second - first) * fraction;
}

static float world_light_smoothstep(float value)
{
    return value * value * (3.0f - 2.0f * value);
}

static WorldLightTessellationVertex world_light_scene_vertex(const SceneVertex *source)
{
    WorldLightTessellationVertex result;

    result.position_x = (float)source->position.x;
    result.position_y = (float)source->position.y;
    result.position_z = (float)source->position.z;
    result.texture_u = (float)source->texture_u;
    result.texture_v = (float)source->texture_v;
    result.source_light_level = (float)source->source_light_level;
    return result;
}

static WorldLightTessellationVertex world_light_bilinear_vertex(
    const WorldLightTessellationVertex *top_left,
    const WorldLightTessellationVertex *top_right,
    const WorldLightTessellationVertex *bottom_right,
    const WorldLightTessellationVertex *bottom_left,
    float horizontal, float vertical)
{
    WorldLightTessellationVertex top;
    WorldLightTessellationVertex bottom;
    WorldLightTessellationVertex result;
    float smooth_horizontal = world_light_smoothstep(horizontal);
    float smooth_vertical = world_light_smoothstep(vertical);

    top.position_x = world_light_lerp(top_left->position_x, top_right->position_x, horizontal);
    top.position_y = world_light_lerp(top_left->position_y, top_right->position_y, horizontal);
    top.position_z = world_light_lerp(top_left->position_z, top_right->position_z, horizontal);
    top.texture_u = world_light_lerp(top_left->texture_u, top_right->texture_u, horizontal);
    top.texture_v = world_light_lerp(top_left->texture_v, top_right->texture_v, horizontal);
    bottom.position_x = world_light_lerp(bottom_left->position_x, bottom_right->position_x,
                                         horizontal);
    bottom.position_y = world_light_lerp(bottom_left->position_y, bottom_right->position_y,
                                         horizontal);
    bottom.position_z = world_light_lerp(bottom_left->position_z, bottom_right->position_z,
                                         horizontal);
    bottom.texture_u = world_light_lerp(bottom_left->texture_u, bottom_right->texture_u,
                                        horizontal);
    bottom.texture_v = world_light_lerp(bottom_left->texture_v, bottom_right->texture_v,
                                        horizontal);
    result.position_x = world_light_lerp(top.position_x, bottom.position_x, vertical);
    result.position_y = world_light_lerp(top.position_y, bottom.position_y, vertical);
    result.position_z = world_light_lerp(top.position_z, bottom.position_z, vertical);
    result.texture_u = world_light_lerp(top.texture_u, bottom.texture_u, vertical);
    result.texture_v = world_light_lerp(top.texture_v, bottom.texture_v, vertical);
    top.source_light_level = world_light_lerp(top_left->source_light_level,
                                               top_right->source_light_level,
                                               smooth_horizontal);
    bottom.source_light_level = world_light_lerp(bottom_left->source_light_level,
                                                  bottom_right->source_light_level,
                                                  smooth_horizontal);
    result.source_light_level = world_light_lerp(top.source_light_level,
                                                  bottom.source_light_level,
                                                  smooth_vertical);
    return result;
}

static int world_light_scene_vertices_equal(const SceneVertex *first,
                                            const SceneVertex *second)
{
    return first->position.x == second->position.x &&
        first->position.y == second->position.y &&
        first->position.z == second->position.z &&
        first->texture_u == second->texture_u &&
        first->texture_v == second->texture_v &&
        first->source_light_level == second->source_light_level;
}

static int world_light_tessellate_wall(const SceneGeometry *geometry, uint8_t factor,
                                       WorldLightTessellationMesh *out_mesh,
                                       char *error, size_t error_size)
{
    WorldLightTessellationVertex corners[4];
    WorldLightTessellationVertex *vertices;
    size_t factor_squared = (size_t)factor * factor;
    size_t vertex_count = factor_squared * 6u;
    size_t output_index = 0u;

    if (geometry->topology != SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST ||
        geometry->vertex_count != WORLD_LIGHT_WALL_VERTEX_COUNT ||
        !world_light_scene_vertices_equal(&geometry->vertices[0], &geometry->vertices[3]) ||
        !world_light_scene_vertices_equal(&geometry->vertices[2], &geometry->vertices[4])) {
        world_light_set_error(error, error_size,
                              "world-light wall is not the source four-corner quad");
        return 0;
    }
    vertices = malloc(vertex_count * sizeof(*vertices));
    if (!vertices) {
        world_light_set_error(error, error_size, "world-light wall allocation failed");
        return 0;
    }
    corners[0] = world_light_scene_vertex(&geometry->vertices[0]);
    corners[1] = world_light_scene_vertex(&geometry->vertices[1]);
    corners[2] = world_light_scene_vertex(&geometry->vertices[2]);
    corners[3] = world_light_scene_vertex(&geometry->vertices[5]);
    for (uint32_t row = 0u; row < factor; ++row) {
        float top = (float)row / (float)factor;
        float bottom = (float)(row + 1u) / (float)factor;

        for (uint32_t column = 0u; column < factor; ++column) {
            float left = (float)column / (float)factor;
            float right = (float)(column + 1u) / (float)factor;
            WorldLightTessellationVertex top_left = world_light_bilinear_vertex(
                &corners[0], &corners[1], &corners[2], &corners[3], left, top);
            WorldLightTessellationVertex top_right = world_light_bilinear_vertex(
                &corners[0], &corners[1], &corners[2], &corners[3], right, top);
            WorldLightTessellationVertex bottom_right = world_light_bilinear_vertex(
                &corners[0], &corners[1], &corners[2], &corners[3], right, bottom);
            WorldLightTessellationVertex bottom_left = world_light_bilinear_vertex(
                &corners[0], &corners[1], &corners[2], &corners[3], left, bottom);

            vertices[output_index++] = top_left;
            vertices[output_index++] = top_right;
            vertices[output_index++] = bottom_right;
            vertices[output_index++] = top_left;
            vertices[output_index++] = bottom_right;
            vertices[output_index++] = bottom_left;
        }
    }
    out_mesh->vertices = vertices;
    out_mesh->vertex_count = (uint32_t)output_index;
    return 1;
}

static double world_light_cross_xz(const SceneVertex *first, const SceneVertex *second,
                                   const SceneVertex *third)
{
    double ab_x = (double)second->position.x - first->position.x;
    double ab_z = (double)second->position.z - first->position.z;
    double ac_x = (double)third->position.x - first->position.x;
    double ac_z = (double)third->position.z - first->position.z;

    return ab_x * ac_z - ab_z * ac_x;
}

static int world_light_point_in_triangle_xz(const SceneVertex *point,
                                            const SceneVertex *first,
                                            const SceneVertex *second,
                                            const SceneVertex *third,
                                            int winding)
{
    double first_cross = world_light_cross_xz(first, second, point) * winding;
    double second_cross = world_light_cross_xz(second, third, point) * winding;
    double third_cross = world_light_cross_xz(third, first, point) * winding;

    return first_cross >= 0.0 && second_cross >= 0.0 && third_cross >= 0.0;
}

static int world_light_triangulate_polygon(const SceneGeometry *geometry,
                                           uint32_t **out_triangle_indices,
                                           uint32_t *out_triangle_count,
                                           char *error, size_t error_size)
{
    uint32_t *active_indices;
    uint32_t *triangle_indices;
    uint32_t active_count;
    uint32_t output_index = 0u;
    uint32_t guard;
    double signed_area = 0.0;
    int winding;

    if (geometry->vertex_count < 3u ||
        geometry->vertex_count > UINT32_MAX / 3u + 2u) {
        world_light_set_error(error, error_size,
                              "world-light polygon has an invalid boundary size");
        return 0;
    }
    active_indices = malloc((size_t)geometry->vertex_count * sizeof(*active_indices));
    triangle_indices = malloc((size_t)(geometry->vertex_count - 2u) * 3u *
                              sizeof(*triangle_indices));
    if (!active_indices || !triangle_indices) {
        free(active_indices);
        free(triangle_indices);
        world_light_set_error(error, error_size,
                              "world-light polygon triangulation allocation failed");
        return 0;
    }
    for (uint32_t index = 0u; index < geometry->vertex_count; ++index) {
        const SceneVertex *first = &geometry->vertices[index];
        const SceneVertex *second = &geometry->vertices[(index + 1u) % geometry->vertex_count];

        signed_area += (double)first->position.x * second->position.z -
            (double)second->position.x * first->position.z;
        active_indices[index] = index;
    }
    if (signed_area == 0.0) {
        free(active_indices);
        free(triangle_indices);
        world_light_set_error(error, error_size, "world-light polygon has zero X/Z area");
        return 0;
    }
    winding = signed_area > 0.0 ? 1 : -1;
    active_count = geometry->vertex_count;
    for (guard = 0u; active_count > 3u; ++guard) {
        uint32_t ear_index;
        int clipped = 0;

        if (guard > geometry->vertex_count) {
            free(active_indices);
            free(triangle_indices);
            world_light_set_error(error, error_size,
                                  "world-light polygon triangulation did not converge");
            return 0;
        }
        for (ear_index = 0u; ear_index < active_count; ++ear_index) {
            uint32_t previous = (ear_index + active_count - 1u) % active_count;
            uint32_t next = (ear_index + 1u) % active_count;
            const SceneVertex *first = &geometry->vertices[active_indices[previous]];
            const SceneVertex *second = &geometry->vertices[active_indices[ear_index]];
            const SceneVertex *third = &geometry->vertices[active_indices[next]];
            int contains_point = 0;

            if (world_light_cross_xz(first, second, third) * winding <= 0.0) {
                continue;
            }
            for (uint32_t point_index = 0u; point_index < active_count; ++point_index) {
                if (point_index == previous || point_index == ear_index || point_index == next) {
                    continue;
                }
                if (world_light_point_in_triangle_xz(
                        &geometry->vertices[active_indices[point_index]], first, second, third,
                        winding)) {
                    contains_point = 1;
                    break;
                }
            }
            if (contains_point) {
                continue;
            }
            triangle_indices[output_index++] = active_indices[previous];
            triangle_indices[output_index++] = active_indices[ear_index];
            triangle_indices[output_index++] = active_indices[next];
            memmove(&active_indices[ear_index], &active_indices[ear_index + 1u],
                    (size_t)(active_count - ear_index - 1u) * sizeof(*active_indices));
            --active_count;
            clipped = 1;
            break;
        }
        if (!clipped) {
            free(active_indices);
            free(triangle_indices);
            world_light_set_error(error, error_size,
                                  "world-light polygon is not a simple X/Z boundary");
            return 0;
        }
    }
    triangle_indices[output_index++] = active_indices[0u];
    triangle_indices[output_index++] = active_indices[1u];
    triangle_indices[output_index++] = active_indices[2u];
    free(active_indices);
    *out_triangle_indices = triangle_indices;
    *out_triangle_count = output_index / 3u;
    return 1;
}

static int world_light_polygon_field_init(WorldLightPolygonField *field,
                                          const SceneGeometry *geometry)
{
    size_t array_size;
    double *workspace;

    if (!field || !geometry || geometry->vertex_count > SIZE_MAX / (3u * sizeof(double))) {
        return 0;
    }
    array_size = (size_t)geometry->vertex_count * sizeof(double);
    workspace = malloc(array_size * 3u);
    if (!workspace) {
        return 0;
    }
    field->geometry = geometry;
    field->distances = workspace;
    field->tangent_half_angles = workspace + geometry->vertex_count;
    field->weights = workspace + geometry->vertex_count * 2u;
    return 1;
}

static void world_light_polygon_field_destroy(WorldLightPolygonField *field)
{
    if (field) {
        free(field->distances);
        memset(field, 0, sizeof(*field));
    }
}

static float world_light_polygon_field_sample(WorldLightPolygonField *field,
                                              double point_x, double point_z)
{
    const SceneGeometry *geometry = field->geometry;
    double minimum_light = geometry->vertices[0].source_light_level;
    double maximum_light = minimum_light;
    double raw_weight_sum = 0.0;
    double enhanced_weight_sum = 0.0;
    double light_sum = 0.0;
    int mean_value_weights_valid = 1;

    for (uint32_t index = 0u; index < geometry->vertex_count; ++index) {
        const SceneVertex *vertex = &geometry->vertices[index];
        double delta_x = (double)vertex->position.x - point_x;
        double delta_z = (double)vertex->position.z - point_z;
        double distance_squared = delta_x * delta_x + delta_z * delta_z;

        if (vertex->source_light_level < minimum_light) {
            minimum_light = vertex->source_light_level;
        }
        if (vertex->source_light_level > maximum_light) {
            maximum_light = vertex->source_light_level;
        }
        if (distance_squared <= world_light_epsilon) {
            return (float)vertex->source_light_level;
        }
        field->distances[index] = sqrt(distance_squared);
    }
    for (uint32_t index = 0u; index < geometry->vertex_count; ++index) {
        uint32_t next = (index + 1u) % geometry->vertex_count;
        const SceneVertex *first = &geometry->vertices[index];
        const SceneVertex *second = &geometry->vertices[next];
        double edge_x = (double)second->position.x - first->position.x;
        double edge_z = (double)second->position.z - first->position.z;
        double point_edge_x = point_x - first->position.x;
        double point_edge_z = point_z - first->position.z;
        double edge_length_squared = edge_x * edge_x + edge_z * edge_z;
        double edge_cross = edge_x * point_edge_z - edge_z * point_edge_x;
        double edge_dot = point_edge_x * edge_x + point_edge_z * edge_z;
        double first_x = (double)first->position.x - point_x;
        double first_z = (double)first->position.z - point_z;
        double second_x = (double)second->position.x - point_x;
        double second_z = (double)second->position.z - point_z;
        double cross = first_x * second_z - first_z * second_x;
        double dot = first_x * second_x + first_z * second_z;
        double denominator = field->distances[index] * field->distances[next] + dot;

        if (edge_length_squared > world_light_epsilon &&
            edge_cross * edge_cross <= world_light_epsilon * edge_length_squared &&
            edge_dot >= 0.0 && edge_dot <= edge_length_squared) {
            float fraction = (float)(edge_dot / edge_length_squared);
            return world_light_lerp((float)first->source_light_level,
                                    (float)second->source_light_level,
                                    world_light_smoothstep(fraction));
        }
        if (fabs(denominator) <= world_light_epsilon) {
            mean_value_weights_valid = 0;
            field->tangent_half_angles[index] = 0.0;
        } else {
            field->tangent_half_angles[index] = cross / denominator;
        }
    }
    if (mean_value_weights_valid) {
        for (uint32_t index = 0u; index < geometry->vertex_count; ++index) {
            uint32_t previous = (index + geometry->vertex_count - 1u) % geometry->vertex_count;
            double raw_weight = (field->tangent_half_angles[previous] +
                                 field->tangent_half_angles[index]) /
                field->distances[index];

            field->weights[index] = raw_weight;
            raw_weight_sum += raw_weight;
        }
        if (fabs(raw_weight_sum) <= world_light_epsilon) {
            mean_value_weights_valid = 0;
        }
    }
    if (mean_value_weights_valid) {
        for (uint32_t index = 0u; index < geometry->vertex_count; ++index) {
            double normalized_weight = field->weights[index] / raw_weight_sum;

            if (normalized_weight < -world_light_epsilon) {
                mean_value_weights_valid = 0;
                break;
            }
            if (normalized_weight < 0.0) {
                normalized_weight = 0.0;
            } else if (normalized_weight > 1.0) {
                normalized_weight = 1.0;
            }
            field->weights[index] = normalized_weight * normalized_weight *
                (3.0 - 2.0 * normalized_weight);
            enhanced_weight_sum += field->weights[index];
        }
        if (enhanced_weight_sum <= world_light_epsilon) {
            mean_value_weights_valid = 0;
        }
    }
    if (!mean_value_weights_valid) {
        enhanced_weight_sum = 0.0;
        for (uint32_t index = 0u; index < geometry->vertex_count; ++index) {
            double inverse_distance = 1.0 / field->distances[index];

            field->weights[index] = inverse_distance * inverse_distance;
            enhanced_weight_sum += field->weights[index];
        }
    }
    for (uint32_t index = 0u; index < geometry->vertex_count; ++index) {
        light_sum += field->weights[index] * geometry->vertices[index].source_light_level;
    }
    light_sum /= enhanced_weight_sum;
    if (light_sum < minimum_light) {
        light_sum = minimum_light;
    } else if (light_sum > maximum_light) {
        light_sum = maximum_light;
    }
    return (float)light_sum;
}

static WorldLightTessellationVertex world_light_triangle_vertex(
    const SceneVertex *first, const SceneVertex *second, const SceneVertex *third,
    uint32_t second_weight, uint32_t third_weight, uint8_t factor,
    WorldLightPolygonField *field)
{
    double second_fraction = (double)second_weight / factor;
    double third_fraction = (double)third_weight / factor;
    double first_fraction = 1.0 - second_fraction - third_fraction;
    WorldLightTessellationVertex result;

    result.position_x = (float)(first->position.x * first_fraction +
        second->position.x * second_fraction + third->position.x * third_fraction);
    result.position_y = (float)(first->position.y * first_fraction +
        second->position.y * second_fraction + third->position.y * third_fraction);
    result.position_z = (float)(first->position.z * first_fraction +
        second->position.z * second_fraction + third->position.z * third_fraction);
    result.texture_u = (float)(first->texture_u * first_fraction +
        second->texture_u * second_fraction + third->texture_u * third_fraction);
    result.texture_v = (float)(first->texture_v * first_fraction +
        second->texture_v * second_fraction + third->texture_v * third_fraction);
    result.source_light_level = world_light_polygon_field_sample(
        field, result.position_x, result.position_z);
    return result;
}

static int world_light_tessellate_polygon(const SceneGeometry *geometry, uint8_t factor,
                                          WorldLightTessellationMesh *out_mesh,
                                          char *error, size_t error_size)
{
    uint32_t *triangle_indices = NULL;
    uint32_t triangle_count = 0u;
    WorldLightPolygonField field = {0};
    WorldLightTessellationVertex *vertices = NULL;
    size_t factor_squared = (size_t)factor * factor;
    size_t vertex_count;
    size_t output_index = 0u;

    if (geometry->topology != SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY ||
        !world_light_triangulate_polygon(geometry, &triangle_indices, &triangle_count,
                                         error, error_size)) {
        return 0;
    }
    if (triangle_count > SIZE_MAX / factor_squared / 3u) {
        free(triangle_indices);
        world_light_set_error(error, error_size, "world-light tessellation is too large");
        return 0;
    }
    vertex_count = (size_t)triangle_count * factor_squared * 3u;
    if (vertex_count > UINT32_MAX || vertex_count > SIZE_MAX / sizeof(*vertices) ||
        !world_light_polygon_field_init(&field, geometry)) {
        free(triangle_indices);
        world_light_set_error(error, error_size, "world-light polygon field allocation failed");
        return 0;
    }
    vertices = malloc(vertex_count * sizeof(*vertices));
    if (!vertices) {
        free(triangle_indices);
        world_light_polygon_field_destroy(&field);
        world_light_set_error(error, error_size, "world-light polygon allocation failed");
        return 0;
    }
    for (uint32_t triangle = 0u; triangle < triangle_count; ++triangle) {
        const SceneVertex *first = &geometry->vertices[triangle_indices[triangle * 3u]];
        const SceneVertex *second = &geometry->vertices[triangle_indices[triangle * 3u + 1u]];
        const SceneVertex *third = &geometry->vertices[triangle_indices[triangle * 3u + 2u]];

        for (uint32_t second_weight = 0u; second_weight < factor; ++second_weight) {
            for (uint32_t third_weight = 0u;
                 third_weight < (uint32_t)factor - second_weight; ++third_weight) {
                vertices[output_index++] = world_light_triangle_vertex(
                    first, second, third, second_weight, third_weight, factor, &field);
                vertices[output_index++] = world_light_triangle_vertex(
                    first, second, third, second_weight + 1u, third_weight, factor, &field);
                vertices[output_index++] = world_light_triangle_vertex(
                    first, second, third, second_weight, third_weight + 1u, factor, &field);
                if (second_weight + third_weight + 1u < factor) {
                    vertices[output_index++] = world_light_triangle_vertex(
                        first, second, third, second_weight + 1u, third_weight, factor, &field);
                    vertices[output_index++] = world_light_triangle_vertex(
                        first, second, third, second_weight + 1u, third_weight + 1u,
                        factor, &field);
                    vertices[output_index++] = world_light_triangle_vertex(
                        first, second, third, second_weight, third_weight + 1u, factor, &field);
                }
            }
        }
    }
    free(triangle_indices);
    world_light_polygon_field_destroy(&field);
    out_mesh->vertices = vertices;
    out_mesh->vertex_count = (uint32_t)output_index;
    return 1;
}

int world_light_tessellate(const SceneGeometry *geometry, uint8_t factor,
                           WorldLightTessellationMesh *out_mesh,
                           char *error, size_t error_size)
{
    if (!out_mesh) {
        world_light_set_error(error, error_size, "world-light tessellation has no output mesh");
        return 0;
    }
    memset(out_mesh, 0, sizeof(*out_mesh));
    if (!geometry || !geometry->vertices ||
        !world_light_tessellation_factor_valid(factor)) {
        world_light_set_error(error, error_size,
                              "world-light tessellation input or factor is invalid");
        return 0;
    }
    if (geometry->primitive == SCENE_GEOMETRY_PRIMITIVE_WALL) {
        return world_light_tessellate_wall(geometry, factor, out_mesh, error, error_size);
    }
    if (geometry->primitive == SCENE_GEOMETRY_PRIMITIVE_FLOOR ||
        geometry->primitive == SCENE_GEOMETRY_PRIMITIVE_CEILING ||
        geometry->primitive == SCENE_GEOMETRY_PRIMITIVE_WATER) {
        return world_light_tessellate_polygon(geometry, factor, out_mesh, error, error_size);
    }
    world_light_set_error(error, error_size, "world-light primitive is unsupported");
    return 0;
}

void world_light_tessellation_mesh_release(WorldLightTessellationMesh *mesh)
{
    if (mesh) {
        free(mesh->vertices);
        mesh->vertices = NULL;
        mesh->vertex_count = 0u;
    }
}
