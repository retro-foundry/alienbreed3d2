#include "scene_geometry_compile.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int near_value(float actual, float expected)
{
    return fabsf(actual - expected) < 0.0001f;
}

static SceneVertex vertex(int32_t x, int32_t y, int32_t z)
{
    SceneVertex result;
    memset(&result, 0, sizeof(result));
    result.position.x = x;
    result.position.y = y;
    result.position.z = z;
    return result;
}

static int test_coordinate_conversion(void)
{
    SceneWorldPoint source = {65535, 128, 32768};
    SceneCamera camera;
    SceneRenderPoint point = scene_render_world_point(source);

    if (!near_value(point.x, -1.0f) || !near_value(point.y, -1.0f) ||
        !near_value(point.z, -32768.0f)) {
        fprintf(stderr, "source world-coordinate conversion changed\n");
        return 0;
    }
    memset(&camera, 0, sizeof(camera));
    camera.position = source;
    camera.source_position_x_16_16 = 98304;
    camera.source_position_z_16_16 = -147456;
    camera.has_source_position_16_16 = 1u;
    point = scene_render_camera_point(&camera);
    if (!near_value(point.x, 1.5f) || !near_value(point.y, -1.0f) ||
        !near_value(point.z, -2.25f)) {
        fprintf(stderr, "promoted camera-coordinate conversion changed\n");
        return 0;
    }
    return 1;
}

static int test_concave_polygon(void)
{
    SceneVertex vertices[] = {
        vertex(0, 0, 0), vertex(4, 0, 0), vertex(4, 0, 4),
        vertex(2, 0, 2), vertex(0, 0, 4),
    };
    const uint32_t expected[] = {1u, 2u, 3u, 0u, 1u, 3u, 0u, 3u, 4u};
    SceneGeometry geometry;
    uint32_t *indices = NULL;
    uint32_t index_count = 0u;
    char error[160] = {0};

    memset(&geometry, 0, sizeof(geometry));
    geometry.vertices = vertices;
    geometry.vertex_count = 5u;
    geometry.topology = SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY;
    if (!scene_geometry_triangle_indices(&geometry, &indices, &index_count,
                                         error, sizeof(error))) {
        fprintf(stderr, "concave triangulation failed: %s\n", error);
        return 0;
    }
    if (index_count != sizeof(expected) / sizeof(expected[0]) ||
        memcmp(indices, expected, sizeof(expected)) != 0) {
        fprintf(stderr, "concave triangulation order changed\n");
        scene_geometry_triangle_indices_release(indices);
        return 0;
    }
    scene_geometry_triangle_indices_release(indices);
    return 1;
}

static int test_rejection_and_triangle_list(void)
{
    SceneVertex vertices[] = {
        vertex(0, 0, 0), vertex(1, 0, 0), vertex(2, 0, 0),
    };
    SceneGeometry geometry;
    uint32_t *indices = NULL;
    uint32_t count = 0u;
    char error[160] = {0};

    memset(&geometry, 0, sizeof(geometry));
    geometry.vertices = vertices;
    geometry.vertex_count = 3u;
    geometry.topology = SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY;
    if (scene_geometry_triangle_indices(&geometry, &indices, &count,
                                        error, sizeof(error)) ||
        strstr(error, "zero X/Z area") == NULL) {
        fprintf(stderr, "zero-area polygon was not rejected clearly\n");
        scene_geometry_triangle_indices_release(indices);
        return 0;
    }
    geometry.topology = SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST;
    error[0] = '\0';
    if (!scene_geometry_triangle_indices(&geometry, &indices, &count,
                                         error, sizeof(error)) ||
        count != 3u || indices[0] != 0u || indices[1] != 1u ||
        indices[2] != 2u) {
        fprintf(stderr, "triangle-list order was not retained: %s\n", error);
        scene_geometry_triangle_indices_release(indices);
        return 0;
    }
    scene_geometry_triangle_indices_release(indices);
    return 1;
}

int main(void)
{
    if (!test_coordinate_conversion() || !test_concave_polygon() ||
        !test_rejection_and_triangle_list()) {
        return 1;
    }
    return 0;
}
