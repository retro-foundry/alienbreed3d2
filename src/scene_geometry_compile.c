#include "scene_geometry_compile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

SceneRenderPoint scene_render_world_point(SceneWorldPoint point)
{
    SceneRenderPoint result;

    result.x = (float)(int16_t)(uint16_t)point.x;
    result.y = -(float)point.y * SCENE_RENDER_SOURCE_Y_UNIT;
    result.z = (float)(int16_t)(uint16_t)point.z;
    return result;
}

SceneRenderPoint scene_render_camera_point(const SceneCamera *camera)
{
    SceneRenderPoint result = {0.0f, 0.0f, 0.0f};

    if (!camera) {
        return result;
    }
    result = scene_render_world_point(camera->position);
    if (camera->has_source_position_16_16 != 0u) {
        result.x = (float)((double)camera->source_position_x_16_16 / 65536.0);
        result.z = (float)((double)camera->source_position_z_16_16 / 65536.0);
    }
    return result;
}

static double cross_xz(const SceneVertex *first, const SceneVertex *second,
                       const SceneVertex *third)
{
    double ab_x = (double)second->position.x - first->position.x;
    double ab_z = (double)second->position.z - first->position.z;
    double ac_x = (double)third->position.x - first->position.x;
    double ac_z = (double)third->position.z - first->position.z;

    return ab_x * ac_z - ab_z * ac_x;
}

static int point_in_triangle_xz(const SceneVertex *point,
                                const SceneVertex *first,
                                const SceneVertex *second,
                                const SceneVertex *third, int winding)
{
    double first_cross = cross_xz(first, second, point) * winding;
    double second_cross = cross_xz(second, third, point) * winding;
    double third_cross = cross_xz(third, first, point) * winding;

    return first_cross >= 0.0 && second_cross >= 0.0 && third_cross >= 0.0;
}

static int copy_triangle_list(const SceneGeometry *geometry,
                              uint32_t **out_indices,
                              uint32_t *out_index_count,
                              char *error, size_t error_size)
{
    uint32_t *indices;

    if (geometry->vertex_count == 0u || geometry->vertex_count % 3u != 0u ||
        (size_t)geometry->vertex_count > SIZE_MAX / sizeof(*indices)) {
        set_error(error, error_size, "scene triangle list is malformed");
        return 0;
    }
    indices = (uint32_t *)malloc((size_t)geometry->vertex_count * sizeof(*indices));
    if (!indices) {
        set_error(error, error_size, "triangle-index allocation failed");
        return 0;
    }
    for (uint32_t index = 0u; index < geometry->vertex_count; ++index) {
        indices[index] = index;
    }
    *out_indices = indices;
    *out_index_count = geometry->vertex_count;
    return 1;
}

static int triangulate_polygon(const SceneGeometry *geometry,
                               uint32_t **out_indices,
                               uint32_t *out_index_count,
                               char *error, size_t error_size)
{
    uint32_t *active_indices;
    uint32_t *triangle_indices;
    uint32_t active_count;
    uint32_t output_index = 0u;
    uint32_t guard;
    double signed_area = 0.0;
    int winding;

    if (geometry->vertex_count < 3u) {
        set_error(error, error_size,
                  "source polygon has fewer than three boundary vertices");
        return 0;
    }
    if (geometry->vertex_count > UINT32_MAX / 3u + 2u ||
        (size_t)(geometry->vertex_count - 2u) >
            SIZE_MAX / (3u * sizeof(*triangle_indices))) {
        set_error(error, error_size, "source polygon is too large to triangulate");
        return 0;
    }
    active_indices =
        (uint32_t *)malloc((size_t)geometry->vertex_count * sizeof(*active_indices));
    triangle_indices = (uint32_t *)malloc(
        (size_t)(geometry->vertex_count - 2u) * 3u * sizeof(*triangle_indices));
    if (!active_indices || !triangle_indices) {
        free(active_indices);
        free(triangle_indices);
        set_error(error, error_size, "polygon triangulation allocation failed");
        return 0;
    }
    for (uint32_t index = 0u; index < geometry->vertex_count; ++index) {
        const SceneVertex *first = &geometry->vertices[index];
        const SceneVertex *second =
            &geometry->vertices[(index + 1u) % geometry->vertex_count];

        signed_area += (double)first->position.x * second->position.z -
            (double)second->position.x * first->position.z;
        active_indices[index] = index;
    }
    if (signed_area == 0.0) {
        free(active_indices);
        free(triangle_indices);
        set_error(error, error_size, "source polygon has zero X/Z area");
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
            set_error(error, error_size,
                      "source polygon triangulation did not converge");
            return 0;
        }
        for (ear_index = 0u; ear_index < active_count; ++ear_index) {
            uint32_t previous = (ear_index + active_count - 1u) % active_count;
            uint32_t next = (ear_index + 1u) % active_count;
            const SceneVertex *first =
                &geometry->vertices[active_indices[previous]];
            const SceneVertex *second =
                &geometry->vertices[active_indices[ear_index]];
            const SceneVertex *third = &geometry->vertices[active_indices[next]];
            uint32_t point_index;
            int contains_point = 0;

            if (cross_xz(first, second, third) * winding <= 0.0) {
                continue;
            }
            for (point_index = 0u; point_index < active_count; ++point_index) {
                if (point_index == previous || point_index == ear_index ||
                    point_index == next) {
                    continue;
                }
                if (point_in_triangle_xz(
                        &geometry->vertices[active_indices[point_index]], first,
                        second, third, winding)) {
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
                    (size_t)(active_count - ear_index - 1u) *
                        sizeof(*active_indices));
            --active_count;
            clipped = 1;
            break;
        }
        if (!clipped) {
            free(active_indices);
            free(triangle_indices);
            set_error(error, error_size,
                      "source polygon is not a simple X/Z boundary");
            return 0;
        }
    }
    triangle_indices[output_index++] = active_indices[0u];
    triangle_indices[output_index++] = active_indices[1u];
    triangle_indices[output_index++] = active_indices[2u];
    free(active_indices);
    *out_indices = triangle_indices;
    *out_index_count = output_index;
    return 1;
}

int scene_geometry_triangle_indices(const SceneGeometry *geometry,
                                    uint32_t **out_indices,
                                    uint32_t *out_index_count,
                                    char *error, size_t error_size)
{
    if (!geometry || !geometry->vertices || !out_indices || !out_index_count) {
        set_error(error, error_size,
                  "scene triangulation received incomplete geometry");
        return 0;
    }
    *out_indices = NULL;
    *out_index_count = 0u;
    if (geometry->topology == SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST) {
        return copy_triangle_list(geometry, out_indices, out_index_count,
                                  error, error_size);
    }
    if (geometry->topology == SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY) {
        return triangulate_polygon(geometry, out_indices, out_index_count,
                                   error, error_size);
    }
    set_error(error, error_size, "scene geometry topology is unsupported");
    return 0;
}

void scene_geometry_triangle_indices_release(uint32_t *indices)
{
    free(indices);
}
